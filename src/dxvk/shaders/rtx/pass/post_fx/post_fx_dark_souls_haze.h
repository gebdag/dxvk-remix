#pragma once

// Fork: Dark Souls' haze constants for the DS1 look's scene pass (post_fx_dark_souls_look.h). Filled by
// DxvkDarkSoulsHaze::fillArgs from the rtx.ds1Haze.* options, which the DS1 proxy sets each frame from the
// game's shader constants.

#include "rtx/utility/shader_types.h"
#include "rtx/concept/camera/camera.h"

// DS1 applies its per-region DrawParams per draw: every lit draw carries its own fog and
// scattering constants. The proxy tags each draw with a slot (Surface::ds1HazeSet, 1-14) and sends
// the slots' constants (rtx.ds1Haze.sets); a pixel is hazed with its primary surface's set. Slot 0
// (untagged surfaces) uses the single set below (the one most of the frame uses). Slot 15 is no set: a layer the
// haze leaves alone (Surface::isDs1Unhazed); the proxy hands out slots 1-14.
#define DS1_HAZE_SET_COUNT 16

// vec3 + float pairs, not vec4: C++'s vec4 is alignas(16) while the shaders use scalar block
// layout, so a vec4 member would be padded differently on the two sides.
struct Ds1HazeSet {
  vec3 fogColor;          // c103.rgb
  float fogStrength;      // c103.w: fog factor at full range
  vec3 extinction;        // c104.rgb per world unit
  float extinctionScale;  // c111.w * 2.0813689
  vec3 sunDirection;      // c111.xyz
  float fogStart;         // VS c128.x
  vec3 phase;             // c107.xyz
  float fogInvRange;      // VS c128.y
  vec3 rayleigh;          // c108.rgb
  float inscatterScale;   // c105.w
  vec3 mie;               // c109.rgb
  float scatterBlend;     // c110.w
  vec3 inverseExtinction; // c106.rgb
  float valid;            // 1 when the slot holds a set
  vec3 surfaceTint;       // c105.rgb
  float pad0;
  vec3 sunColor;          // c110.rgb
  float pad1;
};

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
  float displayScale;   // the game's exposure c135.x / c135.y, times rtx.ds1Haze.displayGain and the user brightness

  vec3 surfaceTint;     // c105.rgb
  // rtx.ds1Haze.minSurfaceShare: the least share of a surface's own colour the scattering leaves, whatever the set's constants say.
  float minSurfaceShare;

  vec3 sunColor;        // c110.rgb
  float pad1;

  // The per-draw sets by slot ([0] unused: untagged surfaces use the values above).
  Ds1HazeSet sets[DS1_HAZE_SET_COUNT];
};
