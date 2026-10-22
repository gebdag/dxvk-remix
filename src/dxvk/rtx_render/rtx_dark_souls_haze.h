#pragma once

#include "dxvk_include.h"
#include "dxvk_context.h"
#include "rtx_resources.h"
#include "rtx_options.h"
#include "rtx/pass/post_fx/post_fx_dark_souls_haze.h"

namespace dxvk {

  class DxvkDevice;

  // Dark Souls 1 haze: the fog and sky-scattering tail of the game's map and
  // character pixel shaders, applied to the tonemapped frame with the game's own
  // per-area values (post_fx_dark_souls_haze.comp.slang). Runs right after tone
  // mapping, before the DS1 DoF and grading, as in the game.
  //
  // The DS1 proxy reads those values from its draws and sets the game-state
  // options below each frame through the Remix API (SetConfigVariable); they are
  // never saved. enable, displayGain and skyDistance are the user's.
  class DxvkDarkSoulsHaze {
  public:
    explicit DxvkDarkSoulsHaze(DxvkDevice* device);
    ~DxvkDarkSoulsHaze() = default;

    void dispatch(
      Rc<DxvkContext> ctx,
      const Resources::Resource& inOutColor,
      const Resources::Resource& linearViewZ,
      const VkExtent3D& extent,
      const Camera& camera,
      const float jitterOffset[2],
      float missLinearViewZ);

    void showImguiSettings();

    bool isEnabled() const { return enable() && active(); }

  private:
    DxvkDevice* m_device;
    Rc<DxvkBuffer> m_constants;

    RTX_OPTION("rtx.ds1Haze", bool, enable, true,
               "Applies Dark Souls' own fog and sky scattering to the tonemapped frame, with the game's per-area values "
               "(set each frame by the DS1 proxy). The formulas run on the frame gamma encoded, as the game's shaders do.");
    RTX_OPTION("rtx.ds1Haze", float, displayGain, 1.0f,
               "Scale on the colours the haze adds (fog colour and in-scatter), on top of the game's exposure.");
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
  };

} // namespace dxvk
