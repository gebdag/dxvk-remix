#pragma once

#include "../../util/rc/util_rc_ptr.h"
#include "../../util/xxHash/xxhash.h"

namespace dxvk {

  class DxvkContext;

  // Fork (DS1): how strongly one game texture glows, set by eye. A scale on the emission of every draw whose material is that texture's
  // (by its hash, as the Remix menus show it): additive and unlit sprites, self-lit layers, and the emission a mod's material gives it.
  // The values live in rtx_comp/emissive_scales.txt (hash, scale, and the name as a note) and are edited in the Dark Souls menu:
  // pick a texture in the game or take it from the list of what glows on screen, drag its scale, save.
  // A change reaches a draw when its instance is next updated: every frame with rtx.enablePreservePath off, as DS1's rtx.conf has it.
  class DxvkDs1Emissive {
  public:
    // What kind of emission a draw has, for the menu's list.
    enum Kind : uint8_t { kMaterial, kAdditive, kUnlit, kSelfLit };

    // How a texture's draws are made to glow, chosen per texture in the menu: as the game and the proxy draw them, or always as an
    // unlit layer (its texture and vertex colour are its display colour, no light falls on it), or as a self-lit surface (it emits
    // its colour as real light, which the exposure, haze and bloom take like any other surface's, and which lights the scene).
    enum RenderAs : uint8_t { kAsDrawn, kAsUnlit, kAsSelfLit };
    static RenderAs renderAs(XXH64_hash_t materialHash);

    // The scale on a material's emission, 1 when it has none. Also notes the material as glowing now, for the menu.
    static float scale(XXH64_hash_t materialHash, Kind kind, uint32_t frame);

    // Once a frame: takes in a file changed from outside.
    static void update();

    static void showImguiSettings(const Rc<DxvkContext>& ctx);
  };

}
