#pragma once

#include "rtx_option.h"
#include "rtx/pass/fork_ds1_ambient.h"

namespace dxvk {

  // Dark Souls 1 ambient: the area's irradiance probe and hemisphere ambient as
  // the radiance of the sky seen by bounce rays (see fork_ds1_ambient.h). The DS1
  // proxy sets the cube each frame from the game's probe texture and constants.
  class DxvkDarkSoulsAmbient {
  public:
    static void fillShaderArgs(Ds1AmbientArgs& args);
    static void showImguiSettings();

    RTX_OPTION("rtx.ds1Ambient", bool, enable, true,
               "Bounce rays that escape to the sky take the game's ambient light for the area (its irradiance probe and "
               "hemisphere ambient, set each frame by the DS1 proxy) instead of the sky texture.");
    RTX_OPTION("rtx.ds1Ambient", float, gain, 1.0f, "Scale on the game's ambient light.");

    // Game state, set by the proxy. Never saved.
    RTX_OPTION_FLAG("rtx.ds1Ambient", bool, active, false, RtxOptionFlags::NoSave,
                    "Set by the DS1 proxy once it has read the game's ambient light.");
    RTX_OPTION_FLAG("rtx.ds1Ambient", Vector3, posX, Vector3(0.0f), RtxOptionFlags::NoSave, "Ambient light towards +X.");
    RTX_OPTION_FLAG("rtx.ds1Ambient", Vector3, negX, Vector3(0.0f), RtxOptionFlags::NoSave, "Ambient light towards -X.");
    RTX_OPTION_FLAG("rtx.ds1Ambient", Vector3, posY, Vector3(0.0f), RtxOptionFlags::NoSave, "Ambient light towards +Y (up).");
    RTX_OPTION_FLAG("rtx.ds1Ambient", Vector3, negY, Vector3(0.0f), RtxOptionFlags::NoSave, "Ambient light towards -Y (down).");
    RTX_OPTION_FLAG("rtx.ds1Ambient", Vector3, posZ, Vector3(0.0f), RtxOptionFlags::NoSave, "Ambient light towards +Z.");
    RTX_OPTION_FLAG("rtx.ds1Ambient", Vector3, negZ, Vector3(0.0f), RtxOptionFlags::NoSave, "Ambient light towards -Z.");
  };

} // namespace dxvk
