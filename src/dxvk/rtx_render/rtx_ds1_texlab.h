#pragma once

#include "rtx_option.h"
#include "../../util/rc/util_rc_ptr.h"
#include "../../util/xxHash/xxhash.h"

#include <string>

namespace dxvk {

  class DxvkContext;

  // Fork (DS1), development tool: pick a surface in the game, generate PBR maps for its texture with ComfyUI + PBRFusion and see the result
  // hot-loaded; set each material's POM depth and roughness by eye. The work is done by a helper process (pbr_tools/texlab/texlab.py),
  // reached through files in rtx_comp/texlab. Generated materials live in their own mod, rtx-remix/mods/ai_generated. All of it, the AI mod
  // included, is off unless rtx.ds1TexLab.enable is set, which a shipped configuration does not do.
  class DxvkDs1TexLab {
  public:
    RTX_OPTION("rtx.ds1TexLab", bool, enable, false, "Enables the Texture Lab development panel, its per-material overrides and the AI generated materials.");
    RTX_OPTION("rtx.ds1TexLab", bool, aiLayer, true, "Uses the AI generated materials (the ai_generated mod). Off shows what the game and the converted mods give.");
    RTX_OPTION("rtx.ds1TexLab", std::string, helperCommand, "", "The command that starts the Texture Lab helper when it is not running.");

    // The live multipliers on a material's POM depth (rtx_comp/texlab/pom_depth.txt) and roughness (roughness.txt), 1 when it has none.
    static float pomDepthScale(XXH64_hash_t materialHash);
    static float roughnessScale(XXH64_hash_t materialHash);

    // Whether this frame's materials must be uploaded even if the scene did not change: a value was just edited or the AI materials changed.
    static bool consumeMaterialUpload();
    static void requestMaterialUpload();

    // Once a frame: picks up finished generations (hot-reloads the AI mod) and edits of the tuning files.
    static void update(DxvkContext* ctx);

    static void showImguiSettings(const Rc<DxvkContext>& ctx);
  };

}
