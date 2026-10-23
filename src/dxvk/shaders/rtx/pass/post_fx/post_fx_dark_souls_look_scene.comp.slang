// Fork: Dark Souls' 8-bit scene value from Remix's linear radiance (see post_fx_dark_souls_look.h).
//
// Every DS1 lit pixel shader ends with
//   col   = albedo * light                       (gamma space, no sRGB anywhere)
//   col   = lerp(col, c103.rgb, fog)             fog
//   col   = lerp(col, col * ext * c105 + in, c110.w)   sky scattering
//   scene = sat(col * c135.x / c135.y)           into an A8R8G8B8 target
// A product in gamma space is the same product of the 2.2 powers in linear space, so with
// Remix radiance standing in for (albedo * light)^2.2, radiance^(1/2.2) * c135.x / c135.y is the
// game's exposed colour. Fog and scattering are affine in col, so on the exposed colour they
// run with the colours they add scaled by the exposure. The clip comes last, as in the game.

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/utility/common.slangh"
#include "rtx/utility/color.slangh"
#include "rtx/concept/camera/camera.slangh"

layout(binding = POST_FX_DS_LOOK_SCENE_INOUT)
RWTexture2D<float4> InOutColor;

layout(binding = POST_FX_DS_LOOK_SCENE_LINEAR_VIEW_Z)
Texture2D<float> LinearViewZ;

layout(binding = POST_FX_DS_LOOK_SCENE_HAZE)
ConstantBuffer<PostFxDarkSoulsHazeArgs> haze;

layout(binding = POST_FX_DS_LOOK_SCENE_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookSceneArgs> cb;

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(pixel >= haze.imageSize))
  {
    return;
  }

  const vec2 screenUV = (vec2(pixel) + 0.5f) / vec2(haze.imageSize);
  uint2 viewZSize;
  LinearViewZ.GetDimensions(viewZSize.x, viewZSize.y);
  const vec2 viewZUV = screenUV + haze.depthJitterUv;
  const uint2 viewZTexel = min(uint2(max(viewZUV, 0.0f) * vec2(viewZSize)), viewZSize - 1);
  const float viewZ = LinearViewZ[viewZTexel];

  const vec4 frame = InOutColor[pixel];
  const vec3 radiance = max(frame.rgb, vec3(0.0f));

  vec3 g = pow(radiance, vec3(1.0f / 2.2f)) * cb.exposure;
  if (cb.hazeEnable != 0 && viewZ != haze.missLinearViewZ)
  {
    const CameraDirections directions = cameraScreenUVToDirection(haze.camera, screenUV, false);
    const float z = abs(viewZ);
    const float dist = length(cameraReconstructViewPosition(haze.camera, directions.viewDirection, z));

    const float fog = saturate(saturate((z - haze.fogStart) * haze.fogInvRange) * haze.fogStrength);
    g = lerp(g, haze.fogColor * haze.displayScale, fog);

    const vec3 ext = exp2(-haze.extinction * dist * haze.extinctionScale);
    const float cosTheta = dot(-directions.worldDirection, haze.sunDirection);
    const float miePhase = haze.phase.x * pow(max(haze.phase.y - haze.phase.z * cosTheta, 1e-4f), -1.5f);
    const vec3 inscatter = (haze.rayleigh * (1.0f + cosTheta * cosTheta) + haze.mie * miePhase)
      * (1.0f - ext) * haze.inverseExtinction * haze.inscatterScale * haze.sunColor;
    g = lerp(g, g * ext * haze.surfaceTint + inscatter * haze.displayScale, haze.scatterBlend);
  }

  // FRPG_Fil_Dof_DofRate: the far DoF rate into alpha (a miss is the sky, at the far plane).
  const float rateZ = viewZ == haze.missLinearViewZ ? 1e9f : abs(viewZ);
  const float farRate = saturate(saturate((rateZ - cb.dofFarBegin) / max(cb.dofFarEnd - cb.dofFarBegin, 1e-6f)) * cb.dofFarStrength);

  InOutColor[pixel] = vec4(saturate(g), farRate);
}
