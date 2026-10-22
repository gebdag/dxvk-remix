// Fork: Dark Souls' haze, applied to the tonemapped frame.
//
// Every DS1 map and character pixel shader ends the same way, on its own display
// colour (gamma encoded, before the exposure c135.x / c135.y scales it):
//   fog:     col = lerp(col, c103.rgb, sat(sat((viewZ - c128.x) * c128.y) * c103.w))
//   scatter: ext = exp2(-c104 * dist * c111.w * 2.0813689)
//            cos = dot(toCamera, c111.xyz)
//            in  = (c108 * (1 + cos^2) + c109 * c107.x * (c107.y - c107.z * cos)^-1.5)
//                  * (1 - ext) * c106 * c105.w * c110.rgb   (c106 = 1 / extinction, c110.rgb = sun colour)
//            col = lerp(col, col * ext * c105.rgb + in, c110.w)
// The tonemapped frame stands in for the game's exposed display colour, so the
// formulas run on it gamma encoded. Both steps are affine in col, so the game's
// exposure only scales the colours they add. Runs where the game's order puts it:
// before depth of field and the colour grade.

#include "rtx/pass/post_fx/post_fx_dark_souls_haze.h"
#include "rtx/utility/common.slangh"
#include "rtx/utility/color.slangh"
#include "rtx/concept/camera/camera.slangh"

layout(binding = POST_FX_DS_HAZE_INOUT)
RWTexture2D<float4> InOutColor;

layout(binding = POST_FX_DS_HAZE_LINEAR_VIEW_Z)
Texture2D<float> LinearViewZ;

layout(binding = POST_FX_DS_HAZE_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsHazeArgs> cb;

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(pixel >= cb.imageSize))
  {
    return;
  }

  const vec2 screenUV = (vec2(pixel) + 0.5f) / vec2(cb.imageSize);
  const CameraDirections directions = cameraScreenUVToDirection(cb.camera, screenUV, false);

  uint2 viewZSize;
  LinearViewZ.GetDimensions(viewZSize.x, viewZSize.y);
  const vec2 viewZUV = screenUV + cb.depthJitterUv;
  const uint2 viewZTexel = min(uint2(max(viewZUV, 0.0f) * vec2(viewZSize)), viewZSize - 1);
  float viewZ = LinearViewZ[viewZTexel];

  float dist;
  if (viewZ == cb.missLinearViewZ)
  {
    if (cb.skyDistance <= 0.0f)
    {
      return;
    }
    viewZ = cb.skyDistance;
    dist = cb.skyDistance;
  }
  else
  {
    viewZ = abs(viewZ);
    dist = length(cameraReconstructViewPosition(cb.camera, directions.viewDirection, viewZ));
  }

  const vec4 frame = InOutColor[pixel];
  // The same power both ways, so a pixel the haze leaves alone comes back unchanged.
  vec3 g = pow(max(frame.rgb, vec3(0.0f)), vec3(1.0f / kSRGBGamma));

  const float fog = saturate(saturate((viewZ - cb.fogStart) * cb.fogInvRange) * cb.fogStrength);
  g = lerp(g, cb.fogColor * cb.displayScale, fog);

  const vec3 ext = exp2(-cb.extinction * dist * cb.extinctionScale);
  const float cosTheta = dot(-directions.worldDirection, cb.sunDirection);
  const float miePhase = cb.phase.x * pow(max(cb.phase.y - cb.phase.z * cosTheta, 1e-4f), -1.5f);
  const vec3 inscatter = (cb.rayleigh * (1.0f + cosTheta * cosTheta) + cb.mie * miePhase)
    * (1.0f - ext) * cb.inverseExtinction * cb.inscatterScale * cb.sunColor;
  g = lerp(g, g * ext * cb.surfaceTint + inscatter * cb.displayScale, cb.scatterBlend);

  InOutColor[pixel] = vec4(pow(saturate(g), vec3(kSRGBGamma)), frame.a);
}
