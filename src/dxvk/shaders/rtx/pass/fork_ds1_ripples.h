/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#pragma once

// Fork (DS1): ripples where characters wade through water (rtx_dark_souls_ripples.h).
//
// An iWave height field (Tessendorf, "Simulating Ocean Water", 2004 course notes, section 5) on a square
// window of world cells around the player, stored toroidally: cell (x, z) lives in texel (x mod N, z mod N),
// so the window follows the player by whole cells without moving any data. Three passes per frame:
//   ds1_ripple_map    each cell's water height (a vertical ray to the first DS1 water surface), cells that
//                     just entered the window cleared, and in the collision window around the player which
//                     cells a character's body fills at the waterline;
//   ds1_ripple_step   one iWave step: h' = (h (2 - a dt) - h_prev - G (K * h)) / (1 + a dt) + viscous damping
//                     + source, cells without water or inside a body held at 0 (they reflect the waves like
//                     the paper's obstructions). The source is the bodies' movement: water rises around
//                     cells a body has just filled and sinks around cells it has just left;
//   ds1_ripple_slope  the slope of the field between its last two steps, for the water material.
// Threads index window cells: thread (i, j) is cell origin + (i, j).
//
// Scales are real water's in metres (DS1 world units are metres): G makes waves of about 12 cells run at the
// gravity wave speed sqrt(g / k) (within 10% from 8 to 16 cells, slower outside: the kernel's response is
// not linear in k there); damping grows with k^2 like viscous damping, 2 nu k^2, so the grid's shortest
// waves, which the kernel cannot move, die within a few steps instead of standing in place.

#include "rtx/utility/shader_types.h"

// The common ray tracing indices for the TLAS and the surfaces (BINDING_ACCELERATION_STRUCTURE,
// BINDING_SURFACE_DATA_BUFFER), so they are what every later pass binds there anyway.
#define DS1_RIPPLE_BINDING_TLAS          0
#define DS1_RIPPLE_BINDING_SURFACES      4
#define DS1_RIPPLE_BINDING_HEIGHT        21
#define DS1_RIPPLE_BINDING_HEIGHT_PREV   22
#define DS1_RIPPLE_BINDING_WATER         23
#define DS1_RIPPLE_BINDING_OCCUPANCY     24
#define DS1_RIPPLE_BINDING_SLOPE         25
#define DS1_RIPPLE_BINDING_HEIGHT_NEXT   26
// The bodies at the waterline as of the last step (DS1_RIPPLE_BINDING_OCCUPANCY: as of this frame).
#define DS1_RIPPLE_BINDING_OCCUPANCY_PREV 27
// The wet player: the body's wetness grid, the water level at the player, the body's vertices.
#define DS1_WET_BINDING_GRID             28
#define DS1_WET_BINDING_LEVEL            29
#define DS1_WET_BINDING_POSITIONS        30
#define DS1_WET_BINDING_COLOURS          31
#define DS1_WET_BINDING_BODY_TOP         32
// Cells of the topmost body surface a side, over twice the splash radius (ds1_splash_top).
#define DS1_SPLASH_TOP_SIZE              40

// The grid is in the body's own space, so what got wet stays wet whatever the pose does. The proxy puts a vertex's
// bind position in its colour: r = (x + 1) / 2, g = (y + 0.2) / 2.4, b = (z + 0.6) / 1.2 (metres, feet at y = 0),
// which is also its place in the grid. Slices of constant z side by side in one 2D texture, 4 x 4 of them.
#define DS1_WET_CELLS_X                  32
#define DS1_WET_CELLS_Y                  48
#define DS1_WET_CELLS_Z                  16
#define DS1_WET_SLICES_PER_ROW           4
#define DS1_WET_GROUP_SIZE               16

#define DS1_RIPPLE_GROUP_SIZE            16
#define DS1_RIPPLE_KERNEL_RADIUS         6

// A surface record is 16 uvec4; textureFlags is data13.z (RtSurface::writeGPUData).
#define DS1_RIPPLE_SURFACE_UVEC4S        16
#define DS1_RIPPLE_WATER_FLAG            (1u << 26)
#define DS1_RIPPLE_SKINNED_FLAG          (1u << 29)
// Surface::isDs1Swamp: a liquid too where rtx.ds1Swamp.wade says so.
#define DS1_RIPPLE_SWAMP_FLAG            (1u << 19)
// Surface::hasDs1SelfLit: on an opaque surface that is not a character, the game's lava.
#define DS1_RIPPLE_SELF_LIT_FLAG         (1u << 30)

// Water height of a cell without water.
#define DS1_RIPPLE_NO_WATER              (-1.0e9f)
// The slope texture's height (relative to Ds1RippleArgs::referenceHeight) where there is no water.
#define DS1_RIPPLE_NO_WATER_RELATIVE     (30000.0f)

// The paper's vertical derivative kernel, G(r) = sum_k k^2 exp(-k^2) J0(r k) / sum_k k^2 exp(-k^2) over a
// 13x13 window (its iwave_paint code), as one quadrant: G(i, j) = kDs1RippleKernel[|i|][|j|]. Truncated to
// 13x13 the paper's kernel sums to 2.10 instead of 0, which makes still water spring back to level 0 and
// raised water bob; that sum is taken out along a Gaussian of 2 cells, which leaves the short waves alone.
// A 5-point Laplacian of 0.03 lifts the response at the grid's finest scale above 0 (the paper's dips to
// -0.09 there, which an explicit step amplifies). The response is then 0.24 at a 64-cell wavelength, 3.0
// at 16, 4.39 at 12 (kDs1RippleKernelResponse12), 5.63 at most (kDs1RippleKernelResponseMax, at 8).
static const float kDs1RippleKernel[7][7] = {
  {  1.0363555f,  0.5744606f,  0.1056878f, -0.0924828f, -0.0757679f, -0.0351083f, -0.0158860f },
  {  0.5744606f,  0.3794225f,  0.0274859f, -0.0977394f, -0.0699547f, -0.0323161f, -0.0149791f },
  {  0.1056878f,  0.0274859f, -0.0807099f, -0.0924084f, -0.0541285f, -0.0255060f, -0.0127082f },
  { -0.0924828f, -0.0977394f, -0.0924084f, -0.0643531f, -0.0351083f, -0.0179827f, -0.0100059f },
  { -0.0757679f, -0.0699547f, -0.0541285f, -0.0351083f, -0.0205387f, -0.0120753f, -0.0075815f },
  { -0.0351083f, -0.0323161f, -0.0255060f, -0.0179827f, -0.0120753f, -0.0081605f, -0.0056817f },
  { -0.0158860f, -0.0149791f, -0.0127082f, -0.0100059f, -0.0075815f, -0.0056817f, -0.0042725f },
};
static const float kDs1RippleKernelResponse12 = 4.388f;
static const float kDs1RippleKernelResponseMax = 5.629f;

struct Ds1RippleMapArgs {
  // The window's first cell (world x, z in cells) this frame and last frame.
  ivec2 origin;
  ivec2 previousOrigin;

  uint gridSize;
  float cellSize;
  // 1: clear every cell (new field, or the window jumped).
  uint resetAll;
  // The vertical water rays run from rayTop down rayLength.
  float rayTop;

  float rayLength;
  // A cell is inside a body when rays along +-x and +-z, bodyReach long, all hit a character (a skinned
  // surface) at the waterline: a leg is narrower than that, the gap between two legs is open sideways.
  float bodyReach;
  // The bodies are looked for in the window cells collisionMin .. collisionMin + collisionSize - 1.
  uint collisionSize;
  // 1: a swamp surface counts as water (rtx.ds1Swamp.wade).
  uint swampIsLiquid;
  ivec2 collisionMin;
  // Test: a body of this radius at this world x, z, 0 for none.
  vec2 testBody;
  float testBodyRadius;
  uint pad1;
  uint pad2;
  uint pad3;
};

struct Ds1RippleStepArgs {
  ivec2 origin;
  uint gridSize;
  float cellSize;

  // The paper's `gravity` (g dt^2 in cell units, scaled to the kernel's response).
  float gravity;
  // alpha * dt.
  float damping;
  // 2 nu dt / cellSize^2: the step's viscous damping on the 5-point Laplacian of the cell's velocity.
  float viscosity;
  // Cells over which the field is damped to 0 at the window's border, so waves leave instead of
  // bouncing back.
  float borderCells;

  // The window cells collisionMin .. collisionMin + collisionSize - 1 hold the bodies.
  ivec2 collisionMin;
  uint collisionSize;
  // Height of the water a body pushes aside per cell it fills, over the frame's steps (metres).
  float displacement;

  // Above 0, a thick liquid such as mud: this damping per step falls on the surface's velocity alone, so what a body pushed up stays
  // up, and heights go only by thickSettle, a share per step. The water's damping above also takes the height itself.
  float thickDamping;
  float thickSettle;
  // Share of the height difference to the neighbours evened out each step.
  float smoothing;
  uint pad0;
};

struct Ds1RippleSlopeArgs {
  ivec2 origin;
  uint gridSize;
  float cellSize;

  // Where the frame falls between the last two steps (0 = the older one).
  float alpha;
  // Ds1RippleArgs::referenceHeight.
  float referenceHeight;
  float pad0;
  float pad1;
};

// The water material's view of the field (RaytraceArgs::ds1Ripple).
// Fork (DS1): the wet player. The water level at the feet, found once a frame (ds1_wet_level).
struct Ds1WetLevelArgs {
  ivec2 origin;
  uint gridSize;
  // The water is looked for at the player's cell and this many cells to each side.
  int reachCells;

  float feet;
  // Water deeper than this over the feet is another body of water, up above.
  float maxDepth;
  float cellSize;
  // 1: a swamp surface counts as water, and what the player stands in is told apart (rtx.ds1Swamp.wade).
  uint swampIsLiquid;
};

// What falling water meets first (ds1_splash_top).
struct Ds1SplashTopArgs {
  vec2 centre;          // x and z
  float radius;
  float top;            // the rays start here, above the head

  float bottom;
  float pad0;
  float pad1;
  float pad2;
};

// The whole body dries a little each frame (ds1_wet_decay).
struct Ds1WetDecayArgs {
  float decay;
  // 1: clear the grid.
  uint reset;
  // 1: wet all over (under a waterfall).
  uint soak;
  // The mud in the grid's second channel goes at its own rate.
  float mudDecay;

  // And the lava in its third.
  float lavaDecay;
  float pad0;
  float pad1;
  float pad2;
};

// One body draw's vertices under the water wet their place in the grid (ds1_wet_splat).
struct Ds1WetSplatArgs {
  vec4 objectToWorld[3];

  ivec2 origin;
  uint gridSize;
  float cellSize;

  // Where a vertex's position and colour start in their buffers, and the distance between vertices, in 32-bit words.
  uint positionWord;
  uint positionStride;
  uint colourWord;
  uint colourStride;

  uint vertexCount;
  // A vertex this far above the water touches it: the game's shallow water lies at or under the ground.
  float contactMargin;
  // Only vertices this close to the player, sideways, count: other characters wear the same parts.
  float radiusSquared;
  float testLevel;

  vec2 playerXZ;
  // 1: testLevel is the water everywhere, for tests.
  uint useTestLevel;
  // 1: that test water is mud. 2: it is lava.
  uint testMud;

  // A vertex this far above lava burns: the game's lava is a floor, the body stands on it.
  float lavaContact;
  float pad0;
  float pad1;
  float pad2;
};

// The material's view of it.
struct Ds1WetArgs {
  vec2 playerXZ;
  float radiusSquared;
  // Roughness of a wet surface: at most this.
  float roughness;

  // Scale on the base reflectivity in display gamma.
  float specularScale;
  // A fully porous surface's albedo scale.
  float porousFactor;
  uint textureIndex;
  uint samplerIndex;

  uint enable;
  // 0 or above: this wetness on the whole body, for tests.
  float forceWetness;
  // Size of the beads of water on wet surfaces, 1 = about a centimetre across; 0: none.
  float dropletScale;
  // Size of the drops that run down, 1 = about a centimetre across; 0: none.
  float runnerScale;

  // Soaked in mud: its colour, stored (gamma) value.
  vec3 mudColour;
  // 0 or above: this much mud on the whole body, for tests.
  float forceMud;

  // The share of mud's run that it takes to harden, and the last share, over which it fades away.
  float mudHarden;
  float mudFade;
  // 0 or above: this much lava on the whole body, for tests.
  float forceLava;
  // The share of lava's run that it stays molten, and the last share, over which the char fades away.
  float lavaMolten;

  float lavaFade;
  // Molten at its hottest, radiance.
  float lavaGlow;
  float pad0;
  float pad1;
};

struct Ds1RippleArgs {
  // World x, z of the window's first corner, and its width (gridSize * cellSize).
  vec2 windowMin;
  float windowSize;
  float invWindowSize;

  // Metres over which ripples fade out towards the window's border.
  float borderFade;
  float normalScale;
  // The slope texture stores water heights relative to this.
  float referenceHeight;
  // A water hit takes ripples only within this distance of its cell's water height (other water bodies
  // above or below pass through the same cells).
  float heightTolerance;

  // Bindless indices of the slope texture (xy = dh/dx, dh/dz; z = water height - referenceHeight) and of
  // its linear, repeating sampler.
  uint textureIndex;
  uint samplerIndex;
  uint enable;
  // Water refraction by depth, always valid: straight through down to refractionFlatDepth metres, full by
  // refractionFullDepth (0: full everywhere). See geometryResolverDs1WaterRefraction.
  float refractionFlatDepth;

  float refractionFullDepth;
  // The game's refraction smear shifts the view in full from this depth in metres, less in shallower water.
  float smearFullDepth;
  // How far under DS1 water, in metres, a view continues from at the shore at the least (geometryResolverDs1ShoreOrigin).
  float shoreMinDepth;
  // The ripples' parallax (ds1_ripple_parallax.slangh): scale on the heights the view ray is walked against, 0 = none (the
  // field only tilts the normal).
  float parallaxScale;

  // The walk goes through the heights from +parallaxBound to -parallaxBound metres around the flat surface.
  float parallaxBound;
  // Size of a cell of the field in metres.
  float cellSize;
  // Steps of the walk: at the least, and at the most (a ray near the surface crosses many cells).
  uint parallaxSteps;
  uint parallaxMaxSteps;
};
