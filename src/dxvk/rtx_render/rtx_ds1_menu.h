#pragma once

#include "../util/rc/util_rc_ptr.h"

namespace dxvk {
  class DxvkContext;

  // The Dark Souls tab of the developer menu: every DS1-specific runtime option in one place.
  void showDs1Menu(const Rc<DxvkContext>& ctx);
}
