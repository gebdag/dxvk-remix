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
#include "rtx/pass/fork_ds1_ambient.h"
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
// r: the far DoF rate. g: what the eye's lift added to this pixel's luminance, which the game's meter must not see.
RWTexture2D<float2> DofRate;

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

layout(binding = POST_FX_DS_LOOK_SCENE_ADDITIVE)
Sampler2D<float4> AdditiveRadiance;

layout(binding = POST_FX_DS_LOOK_SCENE_EYE)
Sampler2D<float2> EyeLevel;

layout(binding = POST_FX_DS_LOOK_SCENE_SKY_MATTE)
Sampler2D<float4> SkyMatte;

layout(binding = POST_FX_DS_LOOK_SCENE_PRE_LIFTED)
Sampler2D<float2> PreLifted;

// The camera ray's hazed unlit alpha layers (smoke, dust). rgb: in mode 2 (cb.layersOverHaze), what they would add to the radiance,
// which is without them. a: what all unlit alpha layers let through of the display value behind them.
layout(binding = POST_FX_DS_LOOK_SCENE_HAZED_LAYERS)
Sampler2D<float4> HazedLayers;

// x: how much of the pixel the hazed layers cover. y: that coverage times their distance from the camera.
layout(binding = POST_FX_DS_LOOK_SCENE_HAZED_LAYER_DEPTH)
Sampler2D<float2> HazedLayerDepth;

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

// The haze of a layer in front of a view Z texel's surface, dist away along the view direction: the haze of its own depth, as the
// game fogs a sprite, by the sets of the surface there (a sprite carries no set of its own; over the sky that is the frame's set).
HazeMap hazeInFrontOf(uint2 viewZTexel, float dist, CameraDirections directions)
{
  HazeMap m;
  m.a = vec3(1.0f);
  m.b = vec3(0.0f);
  if (cb.hazeEnable == 0 || !(dist > 0.0f))
  {
    return m;
  }

  // hazeAt's distance is that of the point z deep along the normalized view direction: back to its depth.
  const float z = dist * abs(directions.viewDirection.z);
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
  float skyWeight = 0.0f;
  float minSky = 1.0f, maxSky = 0.0f;
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
      const float tapSky = tapViewZ == haze.missLinearViewZ ? 1.0f : 0.0f;
      skyWeight += tapSky * weight;
      minSky = min(minSky, tapSky);
      maxSky = max(maxSky, tapSky);
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
  // What of the pixel is sky, for the eye's lift: resolved like the rest, or the lift would crawl along silhouettes.
  float resolvedSky = skyWeight / weightSum;
  const vec2 historyUV = screenUV + PrimaryScreenSpaceMotionVector[motionTexel] / vec2(viewZSize);
  if (cb.historyValid != 0 && cb.temporalBlend < 1.0f && all(historyUV >= 0.0f) && all(historyUV <= 1.0f))
  {
    const vec4 historyA = clamp(HistoryAIn.SampleLevel(historyUV, 0.0f), minA, maxA);
    const vec3 historyB = clamp(HistoryBIn.SampleLevel(historyUV, 0.0f).rgb, minB, maxB);
    resolvedA = lerp(historyA, currentA, cb.temporalBlend);
    resolvedB = lerp(historyB, currentB, cb.temporalBlend);
    resolvedSky = lerp(clamp(HistoryBIn.SampleLevel(historyUV, 0.0f).a, minSky, maxSky), resolvedSky, cb.temporalBlend);
  }
  // Mode 1 runs at render resolution: the history is the upscaled pass's.
  if (cb.mode != 1)
  {
    HistoryAOut[pixel] = resolvedA;
    HistoryBOut[pixel] = vec4(resolvedB, resolvedSky);
  }

  const vec4 frame = InOutColor[pixel];
  const vec3 radiance = max(frame.rgb, vec3(0.0f));

  // After the pass that ran before the upscaler: the lift, the haze and the layers are in the colour already.
  if (cb.mode == 2)
  {
    const vec3 scene = pow(radiance, vec3(1.0f / 2.2f)) * cb.exposure;
    DofRate[pixel] = float2(resolvedA.a, PreLifted.SampleLevel(viewZUV, 0.0f).y);
    InOutColor[pixel] = vec4(cb.keepHeadroom != 0 ? scene : saturate(scene), frame.a);
    return;
  }

  // The primary additive sprites' share of the colour and, in negative channels, what subtractive ones take off
  // (Ds1AdditiveRadiance), at this frame's jittered sample positions, as for view Z.
  const vec4 particleLayers = AdditiveRadiance.SampleLevel(viewZUV, 0.0f);
  const vec3 particles = particleLayers.rgb;
  const vec3 additive = pow(max(particles, vec3(0.0f)), vec3(1.0f / 2.2f)) * cb.exposure;
  const vec3 subtract = min(pow(max(-particles, vec3(0.0f)), vec3(1.0f / 2.2f)) * cb.exposure, vec3(0.98f));

  // The hazed unlit alpha layers (smoke, dust): the game blends such a sprite, fogged by its own depth, over its fogged scene. Here
  // the whole pixel took the haze of the surface behind it: smoke in front of a far tower went dark with the tower and showed its
  // outline. So the layers take the haze of their own depth. Mode 1: on the radiance as it is, by how much of the pixel they cover.
  // Mode 2: the resolve left them out of the radiance, and they go over the hazed frame below.
  const vec4 hazedLayers = HazedLayers.SampleLevel(viewZUV, 0.0f);
  const vec2 hazedLayerDepth = HazedLayerDepth.SampleLevel(viewZUV, 0.0f);
  const vec3 layerRadiance = cb.layersOverHaze == 2 ? max(hazedLayers.rgb, vec3(0.0f)) : vec3(0.0f);
  const float layerCoverage = cb.layersOverHaze != 0 ? saturate(hazedLayerDepth.x) : 0.0f;
  const vec3 layerGamma = pow(layerRadiance, vec3(1.0f / 2.2f)) * cb.exposure;
  // And the fog colour of the surface behind unlit alpha layers (those, and a fog wall's ribbons) shows only as far as they let it
  // through: added whole, a fog wall was lighter where a surface is behind it than where the sky is.
  const float layerThrough = lerp(1.0f, saturate(hazedLayers.a), cb.layerFogThrough);
  vec3 layerHazeA = vec3(1.0f);
  vec3 layerHazeB = vec3(0.0f);
  if (layerCoverage > 0.0f)
  {
    const uint2 layerTexel = min(uint2(clamp(viewZUV, 0.0f, 1.0f) * vec2(viewZSize)), viewZSize - 1);
    const HazeMap own = hazeInFrontOf(layerTexel, hazedLayerDepth.y / hazedLayerDepth.x, directions);
    layerHazeA = lerp(vec3(1.0f), own.a, cb.layerOwnHaze);
    layerHazeB = own.b * cb.layerOwnHaze;
  }
  // Mode 1: the layers are in the pixel's colour. What they cover of it takes their haze, the rest the surface's.
  const float layerShare = cb.layersOverHaze == 1 ? layerCoverage : 0.0f;
  const vec3 hazeA = lerp(resolvedA.rgb, layerHazeA, layerShare);

  // What hazed subtractive layers (lit map decals such as blood) take off, in alpha: they come off here, before the eye's lift
  // and the haze, as the game's fog takes them with the surface they lie on.
  const float hazedSubtract = min(pow(max(particleLayers.a, 0.0f), 1.0f / 2.2f) * cb.exposure, 0.98f);
  // The resolve scaled what is behind an unhazed subtractive layer by 1 - subtract: back to what it was. Hazed layers it left
  // out of the radiance (ds1Ambient.lookActive), so they come off without a division.
  vec3 g = pow(radiance, vec3(1.0f / 2.2f)) * cb.exposure / (vec3(1.0f) - subtract);
  g = max(g - hazedSubtract, vec3(0.0f));
  // The eye opens up where the game's meter stops (see DxvkDarkSoulsLook::updateEye): the view's lift, more in the view's dark parts and less in
  // its bright ones. The curve leaves a value of 1 at 1, so nothing clips (a torch in a dark room stays what it was), and it keeps hue. No light
  // is added: the path traced image only gets looked at differently. The sky through a window keeps its value.
  const vec3 unlifted = g;
  // The lift is of what the eye sees: the layers over the surface rise with it.
  float layerLift = 1.0f;
  if (cb.eyeViewGain > 1.0f)
  {
    const vec2 level = EyeLevel.SampleLevel(screenUV, 0.0f);
    const float share = min(pow(level.y / max(level.x, 1e-3f), cb.eyeLocal), 2.0f);
    // Where the taps are partly sky (a silhouette), the upscaled pixel itself says how much of it is sky: next to a dark object the sky
    // was lifted with it, a light outline. Its own colour against the sky's at this spot decides.
    float skyShare = resolvedSky;
    const bool skyEdge = cb.eyeSkyMatch > 0.0f && resolvedSky > 0.0f && resolvedSky < 1.0f;
    vec3 skyGamma = vec3(0.0f);
    if (skyEdge)
    {
      const vec3 sky = cb.skyBrightness * ds1SkyRadiance(cb.skyGamma, cb.skyUnexpose, SkyMatte.SampleLevel(screenUV, 0.0f).rgb);
      const float skyLevel = max(length(sky), 1e-6f);
      skyShare = 1.0f - saturate(length(radiance - sky) / (cb.eyeSkyMatch * skyLevel));
      skyGamma = pow(sky, vec3(1.0f / 2.2f)) * cb.exposure;
    }
    const float gain = lerp(min(pow(cb.eyeViewGain, share), cb.eyeMaxGain), 1.0f, skyShare);
    const vec3 seen = g + layerGamma;
    const float peak = max(seen.r, max(seen.g, seen.b));
    layerLift = gain / (1.0f + min(peak, 1.0f) * (gain - 1.0f));
    g *= layerLift;
    // A pixel on the silhouette itself is part sky, part object: lifted whole, its sky part came out brighter than the sky beside it.
    // There it rises no higher than the sky (or than it already was).
    // Weighted by how much of the pixel the taps call sky, so an object in front of a dimmer sky keeps its lift.
    if (skyEdge)
    {
      g = lerp(g, min(g, max(unlifted, skyGamma)), resolvedSky);
    }
  }
  // Whatever the eye makes of it, the frame's light is the same: the meter reads the frame without the lift.
  const float liftedLuminance = dot(saturate((g - unlifted) * hazeA + layerGamma * (layerLift - 1.0f) * layerHazeA),
                                    vec3(0.2125f, 0.7154f, 0.0721f));
  g = g * hazeA + resolvedB * layerThrough + layerHazeB * layerShare;
  // The game draws its additive particles after the fog, unfogged (their vertex shader gets c159.xy = (1, 0) for
  // them), and adds them in gamma: the haze above dimmed their share of the colour by the haze gain, so it
  // goes back. And it takes its subtractive ones off in gamma, absolutely, whatever is behind them.
  g += additive * max(1.0f - hazeA, vec3(0.0f));
  g = max(g - subtract, vec3(0.0f));
  // Mode 2: the hazed layers over it all, as the game's blend puts them: their colour (the resolve composited them in gamma, each under
  // the ones in front of it) with the haze of their own depth, and that depth's fog colour as far as they cover the pixel.
  if (cb.layersOverHaze == 2)
  {
    g += layerGamma * layerLift * layerHazeA + layerHazeB * layerCoverage;
  }

  // Before the upscaler: back to radiance for the upscaler, unclipped (mode 2 clips), and the lift's luminance for the meter.
  if (cb.mode == 1)
  {
    DofRate[pixel] = float2(0.0f, liftedLuminance);
    InOutColor[pixel] = vec4(pow(max(g, vec3(0.0f)) / max(cb.exposure, 1e-6f), vec3(2.2f)), frame.a);
    return;
  }

  // The far DoF rate in its own texture: the game keeps it in its scene's alpha, but here other
  // passes run before the DoF.
  DofRate[pixel] = float2(resolvedA.a, liftedLuminance);

  // The game's target is 8-bit: the clip. With HDR10 output the value above 1 stays (what the game's
  // shaders computed and its target cut); the meter and the bloom bright pass clip it themselves.
  InOutColor[pixel] = vec4(cb.keepHeadroom != 0 ? g : saturate(g), frame.a);
}
