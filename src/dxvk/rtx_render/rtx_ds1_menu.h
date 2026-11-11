#pragma once

#include "../util/rc/util_rc_ptr.h"
#include "rtx_option.h"

namespace dxvk {
  class DxvkContext;

  // The Dark Souls tab of the developer menu: every DS1-specific runtime option in one place.
  void showDs1Menu(const Rc<DxvkContext>& ctx);

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
