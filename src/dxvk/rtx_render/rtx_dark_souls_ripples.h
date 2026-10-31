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
  // Moving, their legs raise a bow ahead and a trough behind that grow with their speed (U^2 / 2g, the
  // stagnation head) and vanish when they stop; the change radiates as waves, standing still makes none.
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
    RTX_OPTION("rtx.ds1Ripple", int, gridSize, 512, "Cells along each side of the ripple window (a multiple of 16).");
    RTX_OPTION("rtx.ds1Ripple", float, cellSize, 0.015f,
               "Size of a ripple cell in metres. The shortest ripple that travels is about 8 cells long.");
    RTX_OPTION("rtx.ds1Ripple", float, stepRate, 60.0f, "Ripple simulation steps per second (fixed, independent of the frame rate).");
    RTX_OPTION("rtx.ds1Ripple", int, maxStepsPerFrame, 3, "At most this many steps per frame; time beyond is dropped, never caught up.");
    RTX_OPTION("rtx.ds1Ripple", float, gravityScale, 1.0f,
               "Scale on gravity: 1 gives real water's wave speeds (about 0.5 m/s at 18 cm wavelength).");
    RTX_OPTION("rtx.ds1Ripple", float, damping, 0.3f, "Damping on every wavelength, per second (the paper's alpha).");
    RTX_OPTION("rtx.ds1Ripple", float, viscosity, 3.0e-4f,
               "Viscous damping 2 nu k^2 in m^2/s: short ripples die fast, long ones travel. Clean water is 1e-6; "
               "this stands in for the surface film and the spreading the grid cannot resolve.");
    RTX_OPTION("rtx.ds1Ripple", float, legRadius, 0.06f, "Radius of the wading legs in metres: the bow sits this far ahead.");
    RTX_OPTION("rtx.ds1Ripple", float, sourceRadius, 0.04f, "Width (Gaussian sigma) of the bow and trough, in metres.");
    RTX_OPTION("rtx.ds1Ripple", float, bowEfficiency, 0.1f,
               "Share of the stagnation head U^2 / 2g the legs raise as waves (0.1: about 1 cm at walking pace).");
    RTX_OPTION("rtx.ds1Ripple", float, maxBowHeight, 0.03f, "Highest bow in metres, however fast the player moves.");
    RTX_OPTION("rtx.ds1Ripple", float, normalScale, 1.0f, "Scale on the ripple slope added to the water normal (1 = the real slope).");
    RTX_OPTION("rtx.ds1Ripple", float, borderFade, 0.75f, "Metres over which ripples fade towards the window's border.");
  };

} // namespace dxvk
