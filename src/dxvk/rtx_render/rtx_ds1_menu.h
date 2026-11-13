#pragma once

#include "../util/rc/util_rc_ptr.h"
#include "rtx_option.h"

#include <atomic>

namespace dxvk {
  class DxvkContext;

  // DS1: the size of the game's scene targets while the proxy renders at another size than it presents ([Resolution]
  // RenderWidth/Height apart from PresentWidth/Height, e.g. 3840x2160 into a 1920x1080 back buffer). Stock Remix path traces
  // only draws aimed at a target the back buffer's size, so every scene draw fell back to raster and the view was black.
  // A target of this size counts as well. The proxy sets it while the two sizes differ; 0 (its default) is the stock rule.
  class DxvkDs1SceneTarget {
  public:
    RTX_OPTION_FLAG("rtx.ds1SceneTarget", int, width, 0, RtxOptionFlags::NoSave,
                    "Set by the proxy: the width of the game's scene targets when it renders at another size than the back buffer. 0 = the back buffer's.");
    RTX_OPTION_FLAG("rtx.ds1SceneTarget", int, height, 0, RtxOptionFlags::NoSave,
                    "Set by the proxy: the height of the game's scene targets when it renders at another size than the back buffer. 0 = the back buffer's.");
  };

  // DS1 debug: which draws reach the path tracer. A draw that still runs the game's vertex shader (the proxy passed it through:
  // particles, effects, anything it does not convert) is path traced through vertex capture (rtx.useVertexCapture); the
  // proxy's converted world draws are fixed function. Only Shader Draws drops the fixed-function ones from the path tracer,
  // so what is left in the view came over the shader pipe; capture off is the opposite (shader draws dropped, the UI too).
  class DxvkDs1DrawFilter {
  public:
    RTX_OPTION_FLAG("rtx.ds1Debug", bool, onlyShaderDraws, false, RtxOptionFlags::NoSave,
                    "Debug: only draws that run the game's vertex shader are path traced; the proxy's converted (fixed-function) draws are dropped.");
    RTX_OPTION_FLAG("rtx.ds1Debug", int, logShaderDraws, 0, RtxOptionFlags::NoSave,
                    "Debug: a new value logs the next frame's path traced shader draws ([DS1 shader draw]).");
    // Cost bisection for the replacement (enhanced) materials: which draws keep them, and which of their maps.
    RTX_OPTION_FLAG("rtx.ds1Debug", int, replacementDrawFilter, 0, RtxOptionFlags::NoSave,
                    "Debug: draws that lose their replacement material. 0 none, 1 alpha-tested, 2 blended, 3 opaque (neither), 4 all but alpha-tested.");
    RTX_OPTION_FLAG("rtx.ds1Debug", int, replacementMapMask, 0, RtxOptionFlags::NoSave,
                    "Debug: replacement maps dropped from every replacement material (bits). 1 albedo (the game's own colour texture instead), "
                    "2 normal, 4 height, 8 roughness, 16 metallic, 32 emissive, 64 subsurface, 128 tangent/secondary/specular colour. "
                    "Constants from the game's own material, textures kept: 256 all, 512 roughness/metallic/albedo/opacity, 1024 subsurface, "
                    "2048 displacement, 4096 sampler state, 8192 the rest (anisotropy, emission, legacy flag, thin film, alpha state); "
                    "16384 roughness map and constant, 32768 metallic map and constant.");
    // Path traced draws of the last frame, by kind (written by the API thread at the end of a frame).
    inline static std::atomic<uint32_t> shaderDrawsLastFrame { 0 };
    inline static std::atomic<uint32_t> fixedDrawsLastFrame { 0 };
    inline static std::atomic<uint32_t> shaderPrimsLastFrame { 0 };
    inline static std::atomic<uint32_t> fixedPrimsLastFrame { 0 };
  };

  // Opacity micromaps on merged BLAS buckets that are rebuilt every frame (BlasBucketKey::isVolatile, rtx_accel_manager.cpp).
  class DxvkDs1OmmOptions {
  public:
    RTX_OPTION("rtx.ds1Omm", bool, skipVolatileBuckets, true,
               "Leaves opacity micromaps off the merged BLAS buckets rebuilt every frame (instances that changed in the last few "
               "seconds): their build with micromaps costs more than the micromaps save. Still buckets keep theirs.");
  };

  // The Dark Souls tab of the developer menu: every DS1-specific runtime option in one place.
  void showDs1Menu(const Rc<DxvkContext>& ctx);

  // Dev mode for the runtime's DS1 tools: the Alt+X Dark Souls tab's authoring and debug items, the Texture Lab, and the hitch
  // lines in the log. Off for players; a development machine sets it in its own user.conf (it is never shown in a menu, and as a
  // user setting it is never saved into the shipped rtx.conf). The proxy has its own switch ([Dev] Enabled in its INI).
  class DxvkDs1Dev {
  public:
    RTX_OPTION_FLAG("rtx.ds1Dev", bool, enable, false, RtxOptionFlags::UserSetting,
                    "Dev mode: shows the DS1 authoring and debug tools in the Dark Souls tab and the Texture Lab, and logs hitches.");
    RTX_OPTION_FLAG("rtx.ds1Dev", int, captureRequest, 0, RtxOptionFlags::NoSave,
                    "Dev mode: a new value starts a USD scene capture, as the capture hotkey does (for scripts, which send the game no keys). "
                    "Replacement assets must be off, as for any capture.");
  };

  // The Dark Souls tab's Advanced checkbox: off (the default), the tab shows a player's switches only (the look's on/off and
  // image adjustments, motion blur, HDR10, effects, ripples, the wet player, characters, the HUD); on, every tuning slider,
  // readout and section. A user setting: saved to the player's user.conf.
  class DxvkDs1MenuView {
  public:
    RTX_OPTION_FLAG("rtx.ds1Menu", bool, advanced, false, RtxOptionFlags::UserSetting,
                    "Shows every Dark Souls setting (tuning, readouts) in the Dark Souls tab; off, a player's switches only.");
  };

  class DxvkDs1SettingsCheck {
  public:
    // A diagnostic: each new value writes the changes Save would write, for user.conf and rtx.conf, to
    // rtx_comp/unsaved_<layer>.conf (or logs that there are none). Opening the game and saving should write nothing.
    RTX_OPTION_FLAG("rtx.ds1Debug", int, logUnsavedSettings, 0, RtxOptionFlags::NoSave,
                    "A new value writes the changes the menu's Save would write to rtx_comp/unsaved_user.conf and unsaved_rtx.conf.");
    // Once a frame.
    static void update();
  };
}
