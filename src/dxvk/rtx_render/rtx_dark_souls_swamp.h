#pragma once

#include "rtx_option.h"
#include "rtx/pass/fork_ds1_swamp.h"

#include <string>

namespace dxvk {

  // Dark Souls 1 FRPG_Snow surfaces (the Blighttown swamp): the game's height field, colour,
  // reflection and wake rings, evaluated in the material (fork_ds1_swamp.h). The DS1 proxy tags
  // the draws and sends the game's constants and each frame's rings; the defaults are Blighttown's.
  class DxvkDarkSoulsSwamp {
  public:
    static void fillShaderArgs(Ds1SwampArgs& args);
    static void showImguiSettings();

    RTX_OPTION("rtx.ds1Swamp", bool, enable, true,
               "Shade surfaces the DS1 proxy tags as FRPG_Snow (the Blighttown swamp) from the game's height field: "
               "its colour, reflection, slope normal and wake rings.");
    RTX_OPTION("rtx.ds1Swamp", float, roughness, -1.0f,
               "Perceptual roughness of the swamp's reflection; below 0 = the one whose highlight peak matches the game's "
               "(from the reflection constant c87).");
    RTX_OPTION("rtx.ds1Swamp", float, metresPerUv, 5.0f,
               "World size of one texture coordinate unit on the swamp mesh (Blighttown m14: 5 m, measured by "
               "scripts/gen_swamp_material.py). Places the slope's sample points.");
    RTX_OPTION("rtx.ds1Swamp", float, ringStrength, 1.0f, "Scale on the wake rings' depth (1 = the game's).");

    // Game state, set by the proxy (defaults: Blighttown, captured). Never saved.
    RTX_OPTION_FLAG("rtx.ds1Swamp", Vector3, deepColour, Vector3(0.094118f, 0.078431f, 0.039216f), RtxOptionFlags::NoSave, "c170.");
    RTX_OPTION_FLAG("rtx.ds1Swamp", Vector3, crustColour, Vector3(0.2f, 0.19216f, 0.18431f), RtxOptionFlags::NoSave, "c100.");
    RTX_OPTION_FLAG("rtx.ds1Swamp", Vector3, reflectColour, Vector3(0.36078f, 0.34118f, 0.30196f), RtxOptionFlags::NoSave, "c101.");
    RTX_OPTION_FLAG("rtx.ds1Swamp", Vector3, envColour, Vector3(0.79804f, 0.79804f, 0.66863f), RtxOptionFlags::NoSave, "c87.");
    RTX_OPTION_FLAG("rtx.ds1Swamp", Vector3, layerScale, Vector3(0.11f, 0.17f, 0.26f), RtxOptionFlags::NoSave, "Height pass VS c143.");
    RTX_OPTION_FLAG("rtx.ds1Swamp", Vector3, layerWeight, Vector3(0.4f, 0.4f, 0.4f), RtxOptionFlags::NoSave, "Height pass PS c171.");
    RTX_OPTION_FLAG("rtx.ds1Swamp", Vector3, ramps, Vector3(2.5f, 0.1f, 0.0f), RtxOptionFlags::NoSave, "c172.y, c172.z (deep colour ramp).");
    RTX_OPTION_FLAG("rtx.ds1Swamp", Vector3, reflectRamps, Vector3(1.3333f, 0.05f, 0.02f), RtxOptionFlags::NoSave, "c173.xyz.");
    RTX_OPTION_FLAG("rtx.ds1Swamp", Vector3, uvScales, Vector3(1.0f, 10.0f, 1.0f), RtxOptionFlags::NoSave,
                    "VS c145.x (colour), VS c144.x (detail normal), PS c172.x (detail normal strength).");
    RTX_OPTION_FLAG("rtx.ds1Swamp", float, spanPerDepth, 0.39391f * 8.0f / 360.0f, RtxOptionFlags::NoSave, "VS c142.x * 8 / c142.w.");
    RTX_OPTION_FLAG("rtx.ds1Swamp", float, fadeDistance, 300.0f, RtxOptionFlags::NoSave, "PS c163.z.");
    RTX_OPTION_FLAG("rtx.ds1Swamp", float, sceneDiffuseScale, 0.6f, RtxOptionFlags::NoSave,
                    "c100 of the frame's map materials (most of their geometry), which Remix does not apply to them: "
                    "the swamp's colours are divided by it to keep their ratio to the scene.");
    RTX_OPTION_FLAG("rtx.ds1Swamp", Vector3, layerOffsetU, Vector3(0.0f, 0.0f, 0.0f), RtxOptionFlags::NoSave,
                    "Height pass VS c246.x, c247.x, c248.x: each layer's scroll in u (wrapped).");
    RTX_OPTION_FLAG("rtx.ds1Swamp", Vector3, layerOffsetV, Vector3(0.0f, 0.0f, 0.0f), RtxOptionFlags::NoSave,
                    "Height pass VS c246.y, c247.y, c248.y: each layer's scroll in v (wrapped).");
    RTX_OPTION_FLAG("rtx.ds1Swamp", float, heightScale, 1.0f, RtxOptionFlags::NoSave, "c169.x: the height's weight on the slope.");
    RTX_OPTION_FLAG("rtx.ds1Swamp", bool, heightInAlpha, false, RtxOptionFlags::NoSave,
                    "The height pass reads a texture of its own, which the proxy bakes into the colour texture's alpha "
                    "(Gaping Dragon's slime); false: it reads the colour texture's r + g + b (Blighttown).");
    RTX_OPTION("rtx.ds1Swamp", bool, wade, false,
               "This surface is a liquid: bodies push it around and it soaks them in mud. Set per area: Painted World's snow is the same shader.");
    RTX_OPTION("rtx.ds1Swamp", float, envScale, -1.0f,
               "Reflection weight in place of the game's c87, set per area (the Depths' slime, where 3 saturates it). -1 = c87.");
    RTX_OPTION_FLAG("rtx.ds1Swamp", float, meshMetresPerUv, 0.0f, RtxOptionFlags::NoSave,
                    "The surface mesh's metres per texture coordinate, measured by the proxy; 0 = metresPerUv.");
    RTX_OPTION_FLAG("rtx.ds1Swamp", std::string, rings, "", RtxOptionFlags::NoSave,
                    "The frame's wake rings: u_x,u_z,u_1,v_x,v_z,v_1,strength;... (ring texture coordinates from world x, z).");
  };

} // namespace dxvk
