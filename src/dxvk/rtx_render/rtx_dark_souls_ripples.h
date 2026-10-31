#pragma once

#include "rtx_option.h"
#include "rtx/pass/fork_ds1_ripples.h"

namespace dxvk {

  class RtxContext;

  // Dark Souls 1 water ripples around the wading player (prototype): an iWave height field on a window
  // of world cells that follows the player, stepped at a fixed rate on the GPU, whose slope the DS1 water
  // material adds to the game's wave layers (fork_ds1_ripples.h has the passes, the layout and the kernel).
  //
  // The source is the player: rtx.ds1Lights.playerAnchor (the head, sent by the proxy every frame) gives
  // the position; a probe ray under it decides whether they wade (water above the first solid ground).
  // Moving, they push a Gaussian of water along, which radiates as waves; standing still makes none.
  class DxvkDarkSoulsRipples {
  public:
    // Before the frame's bindless tables are built: creates the field, plans the frame's steps and
    // registers the slope texture and its sampler for the material.
    static void prepare(RtxContext* ctx);

    // The material's view of the field (zeroed when it is off or not prepared this frame).
    static void fillShaderArgs(Ds1RippleArgs& args);

    // After the frame's TLAS build, before the ray tracing passes: water map, steps, slope.
    static void dispatch(RtxContext* ctx);

    RTX_OPTION("rtx.ds1Ripple", bool, enable, true, "Ripples on DS1 water around the wading player.");
    RTX_OPTION("rtx.ds1Ripple", int, gridSize, 256, "Cells along each side of the ripple window (a multiple of 16).");
    RTX_OPTION("rtx.ds1Ripple", float, cellSize, 0.04f, "Size of a ripple cell in metres.");
    RTX_OPTION("rtx.ds1Ripple", float, stepRate, 60.0f, "Ripple simulation steps per second (fixed, independent of the frame rate).");
    RTX_OPTION("rtx.ds1Ripple", int, maxStepsPerFrame, 3, "At most this many steps per frame; time beyond is dropped, never caught up.");
    RTX_OPTION("rtx.ds1Ripple", float, waveSpeed, 0.6f, "Speed of a 10-cell ripple wavelength in metres per second (real water: about 0.6 at 40 cm).");
    RTX_OPTION("rtx.ds1Ripple", float, damping, 1.0f, "How fast ripples die out, per second (the paper's alpha).");
    RTX_OPTION("rtx.ds1Ripple", float, sourceHeight, 0.02f, "Height of the water the wading player pushes ahead, in metres.");
    RTX_OPTION("rtx.ds1Ripple", float, sourceRadius, 0.12f, "Radius (Gaussian sigma) of the pushed water, in metres.");
    RTX_OPTION("rtx.ds1Ripple", float, normalScale, 1.0f, "Scale on the ripple slope added to the water normal.");
    RTX_OPTION("rtx.ds1Ripple", float, borderFade, 1.0f, "Metres over which ripples fade towards the window's border.");
  };

} // namespace dxvk
