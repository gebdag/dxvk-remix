#pragma once

#include "../util/rc/util_rc_ptr.h"
#include "rtx_option.h"

namespace dxvk {
  class DxvkContext;

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
