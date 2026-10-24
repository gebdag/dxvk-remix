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
#include "rtx/pass/post_fx/post_fx_motion_blur_geometry_flags.slangh"

layout(binding = POST_FX_DS_LOOK_SCENE_INOUT)
RWTexture2D<float4> InOutColor;

layout(binding = POST_FX_DS_LOOK_SCENE_LINEAR_VIEW_Z)
Texture2D<float> LinearViewZ;

layout(binding = POST_FX_DS_LOOK_SCENE_HAZE)
ConstantBuffer<PostFxDarkSoulsHazeArgs> haze;

layout(binding = POST_FX_DS_LOOK_SCENE_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookSceneArgs> cb;

layout(binding = POST_FX_DS_LOOK_SCENE_DOF_RATE)
RWTexture2D<float> DofRate;

// The G-buffer's primary surface flags: bits 8-11 hold the surface's haze set (Surface::ds1HazeSet,
// kPrimarySurfaceFlagDs1HazeSetShift).
layout(binding = POST_FX_DS_LOOK_SCENE_SURFACE_FLAGS)
Texture2D<uint> PrimarySurfaceFlags;

// The haze constants of a pixel: those of its surface's own set (the game hazes each draw with
// its region's DrawParams), or the frame's single set for untagged surfaces.
struct HazeTerms
{
  vec4 fog;               // rgb colour, w strength
  vec3 extinction;
  float extinctionScale;
  vec3 sunDirection;
  float fogStart;
  vec3 phase;
  float fogInvRange;
  vec3 rayleigh;
  float inscatterScale;
  vec3 mie;
  float scatterBlend;
  vec3 inverseExtinction;
  vec3 surfaceTint;
  vec3 sunColor;
};

HazeTerms hazeTermsFor(uint set)
{
  HazeTerms t;
  if (set != 0 && set < DS1_HAZE_SET_COUNT && haze.sets[set].valid != 0.0f)
  {
    t.fog = vec4(haze.sets[set].fogColor, haze.sets[set].fogStrength);
    t.extinction = haze.sets[set].extinction;
    t.extinctionScale = haze.sets[set].extinctionScale;
    t.sunDirection = haze.sets[set].sunDirection;
    t.fogStart = haze.sets[set].fogStart;
    t.phase = haze.sets[set].phase;
    t.fogInvRange = haze.sets[set].fogInvRange;
    t.rayleigh = haze.sets[set].rayleigh;
    t.inscatterScale = haze.sets[set].inscatterScale;
    t.mie = haze.sets[set].mie;
    t.scatterBlend = haze.sets[set].scatterBlend;
    t.inverseExtinction = haze.sets[set].inverseExtinction;
    t.surfaceTint = haze.sets[set].surfaceTint;
    t.sunColor = haze.sets[set].sunColor;
    return t;
  }
  t.fog = vec4(haze.fogColor, haze.fogStrength);
  t.extinction = haze.extinction;
  t.extinctionScale = haze.extinctionScale;
  t.sunDirection = haze.sunDirection;
  t.fogStart = haze.fogStart;
  t.phase = haze.phase;
  t.fogInvRange = haze.fogInvRange;
  t.rayleigh = haze.rayleigh;
  t.inscatterScale = haze.inscatterScale;
  t.mie = haze.mie;
  t.scatterBlend = haze.scatterBlend;
  t.inverseExtinction = haze.inverseExtinction;
  t.surfaceTint = haze.surfaceTint;
  t.sunColor = haze.sunColor;
  return t;
}

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
    const HazeTerms t = hazeTermsFor((PrimarySurfaceFlags[viewZTexel] >> kPrimarySurfaceFlagDs1HazeSetShift) & 0xF);

    const float fog = saturate(saturate((z - t.fogStart) * t.fogInvRange) * t.fog.w);
    g = lerp(g, t.fog.rgb * haze.displayScale, fog);

    const vec3 ext = exp2(-t.extinction * dist * t.extinctionScale);
    const float cosTheta = dot(-directions.worldDirection, t.sunDirection);
    const float miePhase = t.phase.x * pow(max(t.phase.y - t.phase.z * cosTheta, 1e-4f), -1.5f);
    const vec3 inscatter = (t.rayleigh * (1.0f + cosTheta * cosTheta) + t.mie * miePhase)
      * (1.0f - ext) * t.inverseExtinction * t.inscatterScale * t.sunColor;
    g = lerp(g, g * ext * t.surfaceTint + inscatter * haze.displayScale, t.scatterBlend);
  }

  // FRPG_Fil_Dof_DofRate: the far DoF rate (a miss is the sky, at the far plane), in its own
  // texture: the game keeps it in its scene's alpha, but here other passes run before the DoF.
  const float rateZ = viewZ == haze.missLinearViewZ ? 1e9f : abs(viewZ);
  DofRate[pixel] = saturate(saturate((rateZ - cb.dofFarBegin) / max(cb.dofFarEnd - cb.dofFarBegin, 1e-6f)) * cb.dofFarStrength);

  InOutColor[pixel] = vec4(saturate(g), frame.a);
}
