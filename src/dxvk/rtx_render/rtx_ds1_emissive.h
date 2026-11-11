#pragma once

#include "../../util/rc/util_rc_ptr.h"
#include "../../util/xxHash/xxhash.h"

namespace dxvk {

  class DxvkContext;

  // Fork (DS1): how strongly one game texture glows, set by eye. A scale on the emission of every draw whose material is that texture's
  // (by its hash, as the Remix menus show it): additive and unlit sprites, self-lit layers, and the emission a mod's material gives it.
  // The values live in rtx_comp/emissive_scales.txt (hash, scale, and the name as a note) and are edited in the Dark Souls menu:
  // pick a texture in the game or take it from the list of what glows on screen, drag its scale, save.
  class DxvkDs1Emissive {
  public:
    // What kind of emission a draw has, for the menu's list.
    enum Kind : uint8_t { kMaterial, kAdditive, kUnlit, kSelfLit };

    // The scale on a material's emission, 1 when it has none. Also notes the material as glowing now, for the menu.
    static float scale(XXH64_hash_t materialHash, Kind kind, uint32_t frame);

    // Once a frame: takes in a file changed from outside.
    static void update();

    static void showImguiSettings(const Rc<DxvkContext>& ctx);
  };

}
