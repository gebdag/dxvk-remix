#pragma once

#include "rtx_option.h"

namespace dxvk {

  // Dark Souls 1 specular colour materials (the z_pbr mod's specular_color_texture / specular_color_scale):
  // the per-draw environment reflection weight the DS1 proxy measures from the game's shader constants, in
  // stage 1's D3DTSS_CONSTANT (0xD7000000 | half, the proxy's env_reflection.cpp). See Surface::ds1EnvReflection.
  class DxvkDarkSoulsSpecular {
  public:
    RTX_OPTION("rtx.ds1Specular", bool, envReflectionWeight, true,
               "Scale a DS1 specular colour material's reflections of its surroundings down to the level the game "
               "reflects its own environment cube at for that draw (near black in the Undead Asylum), instead of the "
               "material's typical level.");
  };

} // namespace dxvk
