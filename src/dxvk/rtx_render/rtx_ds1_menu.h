#pragma once

#include "../util/rc/util_rc_ptr.h"
#include "rtx_option.h"

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

  // The Dark Souls tab of the developer menu: every DS1-specific runtime option in one place.
  void showDs1Menu(const Rc<DxvkContext>& ctx);

  // Dev mode for the runtime's DS1 tools: the Alt+X Dark Souls tab's authoring and debug items, the Texture Lab, and the hitch
  // lines in the log. Off for players; a development machine sets it in its own user.conf (it is never shown in a menu, and as a
  // user setting it is never saved into the shipped rtx.conf). The proxy has its own switch ([Dev] Enabled in its INI).
  class DxvkDs1Dev {
  public:
    RTX_OPTION_FLAG("rtx.ds1Dev", bool, enable, false, RtxOptionFlags::UserSetting,
                    "Dev mode: shows the DS1 authoring and debug tools in the Dark Souls tab and the Texture Lab, and logs hitches.");
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
