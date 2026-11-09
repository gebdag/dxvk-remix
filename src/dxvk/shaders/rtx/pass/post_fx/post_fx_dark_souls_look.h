#pragma once

// Fork: Dark Souls' own image pipeline after the path tracer (rtx_dark_souls_look.cpp).
//
// The game renders every lit draw in gamma space into an 8-bit A8R8G8B8 target:
//   scene  = sat(haze(albedo * light) * c135.x / c135.y)
// and FRPG_Fil_HDR_ColAdj builds the displayed frame from it:
//   out    = sat((sat(scene * c56.z) + bloom * lerp(c56.x, c71.y, bloom.a)) * M[c62..c64])
// with the bloom made from that same 8-bit scene: a 1/N downsample, BrightPassFilter
// (threshold lerp(c11.x, c71.x, depth rate)), GaussBlur5x5, a 2x downsample, another
// GaussBlur5x5 and a separable 15-tap FRPG_Fil_Bloom pair. These passes run the same
// steps on Remix's linear radiance, which stands in for albedo * light raised to 2.2.

#include "rtx/utility/shader_types.h"
#include "rtx/pass/post_fx/post_fx_dark_souls_haze.h"
#include "rtx/pass/fork_ds1_ambient.h"

// Scene pass: linear radiance -> the game's 8-bit scene value (gamma), in place.
#define POST_FX_DS_LOOK_SCENE_INOUT          0
#define POST_FX_DS_LOOK_SCENE_LINEAR_VIEW_Z  1
#define POST_FX_DS_LOOK_SCENE_HAZE           2
#define POST_FX_DS_LOOK_SCENE_CONSTANTS      3
#define POST_FX_DS_LOOK_SCENE_DOF_RATE       4
#define POST_FX_DS_LOOK_SCENE_SURFACE_FLAGS  5
// The depth-derived terms' history (see PostFxDarkSoulsLookSceneArgs::temporalBlend): last frame's,
// sampled, and this frame's, written. A = haze map gain (rgb) and far DoF rate, B = haze map offset.
#define POST_FX_DS_LOOK_SCENE_MOTION_VECTOR  6
#define POST_FX_DS_LOOK_SCENE_HISTORY_A_IN   7
#define POST_FX_DS_LOOK_SCENE_HISTORY_B_IN   8
#define POST_FX_DS_LOOK_SCENE_HISTORY_A_OUT  9
#define POST_FX_DS_LOOK_SCENE_HISTORY_B_OUT  10
// The haze region blend of each view Z texel (the region pass's output, below).
#define POST_FX_DS_LOOK_SCENE_HAZE_REGION    11
// The primary radiance's unlit additive sprite part (GBUFFER_BINDING_DS1_ADDITIVE_RADIANCE_OUTPUT).
#define POST_FX_DS_LOOK_SCENE_ADDITIVE       12
// The eye's local adaptation level (post_fx_dark_souls_look_eye.comp.slang).
#define POST_FX_DS_LOOK_SCENE_EYE            13
// The sky matte (the game's sky raster at output resolution), for the eye lift's sky test at silhouettes.
#define POST_FX_DS_LOOK_SCENE_SKY_MATTE      14
// The frame before upscaling (Composite Output), at the additive layers' resolution: their share of the colour.
#define POST_FX_DS_LOOK_SCENE_LOW_RES_COLOR  15

#define POST_FX_DS_LOOK_EYE_FRAME            0
#define POST_FX_DS_LOOK_EYE_LINEAR_VIEW_Z    1
#define POST_FX_DS_LOOK_EYE_HISTORY          2
#define POST_FX_DS_LOOK_EYE_OUTPUT           3
#define POST_FX_DS_LOOK_EYE_CONSTANTS        4

// Haze region blend (post_fx_dark_souls_look_region.comp.slang): per view Z texel, the two haze sets
// of the surfaces within PostFxDarkSoulsLookRegionArgs::radius of it in the world and the second's
// share, which the scene pass blends its haze between.
#define POST_FX_DS_LOOK_REGION_LINEAR_VIEW_Z 0
#define POST_FX_DS_LOOK_REGION_SURFACE_FLAGS 1
#define POST_FX_DS_LOOK_REGION_HAZE          2
#define POST_FX_DS_LOOK_REGION_CONSTANTS     3
#define POST_FX_DS_LOOK_REGION_OUTPUT        4

// Bright pass: scene -> bloom base buffer (thresholded, alpha = depth rate).
#define POST_FX_DS_LOOK_BRIGHT_SCENE         0
#define POST_FX_DS_LOOK_BRIGHT_LINEAR_VIEW_Z 1
#define POST_FX_DS_LOOK_BRIGHT_OUTPUT        2
#define POST_FX_DS_LOOK_BRIGHT_CONSTANTS     3
#define POST_FX_DS_LOOK_BRIGHT_FOG_GATE_MASK 4

// Near rate: FRPG_Fil_Dof_NearRate and UnfocusNearRate3x3 (rate from depth, dilated).
#define POST_FX_DS_LOOK_NEAR_LINEAR_VIEW_Z   0
#define POST_FX_DS_LOOK_NEAR_OUTPUT          1
#define POST_FX_DS_LOOK_NEAR_CONSTANTS       2

// DoF prepare: the frame as it is when the DoF runs, with the far DoF rate in alpha for the
// rate-gated level blurs. The rate lives in its own texture from the scene pass on, so passes in
// between (any Remix effect) may write the frame's alpha freely.
#define POST_FX_DS_LOOK_DOF_PREPARE_FRAME    0
#define POST_FX_DS_LOOK_DOF_PREPARE_RATE     1
#define POST_FX_DS_LOOK_DOF_PREPARE_OUTPUT   2

// DoF blend: FRPG_Fil_Dof. Its three blur levels are Gaussians with the spread the game's 512x360 / 256x180 chain has
// on screen (rtx_dark_souls_look.cpp), at half resolution unless rtx.ds1Look.dofFullRes, not low-res copies.
#define POST_FX_DS_LOOK_DOF_INOUT            0
#define POST_FX_DS_LOOK_DOF_LEVEL1           1
#define POST_FX_DS_LOOK_DOF_LEVEL2           2
#define POST_FX_DS_LOOK_DOF_LEVEL3           3
#define POST_FX_DS_LOOK_DOF_NEAR             4
#define POST_FX_DS_LOOK_DOF_CONSTANTS        5
#define POST_FX_DS_LOOK_DOF_RATE             6

// Tap filter: GaussBlur5x5, the 2x downsample and the Bloom passes; gated, the DoF's
// Unfocus3x3 and BlurUpSample.
#define POST_FX_DS_LOOK_FILTER_INPUT         0
#define POST_FX_DS_LOOK_FILTER_OUTPUT        1
#define POST_FX_DS_LOOK_FILTER_CONSTANTS     2

// Composite: FRPG_Fil_HDR_ColAdj, then back to linear for the final sRGB pass.
#define POST_FX_DS_LOOK_COMPOSITE_INOUT      0
#define POST_FX_DS_LOOK_COMPOSITE_BLOOM      1
#define POST_FX_DS_LOOK_COMPOSITE_CONSTANTS  2
#define POST_FX_DS_LOOK_COMPOSITE_SHAFTS     3
// Fork: Remix's own tonemapped frame (linear), which rtx.ds1Look.intensity below 1 blends the look with.
#define POST_FX_DS_LOOK_COMPOSITE_STOCK      4

// Light shafts: the game's FRPG_Fil_LightShaft chain. Its source is not the frame: after the scene the
// game draws some sky layers again (Undead Parish: the dome and the shaft planes, not the cloud layers)
// into a 512x360 buffer cleared to black, with the same constants, depth tested against a copy of the
// scene's depth (FRPG_Fil_DepthCopy). Then a bright pass of it at c71.x (BrightPassFilter, the same
// threshold near and far), GaussBlur5x5 and two radial blurs towards the shaft sun's screen position
// (c69), (c70.x, c70.y, c70.z) = (100, 0.04, 0.434) and (10, 1, 0.92) in the game; ColAdj adds the result
// times c56.y. The DS1 proxy tags those layers' main-pass draws and the runtime rasterizes them into a cube
// around the camera (Resources::getDs1ShaftSource); post_fx_dark_souls_look_shafts.comp.slang runs the chain
// on view directions from there (modes 0-3, PostFxDarkSoulsLookShaftArgs), and the composite adds the
// result where ColAdj does. The TLAS sits at the common ray tracing index.
#define POST_FX_DS_LOOK_SHAFTS_TLAS          0
#define POST_FX_DS_LOOK_SHAFTS_SOURCE        1
#define POST_FX_DS_LOOK_SHAFTS_INPUT         2
#define POST_FX_DS_LOOK_SHAFTS_OUTPUT_CUBE   3
#define POST_FX_DS_LOOK_SHAFTS_OUTPUT        4
#define POST_FX_DS_LOOK_SHAFTS_LINEAR_VIEW_Z 5
#define POST_FX_DS_LOOK_SHAFTS_CONSTANTS     6

#define DS_LOOK_MAX_TAPS 64

// Meter: the game's luminance meter (FRPG_Fil_SampleLumInitial / SampleLumFinal) on the scene pass
// output, which is what the game meters: its 8-bit scene, downsampled to the bright pass size
// (256x180), decoded by c56.z * c56.w = c135.y / c135.x, luminance (0.2125, 0.7154, 0.0721),
// ln with a floor of ln(1e-4), averaged and exponentiated. One thread per grid cell; each group
// writes the sum of its cells' ln to Partials, the CPU adds them up.
#define POST_FX_DS_LOOK_METER_SCENE          0
#define POST_FX_DS_LOOK_METER_PARTIALS       1
#define POST_FX_DS_LOOK_METER_CONSTANTS      2
#define POST_FX_DS_LOOK_METER_LIFT           3
#define DS_LOOK_METER_GROUP                  16

struct PostFxDarkSoulsLookMeterArgs {
  uvec2 gridSize;       // the game's meter source size, its bright pass size
  uvec2 sceneSize;

  float exposure;       // the frame's exposure, c135.x / c135.y: the decode divides by it
  uint groupsX;
  uint useLift;         // the eye lifted this frame: take that off
  uint pad1;
};

// Ambient meter: how much of the game's ambient the path-traced frame keeps. The game lights its
// surfaces with an unoccluded probe, and its exposure clamp (c54.y..c54.z) is set for frames lit
// that way; path tracing occludes that ambient, so enclosed areas meter far below the clamp and
// the exposure cannot follow them. Over the meter grid, before the scene pass, this sums for the
// surface taps of each cell (primary hits with a diffuse albedo) the ln of the gamma-space
// luminance of
//   the frame:     radiance^(1/2.2)
//   the estimate:  (albedo * ambient cube(N))^(1/2.2), the game's unoccluded ambient on it
// and the count of such taps: three floats per group at partialsOffset. exp of the mean
// difference is the ratio the game's clamp is scaled by (DxvkDarkSoulsExposure::adapt).
#define POST_FX_DS_LOOK_AMBIENT_METER_RADIANCE      0
#define POST_FX_DS_LOOK_AMBIENT_METER_ALBEDO        1
#define POST_FX_DS_LOOK_AMBIENT_METER_NORMAL        2
#define POST_FX_DS_LOOK_AMBIENT_METER_LINEAR_VIEW_Z 3
#define POST_FX_DS_LOOK_AMBIENT_METER_PARTIALS      4
#define POST_FX_DS_LOOK_AMBIENT_METER_CONSTANTS     5

struct PostFxDarkSoulsLookAmbientMeterArgs {
  Ds1AmbientArgs ambient;

  uvec2 gridSize;
  uvec2 radianceSize;

  uvec2 gbufferSize;    // albedo and shading normal (compacted, render resolution)
  uvec2 viewZSize;

  float missLinearViewZ;
  float minAlbedo;      // taps with a darker diffuse albedo (metal, black) are left out
  uint groupsX;
  uint partialsOffset;  // in floats
};

struct PostFxDarkSoulsLookSceneArgs {
  // The game's c135.x / c135.y: Remix radiance^(1/2.2) times this is the 8-bit scene value.
  float exposure;
  // 1: run the haze (fog and scattering) on surfaces. Primary misses are never hazed: the sky
  // raster is the game's sky shader output, haze included, mapped to radiance by ds1SkyRadiance.
  uint hazeEnable;
  // FRPG_Fil_Dof_DofRate, into the DoF rate texture: sat(sat((viewZ - c9.x) / (c9.y - c9.x)) * c9.z).
  float dofFarBegin;
  float dofFarEnd;

  float dofFarStrength;
  // The depth-derived terms (haze, DoF rate) are averaged over a 3x3 of taps this many view Z texels
  // apart (0: the centre alone). See depthStabilizationTexels in rtx_dark_souls_look.h.
  float depthStabilizationTexels;
  // Weight of this frame's terms against their reprojected history, which is clamped to the range
  // of the taps (1: no history). See depthTemporalBlend in rtx_dark_souls_look.h.
  float temporalBlend;
  // 0 when there is no history to read (first frame, a resize, a frame the pass did not run).
  uint historyValid;

  // The eye's adaptation (rtx.ds1Look.eye*): the view's lift, 1 = none, how much more its dark parts take, the largest lift.
  float eyeViewGain;
  float eyeLocal;
  float eyeMaxGain;
  // DS1 HDR10 output (rtx_hdr_output.h): 1 = the scene value is not clipped at 1; what the game's 8-bit
  // target would cut stays for the display. The meter and the bloom read it clipped.
  uint keepHeadroom;

  // The sky matte's mapping to radiance, as the composite's (ds1SkyRadiance), and how near the upscaled colour must be to it to
  // count as sky in the eye lift where the depth taps are partly sky (rtx.ds1Look.eyeSkyMatch; 0 = the taps alone).
  uint skyGamma;
  float skyUnexpose;
  float skyBrightness;
  float eyeSkyMatch;

  // 1: POST_FX_DS_LOOK_SCENE_LOW_RES_COLOR is the frame before upscaling, so the additive layers come back as a share of the upscaled
  // colour; 0 (no upscaler, the same image): as they are.
  uint lowResShare;
  uint lowResPad0;
  uint lowResPad1;
  uint lowResPad2;
};

struct PostFxDarkSoulsLookEyeArgs {
  uvec2 frameSize;
  uvec2 outputSize;
  uvec2 viewZSize;
  float exposure;
  float missLinearViewZ;
  float riseBlend;      // per frame, towards a brighter level
  float fallBlend;      // per frame, towards a darker one
  uint historyValid;
  uint pad;
};

struct PostFxDarkSoulsLookRegionArgs {
  uvec2 size;           // the view Z buffer's
  // World units: surfaces this close to a texel's share its haze, falling off with the distance.
  // 0 = every texel keeps its own surface's set.
  float radius;
  // Cap on the disc's radius on screen, in UV (right in front of the camera it would cover the screen).
  float maxUvRadius;

  uint tapCount;
  uint frameIndex;      // rotates the tap pattern
  uint pad0;
  uint pad1;
};

struct PostFxDarkSoulsLookNearArgs {
  uvec2 outputSize;
  vec2 depthJitterUv;

  float missLinearViewZ;
  float nearBegin;      // c10.x: near rate = sat((viewZ - c10.x) / (c10.y - c10.x)) * c10.z
  float nearEnd;        // c10.y
  float nearStrength;   // c10.z

  // The UnfocusNearRate3x3 dilation footprint: one texel of the game's half-size DoF level, in UV.
  vec2 dilationUv;
  // As PostFxDarkSoulsLookSceneArgs::depthStabilizationTexels, for each rate lookup.
  float depthStabilizationTexels;
  uint pad1;
};

struct PostFxDarkSoulsLookDofArgs {
  uvec2 imageSize;
  vec2 invImageSize;

  vec4 blendScale;      // c66: weights = sat(rate * c66 + c67)
  vec4 blendBias;       // c67

  uint useNear;         // 0: no near DoF this frame, its rate image is stale
  uint pad0;
  uint pad1;
  uint pad2;
};

struct PostFxDarkSoulsLookBrightArgs {
  uvec2 outputSize;
  uvec2 sceneSize;

  vec2 depthJitterUv;   // render-resolution UV jitter of the view Z buffer
  float missLinearViewZ;
  float thresholdNear;  // c11.x

  float thresholdFar;   // c71.x
  float depthBegin;     // c71.z: bloom depth rate = sat((viewZ - begin) / (end - begin))
  float depthEnd;       // c71.w
  uint useFogGateMask;  // fog gates were drawn: post_fx_dark_souls_fog_gate_march
};

struct PostFxDarkSoulsLookFilterArgs {
  uvec2 outputSize;
  vec2 invInputSize;

  vec2 invOutputSize;
  uint tapCount;
  // 0: sum of the taps. 1: FRPG_Fil_Dof_Unfocus3x3 / BlurUpSample: a tap counts only where its
  // alpha (DoF rate) is at least the centre's, and the centre keeps the remaining weight.
  // 2: FRPG_Fil_Bloom: a tap's w weighs alpha, and the result clips at 1 like the game's 8-bit target.
  uint gated;

  // xy = offset in input texels, z = weight, w = alpha weight for gated 2.
  vec4 taps[DS_LOOK_MAX_TAPS];
};

struct PostFxDarkSoulsLookCompositeArgs {
  uvec2 imageSize;
  vec2 invImageSize;

  float sceneScale;     // c56.z
  float bloomRateNear;  // c56.x
  float bloomRateFar;   // c71.y
  uint bloomEnable;

  vec4 gradeRowR;       // c62: dot((col, 1), row)
  vec4 gradeRowG;       // c63
  vec4 gradeRowB;       // c64

  uint shaftEnable;
  float shaftWeight;    // c56.y
  // rtx.ds1Look.intensity: the look over Remix's own frame (STOCK) in gamma space; 1 = the look alone.
  float intensity;
  // rtx.ds1Look.blackToe: 0 = the grade's hard clip at black, 1 = the full toe (post_fx_dark_souls_look_composite.comp.slang).
  float blackToe;

  // Image adjustments on top of the look (rtx.ds1Look.adjust*), display (gamma) values; adjustEnable 0 = all at their
  // defaults, which leave the image as it is.
  float adjustExposure;   // gain on the scene value, 2^(EV / 2.2)
  float adjustGain;       // brightness gain after the grade
  float adjustGamma;      // midtones: value^(1 / gamma)
  float adjustLift;       // shadows: lift + (1 - lift) * value
  float adjustContrast;   // around 0.5
  float adjustSaturation; // against Rec. 709 luma
  uint adjustEnable;
  // DS1 HDR10 output (rtx_hdr_output.h): 1 = ColAdj's clips (sat(scene * c56.z), the graded 8-bit output)
  // are replaced by a roll-off (hdrRollOff, in gamma space) from hdrKneeGamma towards hdrLimitGamma, so what
  // they would cut goes to the display above SDR white.
  uint keepHeadroom;
  float hdrLimitGamma;  // the display's headroom (peak / paper white) as a gamma value
  float hdrPerChannel;  // rtx.hdr.rollOffPerChannel
  float hdrKneeGamma;   // rtx.hdr.knee
  uint pad3;
};

// One stage of the light shaft chain in direction space (post_fx_dark_souls_look_shafts.comp.slang).
struct PostFxDarkSoulsLookShaftArgs {
  Camera camera;

  // -- Struct objects should go above this line to preserve alignment --

  uint mode;            // 0 source, 1 blur, 2 pass 1 (cubes), 3 pass 2 (to the bright pass buffer)
  uint cubeSize;
  uvec2 outputSize;     // mode 3

  vec3 sunDirection;    // towards the shaft sun (the proxy recovers it from c69)
  float texelAngle;     // one texel of the game's bright pass buffer at the screen's centre, in radians

  float maxLength;      // c70.x: the most the taps reach, in texels
  float weight;         // c70.y
  float decay;          // c70.z: each tap's weight times this
  float threshold;      // c71.x of the game's shaft bright pass

  // The sky raster turned into the game's scene value, as the scene pass turns a primary miss:
  // (skyBrightness * ds1SkyRadiance(raster))^(1/2.2) * exposure.
  float skyBrightness;
  float exposure;
  float skyUnexpose;
  uint skyGamma;

  vec2 depthJitterUv;   // render-resolution UV jitter of the view Z buffer
  float missLinearViewZ;
  uint pad0;
};
