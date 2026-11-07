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
  // Scale the DS1 sun on lightmapped surfaces by their lightmap (Surface::ds1SunLightmap).
  uint sunFollowsLightmap;

  // The sun as the proxy submits it, in the game's gamma space: the ambient on a surface facing it
  // (rgb) and its colour (rgb). Its radiance is (sunBase + sunLobe)^2.2 - sunBase^2.2.
  vec4 sunBase;
  vec4 sunLobe;

  // DS1 foliage (Surface::isDs1Foliage): the share of lighting rays that pass a leaf (rtx.ds1Foliage.transmission).
  float foliageTransmission;
  uint foliagePad0;
  uint foliagePad1;
  uint foliagePad2;
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

// The scale on the DS1 sun's radiance at a surface tagged with its lightmap level
// (Surface::ds1SunLightmap, 0xD1rrggbb, anything else = not lightmapped). The game's lightmapped map
// shaders add lightmap * lobe to the ambient in gamma space; the sun light adds
// (base + lobe)^2.2 - base^2.2 in linear space, so a level l takes
// ((base + l * lobe)^2.2 - base^2.2) / ((base + lobe)^2.2 - base^2.2) of it.
vec3 ds1SunLightmapScale(Ds1AmbientArgs args, uint tag)
{
  if (args.sunFollowsLightmap == 0 || (tag >> 24) != 0xD1u)
  {
    return vec3(1.0f);
  }
  const vec3 level = vec3(float((tag >> 16) & 0xFFu), float((tag >> 8) & 0xFFu), float(tag & 0xFFu)) / 255.0f;
  const vec3 base = max(args.sunBase.rgb, vec3(0.0f));
  const vec3 lobe = max(args.sunLobe.rgb, vec3(0.0f));
  const vec3 baseLinear = pow(base, vec3(2.2f));
  const vec3 full = pow(base + lobe, vec3(2.2f)) - baseLinear;
  const vec3 part = pow(base + level * lobe, vec3(2.2f)) - baseLinear;
  vec3 scale;
  scale.x = full.x > 1e-6f ? part.x / full.x : level.x;
  scale.y = full.y > 1e-6f ? part.y / full.y : level.y;
  scale.z = full.z > 1e-6f ? part.z / full.z : level.z;
  return clamp(scale, vec3(0.0f), vec3(1.0f));
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
