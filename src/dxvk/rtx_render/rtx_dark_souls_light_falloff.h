#pragma once

#include "rtx_option.h"

#include <cstdint>

namespace dxvk {

  struct RtLight;
  class RtxContext;

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
    // Before the frame's apply calls: forgets last frame's player light.
    static void beginFrame();

    // Sets the light's falloff from the table (or clears it). light handle = its API hash.
    static void apply(uint64_t handle, RtLight& light);

    // The player light hangs at the head plus an offset the proxy sets (behind the character by
    // default, so the body casts its shadow forward). Backed into a wall, that offset puts the light
    // behind the wall, and the room goes dark around the player. Once the light buffer and the TLAS
    // are built, one ray from the head (rtx.ds1Lights.playerAnchor) towards the light moves the light
    // back short of the first surface in the way. Call after the frame's TLAS build and light upload,
    // before anything samples the lights.
    static void dispatchPlayerLightClearance(RtxContext* ctx);

    RTX_OPTION_FLAG("rtx.ds1Lights", std::string, falloffTable, "", RtxOptionFlags::NoSave,
                    "Set by the DS1 proxy: hash:begin:end;... for the lights that use the game's falloff.");
    RTX_OPTION_FLAG("rtx.ds1Lights", std::string, playerLight, "", RtxOptionFlags::NoSave,
                    "Set by the DS1 proxy: the player light's API hash (hex).");
    RTX_OPTION_FLAG("rtx.ds1Lights", Vector3, playerAnchor, Vector3(0.0f), RtxOptionFlags::NoSave,
                    "Set by the DS1 proxy every frame: where the player light hangs before its offset (the head).");
    RTX_OPTION("rtx.ds1Lights", bool, playerLightClearance, true,
               "Keep the DS1 player light out of walls: pull it back from its offset towards the head when a surface is in the way.");
    RTX_OPTION("rtx.ds1Lights", float, playerLightClearanceMargin, 0.1f,
               "Gap between the DS1 player light's emitter and the surface it was pulled back from, in world units.");
    RTX_OPTION("rtx.ds1Lights", float, playerLightClearanceIgnore, 0.1f,
               "Hits closer than this to the head are ignored, in world units (characters never count).");
  };

} // namespace dxvk
