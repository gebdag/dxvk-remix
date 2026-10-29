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

// Render resolution, in pixels, previous position minus current (cameraCalcMotionVectorPixel).
layout(binding = POST_FX_DS_LOOK_SCENE_MOTION_VECTOR)
Texture2D<float2> PrimaryScreenSpaceMotionVector;

layout(binding = POST_FX_DS_LOOK_SCENE_HISTORY_A_IN)
Sampler2D<float4> HistoryAIn;

layout(binding = POST_FX_DS_LOOK_SCENE_HISTORY_B_IN)
Sampler2D<float4> HistoryBIn;

layout(binding = POST_FX_DS_LOOK_SCENE_HISTORY_A_OUT)
RWTexture2D<float4> HistoryAOut;

layout(binding = POST_FX_DS_LOOK_SCENE_HISTORY_B_OUT)
RWTexture2D<float4> HistoryBOut;

// Per view Z texel, the two haze sets around it and the second's share (post_fx_dark_souls_look_region).
layout(binding = POST_FX_DS_LOOK_SCENE_HAZE_REGION)
Texture2D<uint> HazeRegion;

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

// The haze of one view Z texel as the affine map it is on the exposed colour: haze(g) = a * g + b.
// Fog: g * (1 - fog) + fogColour * fog. Scattering: g * lerp(1, ext * tint, blend) + inscatter * blend.
// A primary miss (the sky) is not hazed: a = 1, b = 0.
struct HazeMap
{
  vec3 a;
  vec3 b;
};

// One set's haze at a surface point z deep and dist away along the view direction.
HazeMap hazeMapFor(uint set, float z, float dist, CameraDirections directions)
{
  HazeMap m;
  const HazeTerms t = hazeTermsFor(set);

  const float fog = saturate(saturate((z - t.fogStart) * t.fogInvRange) * t.fog.w);

  const vec3 ext = exp2(-t.extinction * dist * t.extinctionScale);
  const float cosTheta = dot(-directions.worldDirection, t.sunDirection);
  const float miePhase = t.phase.x * pow(max(t.phase.y - t.phase.z * cosTheta, 1e-4f), -1.5f);
  const vec3 inscatter = (t.rayleigh * (1.0f + cosTheta * cosTheta) + t.mie * miePhase)
    * (1.0f - ext) * t.inverseExtinction * t.inscatterScale * t.sunColor;
  const vec3 scatterGain = lerp(vec3(1.0f), ext * t.surfaceTint, t.scatterBlend);

  m.a = (1.0f - fog) * scatterGain;
  m.b = t.fog.rgb * haze.displayScale * fog * scatterGain + inscatter * haze.displayScale * t.scatterBlend;
  return m;
}

// Its surface's set's haze, blended with the set of the surfaces around it in the world (the region
// pass): the game pairs each region's fog with that region's ambient, which Remix does not, so two
// regions' fog alone would meet in a hard line.
HazeMap hazeAt(uint2 viewZTexel, CameraDirections directions)
{
  HazeMap m;
  m.a = vec3(1.0f);
  m.b = vec3(0.0f);
  const float viewZ = LinearViewZ[viewZTexel];
  if (cb.hazeEnable == 0 || viewZ == haze.missLinearViewZ)
  {
    return m;
  }

  const float z = abs(viewZ);
  const float dist = length(cameraReconstructViewPosition(haze.camera, directions.viewDirection, z));
  const uint region = HazeRegion[viewZTexel];
  m = hazeMapFor(region & 0xF, z, dist, directions);
  const float secondShare = float(region >> 16) / 65535.0f;
  if (secondShare > 0.0f)
  {
    const HazeMap second = hazeMapFor((region >> 4) & 0xF, z, dist, directions);
    m.a = lerp(m.a, second.a, secondShare);
    m.b = lerp(m.b, second.b, secondShare);
  }
  return m;
}

// FRPG_Fil_Dof_DofRate of one view Z texel (a miss is the sky, at the far plane).
float dofRateAt(uint2 viewZTexel)
{
  const float viewZ = LinearViewZ[viewZTexel];
  const float rateZ = viewZ == haze.missLinearViewZ ? 1e9f : abs(viewZ);
  return saturate(saturate((rateZ - cb.dofFarBegin) / max(cb.dofFarEnd - cb.dofFarBegin, 1e-6f)) * cb.dofFarStrength);
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
  const CameraDirections directions = cameraScreenUVToDirection(haze.camera, screenUV, false);

  // The colour here is the upscaler's temporally resolved frame, but view Z is this frame's raster
  // at its jittered sample positions: at a silhouette, and above all against the sky, which surface
  // owns the texel a pixel reads flips from frame to frame, and so would its haze and DoF rate.
  // Resolved like the colour instead: a tent-weighted 3x3 of taps a stabilization step apart turns
  // the flip into a coverage estimate, and its reprojected history, clamped to the range of those
  // taps so a disocclusion cannot ghost, averages that estimate over the jitter sequence the
  // upscaler averaged the colour over. Haze is affine in the colour, so its maps average exactly.
  const vec2 tapStep = cb.depthStabilizationTexels / vec2(viewZSize);
  const int tapRadius = cb.depthStabilizationTexels > 0.0f ? 1 : 0;
  vec4 currentA = vec4(0.0f);   // haze gain, far DoF rate
  vec3 currentB = vec3(0.0f);   // haze offset
  vec4 minA = vec4(1e30f), maxA = vec4(-1e30f);
  vec3 minB = vec3(1e30f), maxB = vec3(-1e30f);
  float weightSum = 0.0f;
  // The history follows the front-most surface among the taps (TAA's dilated motion vector): the
  // centre texel's owner flips at a silhouette, and with it, in motion, where the history is read.
  uint2 motionTexel = min(uint2(clamp(viewZUV, 0.0f, 1.0f) * vec2(viewZSize)), viewZSize - 1);
  float frontZ = 1e30f;
  for (int y = -tapRadius; y <= tapRadius; ++y)
  {
    for (int x = -tapRadius; x <= tapRadius; ++x)
    {
      const vec2 uv = viewZUV + vec2(x, y) * tapStep;
      const uint2 viewZTexel = min(uint2(clamp(uv, 0.0f, 1.0f) * vec2(viewZSize)), viewZSize - 1);
      const float tapViewZ = LinearViewZ[viewZTexel];
      if (tapViewZ != haze.missLinearViewZ && abs(tapViewZ) < frontZ)
      {
        frontZ = abs(tapViewZ);
        motionTexel = viewZTexel;
      }
      const HazeMap tap = hazeAt(viewZTexel, directions);
      const vec4 tapA = vec4(tap.a, dofRateAt(viewZTexel));
      const float weight = float((2 - abs(x)) * (2 - abs(y)));
      currentA += tapA * weight;
      currentB += tap.b * weight;
      weightSum += weight;
      minA = min(minA, tapA);
      maxA = max(maxA, tapA);
      minB = min(minB, tap.b);
      maxB = max(maxB, tap.b);
    }
  }
  currentA /= weightSum;
  currentB /= weightSum;

  vec4 resolvedA = currentA;
  vec3 resolvedB = currentB;
  const vec2 historyUV = screenUV + PrimaryScreenSpaceMotionVector[motionTexel] / vec2(viewZSize);
  if (cb.historyValid != 0 && cb.temporalBlend < 1.0f && all(historyUV >= 0.0f) && all(historyUV <= 1.0f))
  {
    const vec4 historyA = clamp(HistoryAIn.SampleLevel(historyUV, 0.0f), minA, maxA);
    const vec3 historyB = clamp(HistoryBIn.SampleLevel(historyUV, 0.0f).rgb, minB, maxB);
    resolvedA = lerp(historyA, currentA, cb.temporalBlend);
    resolvedB = lerp(historyB, currentB, cb.temporalBlend);
  }
  HistoryAOut[pixel] = resolvedA;
  HistoryBOut[pixel] = vec4(resolvedB, 0.0f);

  const vec4 frame = InOutColor[pixel];
  const vec3 radiance = max(frame.rgb, vec3(0.0f));

  vec3 g = pow(radiance, vec3(1.0f / 2.2f)) * cb.exposure;
  g = g * resolvedA.rgb + resolvedB;

  // The far DoF rate in its own texture: the game keeps it in its scene's alpha, but here other
  // passes run before the DoF.
  DofRate[pixel] = resolvedA.a;

  InOutColor[pixel] = vec4(saturate(g), frame.a);
}
