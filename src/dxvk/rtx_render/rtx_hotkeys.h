#pragma once

namespace dxvk {
  // Global hotkeys (rtx.hotkeys.*), polled once per frame; they act only while the game has the focus.
  void pollGlobalHotkeys();
}
