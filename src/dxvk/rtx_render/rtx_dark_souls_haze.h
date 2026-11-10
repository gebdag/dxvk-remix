#pragma once

#include "dxvk_include.h"
#include "dxvk_context.h"
#include "rtx_resources.h"
#include "rtx_options.h"
#include "rtx/pass/post_fx/post_fx_dark_souls_haze.h"

namespace dxvk {

  class DxvkDevice;

  // Dark Souls 1 haze: the fog and sky-scattering tail of the game's map and
  // character pixel shaders, with the game's own per-area values. The DS1 look's
  // scene pass applies it (rtx.ds1Look.hazeEnable switches it).
  //
  // The DS1 proxy reads those values from its draws and sets the game-state
  // options below each frame through the Remix API (SetConfigVariable); they are
  // never saved. displayGain and skyDistance are the user's.
  class DxvkDarkSoulsHaze {
  public:
    void showImguiSettings();

    // The haze constants for this frame, also used by the DS1 look's scene pass. False when the
    // game has sent no haze values (the args are then zeroed: no fog, no scattering).
    bool fillArgs(PostFxDarkSoulsHazeArgs& args, const VkExtent3D& extent, const Resources::Resource& linearViewZ,
                  const Camera& camera, const float jitterOffset[2], float missLinearViewZ) const;

  private:
    RTX_OPTION("rtx.ds1Haze", float, displayGain, 1.0f,
               "Scale on the colours the haze adds (fog colour and in-scatter), on top of the game's exposure.");
    RTX_OPTION("rtx.ds1Haze", float, minSurfaceShare, 0.0f,
               "For a haze set whose tint takes nearly all of a surface's colour (c105 under 0.1): the least share of the surface's light kept, 0 to 1, of what extinction alone leaves. "
               "0: the game's constants as they are. For an area override: at Ash Lake's dragon end fog and scattering keep 16% of a surface's light at arm's length (c105 = 1/255, blend 0.75), "
               "so lights that suit the sand beside it leave this sand black.");
    RTX_OPTION("rtx.ds1Haze", float, skyDistance, 0.0f,
               "Primary misses (the sky) are hazed like a surface at this distance in world units. 0 = not hazed.");

    // Game state, set by the proxy. Never saved.
    RTX_OPTION_FLAG("rtx.ds1Haze", bool, active, false, RtxOptionFlags::NoSave,
                    "Set by the DS1 proxy once it has read the game's haze values.");
    RTX_OPTION_FLAG("rtx.ds1Haze", Vector3, fogColor, Vector3(0.0f), RtxOptionFlags::NoSave, "Fog colour (PS c103.rgb).");
    RTX_OPTION_FLAG("rtx.ds1Haze", float, fogStrength, 0.0f, RtxOptionFlags::NoSave, "Fog factor at full range (PS c103.w).");
    RTX_OPTION_FLAG("rtx.ds1Haze", float, fogStart, 0.0f, RtxOptionFlags::NoSave, "Linear view depth where fog starts (VS c128.x).");
    RTX_OPTION_FLAG("rtx.ds1Haze", float, fogInvRange, 0.0f, RtxOptionFlags::NoSave, "1 / fog range (VS c128.y).");
    RTX_OPTION_FLAG("rtx.ds1Haze", Vector3, extinction, Vector3(0.0f), RtxOptionFlags::NoSave, "Extinction per world unit (PS c104.rgb).");
    RTX_OPTION_FLAG("rtx.ds1Haze", Vector3, sunDirection, Vector3(0.0f, 1.0f, 0.0f), RtxOptionFlags::NoSave, "Scattering sun direction (PS c111.xyz).");
    RTX_OPTION_FLAG("rtx.ds1Haze", float, extinctionScale, 0.0f, RtxOptionFlags::NoSave, "Extinction distance scale (PS c111.w).");
    RTX_OPTION_FLAG("rtx.ds1Haze", Vector3, phase, Vector3(0.0f), RtxOptionFlags::NoSave, "Mie phase terms (PS c107.xyz: 1 - g^2, 1 + g, 2g).");
    RTX_OPTION_FLAG("rtx.ds1Haze", Vector3, rayleigh, Vector3(0.0f), RtxOptionFlags::NoSave, "Rayleigh in-scatter (PS c108.rgb).");
    RTX_OPTION_FLAG("rtx.ds1Haze", Vector3, mie, Vector3(0.0f), RtxOptionFlags::NoSave, "Mie in-scatter (PS c109.rgb).");
    RTX_OPTION_FLAG("rtx.ds1Haze", Vector3, inverseExtinction, Vector3(0.0f), RtxOptionFlags::NoSave, "1 / extinction, normalising the in-scatter (PS c106.rgb).");
    RTX_OPTION_FLAG("rtx.ds1Haze", Vector3, surfaceTint, Vector3(1.0f), RtxOptionFlags::NoSave, "Tint on the attenuated surface (PS c105.rgb).");
    RTX_OPTION_FLAG("rtx.ds1Haze", float, inscatterScale, 0.0f, RtxOptionFlags::NoSave, "In-scatter scale (PS c105.w).");
    RTX_OPTION_FLAG("rtx.ds1Haze", Vector3, sunColor, Vector3(1.0f), RtxOptionFlags::NoSave, "Scattering sun colour (PS c110.rgb).");
    RTX_OPTION_FLAG("rtx.ds1Haze", float, scatterBlend, 0.0f, RtxOptionFlags::NoSave, "Weight of the scattered colour (PS c110.w).");
    RTX_OPTION_FLAG("rtx.ds1Haze", std::string, sets, "", RtxOptionFlags::NoSave,
                    "The per-draw haze sets by slot (Surface::ds1HazeSet): 'slot: c103..c111 (36 values), VS c128.x, c128.y;' "
                    "for each slot in use.");
  };

} // namespace dxvk
