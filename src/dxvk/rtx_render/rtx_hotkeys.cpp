// Global hotkeys, polled once per frame with GetAsyncKeyState so they work without opening the
// Remix menu. Keys only act while the game has the keyboard focus, so pressing one while typing elsewhere does
// nothing. "The game" is this process, or the process that launched it: under the 32-bit bridge the runtime runs
// inside NvRemixBridge.exe while the focused window belongs to the game that spawned it.
#include "rtx_hotkeys.h"
#include "rtx_option.h"
#include "rtx_dlss_neural_rendering.h"
#include "../../util/log/log.h"

#include <windows.h>
#include <tlhelp32.h>

namespace dxvk {
  struct HotkeyOptions {
    RTX_OPTION("rtx.hotkeys", int, toggleDlssNeuralRendering, 0x70,
               "Windows virtual-key code that toggles rtx.dlssNeuralRendering.enable while the game has the keyboard "
               "focus (0 = off). Default 0x70 = F1, the same key the Bloodborne runtime uses; set another code if the "
               "game itself binds F1.");
  };

  namespace {
    DWORD findParentProcessId() {
      const DWORD self = GetCurrentProcessId();
      DWORD parent = 0;
      const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
      if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
      }
      PROCESSENTRY32 entry {};
      entry.dwSize = sizeof(entry);
      for (BOOL ok = Process32First(snapshot, &entry); ok; ok = Process32Next(snapshot, &entry)) {
        if (entry.th32ProcessID == self) {
          parent = entry.th32ParentProcessID;
          break;
        }
      }
      CloseHandle(snapshot);
      return parent;
    }

    bool foregroundIsGame() {
      const HWND foreground = GetForegroundWindow();
      if (foreground == nullptr) {
        return false;
      }
      DWORD pid = 0;
      GetWindowThreadProcessId(foreground, &pid);
      if (pid == GetCurrentProcessId()) {
        return true;
      }
      // The launching process does not change for the life of this one, so look it up once.
      static const DWORD s_parentPid = findParentProcessId();
      return s_parentPid != 0 && pid == s_parentPid;
    }
  } // namespace

  void pollGlobalHotkeys() {
    static bool s_wasDown = false;
    const int key = HotkeyOptions::toggleDlssNeuralRendering();
    const bool down = key > 0 && key < 256 && (GetAsyncKeyState(key) & 0x8000) != 0;
    if (down && !s_wasDown && foregroundIsGame()) {
      const bool enable = !DlssNeuralRendering::enable();
      DlssNeuralRendering::enableObject().setDeferred(enable);
      Logger::info(str::format("[Hotkeys] DLSS 3D-Guided Neural Generation ", enable ? "on" : "off"));
    }
    s_wasDown = down;
  }
} // namespace dxvk
