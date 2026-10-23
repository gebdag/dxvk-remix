#pragma once

// Fork: Dark Souls' ambient light as the sky that bounce rays see.
//
// DS1 lights most surfaces with the area's irradiance probe (the s11 cube times
// c86) plus a hemisphere ambient (lerp(c99, c98, N.y * 0.5 + 0.5)). The DS1 proxy
// reduces both to an ambient cube (one colour per axis direction) each frame;
// indirect rays that escape to the sky take their radiance from it, so surfaces
// receive the game's ambient where they can see the sky, occluded and bounced by
// the path tracer. Filled by DxvkDarkSoulsAmbient (rtx_dark_souls_ambient.cpp).

#include "rtx/utility/shader_types.h"

struct Ds1AmbientArgs {
  // +X, -X, +Y, -Y, +Z, -Z (rgb; w unused)
  vec4 cube[6];

  uint enable;
  // DS1 look (rtx_dark_souls_look.h): 0 = sky radiance as rasterized; 1 = the sky raster holds the
  // game's 8-bit sky value as is (skyForceHDR), 2 = sRGB-decoded (8-bit sky target). Either way it
  // becomes (value * skyUnexpose)^2.2, the radiance the DS1 scene pass maps back to that value.
  uint skyGamma;
  // c135.y / c135.x: the game's sky shader already applied its exposure.
  float skyUnexpose;
  uint pad0;
};

#ifndef __cplusplus
// The game's own sky raster as radiance in the space the DS1 look's scene pass expects.
vec3 ds1SkyRadiance(uint skyGamma, float skyUnexpose, vec3 raster)
{
  if (skyGamma == 0)
  {
    return raster;
  }
  vec3 s = max(raster, vec3(0.0f));
  if (skyGamma == 2)
  {
    s = lerp(s * 12.92f, 1.055f * pow(s, vec3(1.0f / 2.4f)) - 0.055f, step(vec3(0.0031308f), s));
  }
  return pow(max(s * skyUnexpose, vec3(0.0f)), vec3(2.2f));
}

vec3 ds1SkyRadiance(Ds1AmbientArgs args, vec3 raster)
{
  return ds1SkyRadiance(args.skyGamma, args.skyUnexpose, raster);
}

// Ambient-cube radiance for a direction: each axis weighted by its squared component.
vec3 ds1AmbientRadiance(Ds1AmbientArgs args, vec3 direction)
{
  const vec3 d2 = direction * direction;
  return d2.x * (direction.x >= 0.0f ? args.cube[0].rgb : args.cube[1].rgb)
       + d2.y * (direction.y >= 0.0f ? args.cube[2].rgb : args.cube[3].rgb)
       + d2.z * (direction.z >= 0.0f ? args.cube[4].rgb : args.cube[5].rgb);
}
#endif
