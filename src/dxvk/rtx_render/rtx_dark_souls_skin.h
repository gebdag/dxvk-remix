#pragma once

#include "rtx_option.h"

namespace dxvk {

  // Fork (DS1): human skin as a Remix diffusion profile, without any maps: the proxy tags skin draws in stage 5 (0xD3rrggbb body with the character's tone, 0xD6000000 a face's FaceGen base).
  // A tagged surface has no transmittance colour of its own: scattering takes the pixel's colour, tone included (opaque_surface_material_interaction: a profile with zero transmittance).
  // Its sample rays only see the SSS TLAS, so a tagged draw must also be flagged subsurface on its instance (rtx_instance_manager), and the per-frame material cache keys on the tag (rtx_scene_manager).
  class DxvkDarkSoulsSkin {
  public:
    static void showImguiSettings();
    RTX_OPTION("rtx.ds1Skin", bool, skinnedShadowTerminator, true,
               "Shadow terminator offset for skinned meshes (characters, armour, enemies), whatever rtx.shadowTerminator.enableOffset says for the rest of the scene.");
    RTX_OPTION("rtx.ds1Skin", bool, enable, false, "Experimental: human skin scatters light under the surface (subsurface diffusion profile, coloured by the skin itself).");
    RTX_OPTION("rtx.ds1Skin", Vector3, radius, Vector3(1.0f, 0.37f, 0.15f), "How far each colour scatters, relative to the others.");
    RTX_OPTION("rtx.ds1Skin", float, radiusScale, 0.5f, "Scale on the radius, in centimetres (the profile's unit; the world is in metres).");
    RTX_OPTION("rtx.ds1Skin", float, maxSampleRadius, 0.1f, "How far a scattering sample may reach, metres. The sample rays start this far off the skin, so a large value starts them past whatever is around the character.");
  };

} // namespace dxvk
