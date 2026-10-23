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

// Scene pass: linear radiance -> the game's 8-bit scene value (gamma), in place.
#define POST_FX_DS_LOOK_SCENE_INOUT          0
#define POST_FX_DS_LOOK_SCENE_LINEAR_VIEW_Z  1
#define POST_FX_DS_LOOK_SCENE_HAZE           2
#define POST_FX_DS_LOOK_SCENE_CONSTANTS      3

// Bright pass: scene -> bloom base buffer (thresholded, alpha = depth rate).
#define POST_FX_DS_LOOK_BRIGHT_SCENE         0
#define POST_FX_DS_LOOK_BRIGHT_LINEAR_VIEW_Z 1
#define POST_FX_DS_LOOK_BRIGHT_OUTPUT        2
#define POST_FX_DS_LOOK_BRIGHT_CONSTANTS     3

// Box reduce: the game's one-tap downsample of the full frame, as a footprint average.
#define POST_FX_DS_LOOK_REDUCE_INPUT         0
#define POST_FX_DS_LOOK_REDUCE_OUTPUT        1
#define POST_FX_DS_LOOK_REDUCE_CONSTANTS     2

// Near rate: FRPG_Fil_Dof_NearRate and UnfocusNearRate3x3 (rate from depth, dilated).
#define POST_FX_DS_LOOK_NEAR_LINEAR_VIEW_Z   0
#define POST_FX_DS_LOOK_NEAR_OUTPUT          1
#define POST_FX_DS_LOOK_NEAR_CONSTANTS       2

// DoF blend: FRPG_Fil_Dof.
#define POST_FX_DS_LOOK_DOF_INOUT            0
#define POST_FX_DS_LOOK_DOF_LEVEL1           1
#define POST_FX_DS_LOOK_DOF_LEVEL2           2
#define POST_FX_DS_LOOK_DOF_LEVEL3           3
#define POST_FX_DS_LOOK_DOF_NEAR             4
#define POST_FX_DS_LOOK_DOF_CONSTANTS        5

// Tap filter: GaussBlur5x5, the 2x downsample and the Bloom passes; gated, the DoF's
// Unfocus3x3 and BlurUpSample.
#define POST_FX_DS_LOOK_FILTER_INPUT         0
#define POST_FX_DS_LOOK_FILTER_OUTPUT        1
#define POST_FX_DS_LOOK_FILTER_CONSTANTS     2

// Composite: FRPG_Fil_HDR_ColAdj, then back to linear for the final sRGB pass.
#define POST_FX_DS_LOOK_COMPOSITE_INOUT      0
#define POST_FX_DS_LOOK_COMPOSITE_BLOOM      1
#define POST_FX_DS_LOOK_COMPOSITE_CONSTANTS  2

#define DS_LOOK_MAX_TAPS 16

struct PostFxDarkSoulsLookSceneArgs {
  // The game's c135.x / c135.y: Remix radiance^(1/2.2) times this is the 8-bit scene value.
  float exposure;
  // 1: run the haze (fog and scattering) on surfaces. Primary misses are never hazed: the sky
  // raster is the game's sky shader output, haze included, mapped to radiance by ds1SkyRadiance.
  uint hazeEnable;
  // FRPG_Fil_Dof_DofRate, into the scene's alpha: sat(sat((viewZ - c9.x) / (c9.y - c9.x)) * c9.z).
  float dofFarBegin;
  float dofFarEnd;

  float dofFarStrength;
  uint pad0;
  uint pad1;
  uint pad2;
};

struct PostFxDarkSoulsLookReduceArgs {
  uvec2 outputSize;
  uvec2 inputSize;
};

struct PostFxDarkSoulsLookNearArgs {
  uvec2 outputSize;
  vec2 depthJitterUv;

  float missLinearViewZ;
  float nearBegin;      // c10.x: near rate = sat((viewZ - c10.x) / (c10.y - c10.x)) * c10.z
  float nearEnd;        // c10.y
  float nearStrength;   // c10.z
};

struct PostFxDarkSoulsLookDofArgs {
  uvec2 imageSize;
  vec2 invImageSize;

  vec4 blendScale;      // c66: weights = sat(rate * c66 + c67)
  vec4 blendBias;       // c67
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
  float pad0;
};

struct PostFxDarkSoulsLookFilterArgs {
  uvec2 outputSize;
  vec2 invInputSize;

  vec2 invOutputSize;
  uint tapCount;
  // 0: sum of the taps. 1: FRPG_Fil_Dof_Unfocus3x3 / BlurUpSample: a tap counts only where its
  // alpha (DoF rate) is at least the centre's, and the centre keeps the remaining weight.
  uint gated;

  // xy = offset in input texels, z = weight.
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
};
