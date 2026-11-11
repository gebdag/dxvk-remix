#pragma once

#include "rtx_option.h"

namespace dxvk {

  // Dark Souls 1's motion blur (its PC option "Motion Blur") in Remix's motion blur pass, the same at every frame rate.
  //
  // The game blurs camera and object motion by the previous rendered frame's displacement: offset 0.4 of it, a centre
  // sample plus 7 taps with weights fading from 7/36 to 1/36 of 0.7, so the blur spans about 0.34 of a 30 fps frame
  // (the spread of a box blur of that width), an 11.4 ms shutter. It reads no frame time, so above 30 fps its streaks
  // shorten, and Remix's exposureFraction (a fraction of the frame interval) does the same. Here the shutter is a time:
  // each frame the exposure fraction is shutterMs over the measured frame time, so a streak covers the same span of
  // motion at 30, 60 or 144 fps.
  //
  // A stopgap, not the game's kernel. Remix's pass is not a box of that width: with its noise sampling each of the four steps
  // on either side goes a random part of the way, so at an exposure of 0.34 the blur's spread is 0.059 of the motion where the
  // game's is 0.098 (1.67 to 1), and its reach about +-0.085 of the motion on average where the game trails 0.35 to one side.
  // The shutter is therefore 19 ms (11.4 x 1.67), which matches the spread. Remix also blurs nothing that moves less than a
  // pixel per side (under 6 px a frame at 30 fps), which the game's camera blur does not do: minimumVelocityPx takes that down.
  // Still missing: trails outside a moving shape (the pass goes by the centre pixel's own motion) and the game's one-sided
  // kernel with its sharp centre.
  class DxvkDarkSoulsMotionBlur {
  public:
    RTX_OPTION("rtx.ds1MotionBlur", bool, enable, true,
               "Drive Remix's motion blur with Dark Souls 1's values and a shutter time instead of a fraction of the "
               "frame, so the blur matches the game's at 30 fps at any frame rate. Off: rtx.postfx.exposureFraction, "
               "blurDiameterFraction and motionBlurDynamicDeduction apply as set.");
    RTX_OPTION("rtx.ds1MotionBlur", float, shutterMs, 19.0f,
               "Time the blur covers, in milliseconds. 19 gives Remix's noise-sampled blur the spread of the game's at its "
               "30 fps (11.4 would, were the blur a plain box of that width).");
    RTX_OPTION("rtx.ds1MotionBlur", float, minimumVelocityPx, 0.25f,
               "Motion under this many pixels per side is not blurred (Remix's own threshold is 1). The game's camera blur "
               "has no threshold.");
    RTX_OPTION("rtx.ds1MotionBlur", float, blurDiameterFraction, 0.1f,
               "Longest blur on each side of a pixel, as a fraction of the screen. The game never shortens camera "
               "motion and lets objects reach about 0.35 of their per-frame motion, so this is well above Remix's 0.02.");
    RTX_OPTION("rtx.ds1MotionBlur", float, maxExposureFraction, 3.0f,
               "Upper limit of the exposure fraction, for very short frames (Remix's own slider stops at 3).");
    RTX_OPTION("rtx.ds1MotionBlur", float, frameTimeSmoothing, 0.1f,
               "Weight of each new frame time in the running average the exposure divides by (1 = no smoothing).");

    // Advances the running frame time by this frame's wall-clock interval. Once per frame.
    static void update(float realFrameMs);
    // shutterMs over the smoothed frame time, limited to maxExposureFraction.
    static float exposureFraction();
    static void showImguiSettings();

  private:
    static float s_frameMs;
  };

} // namespace dxvk
