#pragma once

#include "rtx_option.h"

#include <cstdint>

namespace dxvk {

  struct RtLight;

  // Dark Souls 1 point light falloff for the proxy's API lights.
  //
  // The game lights a surface with saturate((end - d) * c112.w) * c116.rgb * max(N.L, 0), with
  // c116.w = dwindle end and c112.w = 1 / (end - begin): flat to dwindle begin, then linear to zero
  // at dwindle end, in gamma space. An inverse-square light can't follow that: a torch mounted on a
  // wall burns a hot spot into the wall and leaves the room behind it dark. For the lights the
  // proxy lists, the sphere light evaluates (colour * att(d))^2.2 instead of inverse square (with
  // its radiance = colour^2.2 / pi; sphere_light.slangh), the game's light in linear space.
  //
  // The proxy sends the list as rtx.ds1Lights.falloffTable, "hash:begin:end;..." with the light's
  // API hash in hex. The switch is the proxy's (F4 > Lights > Game falloff): a listed light's
  // radiance is only right for this falloff, so the proxy changes both together.
  class DxvkDarkSoulsLightFalloff {
  public:
    // Sets the light's falloff from the table (or clears it). light handle = its API hash.
    static void apply(uint64_t handle, RtLight& light);

    RTX_OPTION_FLAG("rtx.ds1Lights", std::string, falloffTable, "", RtxOptionFlags::NoSave,
                    "Set by the DS1 proxy: hash:begin:end;... for the lights that use the game's falloff.");
  };

} // namespace dxvk
