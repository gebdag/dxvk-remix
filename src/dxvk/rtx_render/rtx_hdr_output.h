#pragma once

#include "../dxvk_include.h"
#include "rtx_option.h"

#include <atomic>
#include <algorithm>
#include <cmath>

namespace dxvk {

  // HDR10 output (experimental), gated by rtx.hdr.enable. With it off nothing below applies and the
  // output path is the stock one: an 8-bit UNORM back buffer, an sRGB swap chain, the final pass's
  // saturate and 8-bit dither.
  //
  // With it on:
  //  - the D3D9 back buffers are created as R16G16B16A16_SFLOAT behind the game's X8R8G8B8 request
  //    (D3D9CommonTexture), so values above 1 (brighter than SDR white) survive the blit from Remix's
  //    final output and the game's UI, drawn over them in gamma space as before, blends unchanged;
  //  - the swap chain asks for A2B10G10R10 + VK_COLOR_SPACE_HDR10_ST2084_EXT (D3D9SwapChainEx::PickFormats)
  //    and falls back to the sRGB formats where the surface has no HDR10 (Windows HDR off);
  //  - the present blit (DxvkSwapchainBlitter) decodes the gamma-encoded back buffer (2.2, or sRGB), maps
  //    1.0 to paperWhiteNits, rolls the luminance off towards peakNits above shoulderStart * peakNits,
  //    converts BT.709 -> BT.2020 and encodes ST 2084 (PQ);
  //  - the Remix menu (ImGui) encodes its colours the same way, at paper white;
  //  - the frame keeps its headroom: the DS1 look replaces the game's 8-bit scene clip and ColAdj clips
  //    with a per-channel roll-off from the knee towards the headroom (post_fx_dark_souls_look_scene /
  //    _composite, hdrRollOff in tonemapping.slangh), the stock tonemappers continue their SDR curve
  //    straight past the curve knee and roll off the same way instead of the per-channel saturate
  //    (tonemapping_apply_tonemapping, final_combine), and the final pass skips the saturate and the
  //    8-bit dither (srgb_dither).
  // Nothing in the SDR range changes: every pass that reads the frame for a decision (the DS1 meter,
  // the bloom bright pass) reads the clipped value, so bloom, exposure and DoF rates are those of the
  // SDR frame, and a value within 0..1 reaches the display exactly as before, at paper white.
  class DxvkHdrOutput {
  public:
    RTX_OPTION("rtx.hdr", bool, enable, false,
               "HDR10 output (experimental). The swap chain becomes 10-bit ST 2084 (PQ) BT.2020 when the display "
               "runs in Windows HDR, the back buffer 16-bit float, and the frame keeps what SDR clipped: values "
               "brighter than SDR white (the DS1 look's scene and ColAdj clips, the tonemapper's channel clip) go to "
               "the display up to the peak instead of being clipped. Off: the stock SDR output, untouched.");
    RTX_OPTION("rtx.hdr", float, paperWhiteNits, 200.0f,
               "Brightness of SDR white (a value of 1.0, the UI's white) in nits. The whole SDR range of the image "
               "is shown as it was, at this brightness.");
    RTX_OPTION("rtx.hdr", float, peakNits, 1000.0f,
               "The display's peak brightness in nits. Values above the shoulder roll off towards it (hue kept).");
    RTX_OPTION("rtx.hdr", float, shoulderStart, 1.0f,
               "A second, luminance roll-off at present, from this share of peakNits. 1 = none: the render passes "
               "already roll every channel off towards the peak, so the present only clips what exceeds it.");
    RTX_OPTION("rtx.hdr", float, knee, 0.85f,
               "The SDR output level (gamma, 0..1) from which the roll-off towards the peak begins. At or below it the "
               "image is the SDR image; from it the range SDR clipped is spread over the headroom. Lower starts the "
               "roll-off earlier (more of the highlight range keeps gradation); 1 starts it exactly at white.");
    RTX_OPTION("rtx.hdr", float, rollOffPerChannel, 0.56f,
               "How the roll-off treats colour, 0..1. 1 = each channel on its own: the brightest channel compresses "
               "first, so a bright saturated colour turns towards white as it brightens, as it did at the SDR clip, "
               "only higher up. 0 = all three by the brightest channel: the hue holds all the way to the peak.");
    RTX_OPTION("rtx.hdr", bool, srgbDecode, false,
               "Decode the gamma-encoded frame and UI with the sRGB piecewise curve instead of a 2.2 power. An SDR "
               "display shows the 8-bit frame through about a 2.2 power, so 2.2 keeps blacks as they looked; the "
               "piecewise curve lifts them slightly (Windows' own SDR-in-HDR choice).");
    RTX_OPTION("rtx.hdr", bool, ds1Headroom, true,
               "With the game's image pipeline: keep what its 8-bit scene clip and ColAdj clips would cut above "
               "white, so the sky, fire and lit highlights go above SDR white. Off: the look's output clips at 1 as "
               "in SDR (the HDR display then only gets the 10-bit encode).");
    RTX_OPTION("rtx.hdr", bool, remixCurveContinue, true,
               "With Remix's own tonemappers: continue the tone curve straight past its knee instead of letting it fit "
               "everything under SDR white. Highlights above the knee get brighter than in SDR, up to the display's "
               "peak; below the knee the image is the SDR image. Off: the tonemapped image has nothing above white "
               "and only the roll-off runs.");
    RTX_OPTION("rtx.hdr", float, remixCurveKnee, 0.5f,
               "The SDR output level (0..1, display linear) at which the tone curve stops following its SDR shape and "
               "continues straight. Lower: more of the highlights are brightened; higher: closer to the SDR image.");
    RTX_OPTION_FLAG("rtx.hdr", bool, dumpFrame, false, RtxOptionFlags::NoSave,
                    "Write the next frame's final output (before the game's UI) as a 16-bit float DDS to the "
                    "Screenshots folder (hdrFinal_*.dds), then clear. Gamma-encoded, 1.0 = paper white.");

    // The back buffers hold 16-bit float (set by the swap chain when it creates them).
    static bool floatBackBuffers() { return s_floatBackBuffers.load(); }
    static void setFloatBackBuffers(bool value) { s_floatBackBuffers.store(value); }

    // The swap chain presents in HDR10 (set by the swap chain after each (re)creation).
    static bool swapchainHdr() { return s_swapchainHdr.load(); }
    static VkColorSpaceKHR swapchainColorSpace() { return s_colorSpace.load(); }
    static VkFormat swapchainFormat() { return s_swapchainFormat.load(); }
    static void setSwapchain(VkFormat format, VkColorSpaceKHR colorSpace) {
      s_swapchainFormat.store(format);
      s_colorSpace.store(colorSpace);
      s_swapchainHdr.store(colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT);
    }

    // True while the frame is rendered with headroom: values above 1 reach a float back buffer and an
    // HDR10 swap chain. False for one frame after the toggle, while those are being recreated.
    // The swap chain and back buffer state is written on the app thread; the render passes run on the CS
    // thread and all read one snapshot taken at the start of the frame (beginFrame, RtxContext::injectRTX),
    // so every pass of a frame agrees.
    static void beginFrame() { s_frameHeadroom = enable() && s_floatBackBuffers.load() && s_swapchainHdr.load(); }
    static bool renderHeadroom() { return s_frameHeadroom; }

    // Headroom above SDR white the display has, as a factor on 1.0 (peak / paper white).
    static float headroom() { return std::max(peakNits(), 1.0f) / std::max(paperWhiteNits(), 1.0f); }

    // The roll-off knee (rtx.hdr.knee) as a gamma value and as display linear.
    static float kneeGamma() { return std::clamp(knee(), 0.3f, 1.0f); }
    static float kneeLinear() { return std::pow(kneeGamma(), 2.2f); }

    // The Remix tone curve's continuation knee (display linear), or 2 for none.
    static float curveKnee() { return remixCurveContinue() ? std::clamp(remixCurveKnee(), 0.1f, 0.95f) : 2.0f; }

    // Decode exponent of the gamma-encoded frame: 2.2, or 0 for the sRGB piecewise curve.
    static float decodeGamma() { return srgbDecode() ? 0.0f : 2.2f; }

    // Takes the dump request, once per setting of rtx.hdr.dumpFrame (the option is cleared deferred, so the
    // frame after the dump may still read it as set).
    static bool consumeDump() {
      if (!dumpFrame()) {
        s_dumpTaken = false;
        return false;
      }
      if (s_dumpTaken) {
        return false;
      }
      s_dumpTaken = true;
      s_dumpSwapchain = true;
      s_dumpBackBuffer = true;
      dumpFrameObject().setDeferred(false);
      return true;
    }

    // After a frame dump, the swap chain image of the same frame (what the display gets: PQ-encoded with HDR10).
    static bool takeSwapchainDump() {
      const bool take = s_dumpSwapchain;
      s_dumpSwapchain = false;
      return take;
    }

    // ... and the back buffer as presented (the frame with the game's UI, float with HDR10).
    static bool takeBackBufferDump() {
      const bool take = s_dumpBackBuffer;
      s_dumpBackBuffer = false;
      return take;
    }

    static void showImguiSettings();

  private:
    inline static std::atomic<bool> s_floatBackBuffers = false;
    inline static std::atomic<bool> s_swapchainHdr = false;
    inline static bool s_frameHeadroom = false;   // CS thread only
    inline static bool s_dumpTaken = false;       // CS thread only
    inline static bool s_dumpSwapchain = false;   // CS thread only
    inline static bool s_dumpBackBuffer = false;  // CS thread only
    inline static std::atomic<VkColorSpaceKHR> s_colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    inline static std::atomic<VkFormat> s_swapchainFormat = VK_FORMAT_UNDEFINED;
  };

} // namespace dxvk
