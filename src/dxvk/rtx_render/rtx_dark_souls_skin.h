#pragma once

#include "rtx_option.h"

namespace dxvk {

  // Fork (DS1): human skin as a Remix diffusion profile, on the body draws the proxy tags with the character's tone (stage 5: 0xD3rrggbb). Not on faces: with a
  // profile on the FaceGen base the face goes dark, with or without transmission (a face is two coplanar layers); an experiment, off by default.
  // A fixed transmittance colour fails on skin (detail and dark skin wash out), so a tagged surface has none: its scattering takes the pixel's own colour, tone included
  // (opaque_surface_material_interaction: a diffusion profile whose transmittance is zero).
  class DxvkDarkSoulsSkin {
  public:
    RTX_OPTION("rtx.ds1Skin", bool, enable, false, "Experimental: human skin (bodies, not faces) scatters light under the surface (subsurface diffusion profile, coloured by the skin itself).");
    RTX_OPTION("rtx.ds1Skin", Vector3, radius, Vector3(1.0f, 0.37f, 0.15f), "How far each colour scatters, relative to the others.");
    RTX_OPTION("rtx.ds1Skin", float, radiusScale, 0.03f, "Scale on the radius.");
    RTX_OPTION("rtx.ds1Skin", float, maxSampleRadius, 16.0f, "Largest screen radius of a scattering sample.");
  };

} // namespace dxvk
