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
  uint pad0;
  uint pad1;
  uint pad2;
};

#ifndef __cplusplus
// Ambient-cube radiance for a direction: each axis weighted by its squared component.
vec3 ds1AmbientRadiance(Ds1AmbientArgs args, vec3 direction)
{
  const vec3 d2 = direction * direction;
  return d2.x * (direction.x >= 0.0f ? args.cube[0].rgb : args.cube[1].rgb)
       + d2.y * (direction.y >= 0.0f ? args.cube[2].rgb : args.cube[3].rgb)
       + d2.z * (direction.z >= 0.0f ? args.cube[4].rgb : args.cube[5].rgb);
}
#endif
