#pragma once

// Fork: Dark Souls' haze on the tonemapped frame (post_fx_dark_souls_haze.comp.slang).
// Filled by fork_hooks::dispatchDs1Haze (rtx_fork_ds1_haze.cpp) from the rtx.ds1Haze.*
// options, which the DS1 proxy sets each frame from the game's shader constants.

#include "rtx/utility/shader_types.h"
#include "rtx/concept/camera/camera.h"

#define POST_FX_DS_HAZE_INOUT         0
#define POST_FX_DS_HAZE_LINEAR_VIEW_Z 1
#define POST_FX_DS_HAZE_CONSTANTS     2

struct PostFxDarkSoulsHazeArgs {
  Camera camera;

  // -- Struct objects should go above this line to preserve alignment --

  uvec2 imageSize;
  // 1 / view Z buffer extent: the buffer is at render resolution, the frame at output resolution.
  vec2 invViewZSize;

  // Undoes the camera jitter the view Z buffer was rasterized with (render-resolution UV).
  vec2 depthJitterUv;
  // The view Z the G-buffer stores for a primary miss (the sky).
  float missLinearViewZ;
  // Primary misses are hazed like a surface at this distance; 0 = not hazed.
  float skyDistance;

  vec3 fogColor;        // c103.rgb
  float fogStrength;    // c103.w: fog factor at full range

  vec3 extinction;      // c104.rgb, per world unit
  float extinctionScale; // c111.w * 2.0813689 (the shaders' constant)

  vec3 sunDirection;    // c111.xyz
  float fogStart;       // VS c128.x, linear view depth where fog starts

  vec3 phase;           // c107.xyz: Mie phase = x * (y - z * cos)^-1.5
  float fogInvRange;    // VS c128.y

  vec3 rayleigh;        // c108.rgb
  float inscatterScale; // c105.w

  vec3 mie;             // c109.rgb
  float scatterBlend;   // c110.w

  vec3 inverseExtinction; // c106.rgb: 1 / extinction
  float displayScale;   // the game's exposure c135.x / c135.y, times rtx.ds1Haze.displayGain

  vec3 surfaceTint;     // c105.rgb
  float pad0;

  vec3 sunColor;        // c110.rgb
  float pad1;
};
