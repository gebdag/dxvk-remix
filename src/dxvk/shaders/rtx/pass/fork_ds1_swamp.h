#pragma once

// Fork: Dark Souls' FRPG_Snow surfaces (the Blighttown swamp), shaded from the game's own height field.
//
// The game draws the swamp in two passes (shaders disassembled with lookdev/analyze.py):
//   FRPG_Snow_HeightMap  h = sum_i c171_i * (r + g + b)(colour texture at mesh_uv * c143_i) / 3, faded
//                        to 0 at c163.z of view depth, into a 512x360 screen-space buffer. WaterWave
//                        draws (FRPG_WWS_Dif..._WaterWave, the ring texture s00019 on a small disc where a
//                        character wades) then subtract min(ring * c100.x*c100.w * c139.x*c139.w, 1).
//   FRPG_Snow_*          colour = lerp(texture(mesh_uv * c145.x) * c100, c170, saturate((h - c172.z) * c172.y));
//                        reflection (environment * c87 + point light highlight) * c101 * saturate((h - c173.y) * c173.x),
//                        added on top; normal = the height field's slope, (h(p + T * span) - h(p)) clamped
//                        to +-c173.z over 2 * span (span = view depth * c142.x * 8 / c142.w), with the tar
//                        detail normal (texture at mesh_uv * c144.x, x/y * c172.x) on top.
// The surface itself does not move (the layer offsets c246-c248 stay 0): only the rings animate it.
//
// Remix evaluates the same height field at the hit point and two points a span away along the
// texture axes. The DS1 proxy tags the draws (surface flag isDs1Swamp) and sends the constants and the
// frame's rings (rtx.ds1Swamp.*, rtx_dark_souls_swamp.h). The ds1_swamp mod (scripts/gen_swamp_material.py)
// gives the material the colour texture as stored with the ring texture in its alpha (an opaque draw's
// opacity is 1 regardless), and the tar detail normal as its normal map.
//
// The game's reflection has no Fresnel term, so it becomes the specular colour workflow's base
// reflectivity (F90 = F0): (c87 * c101 * mask)^2.2. The point light highlight, c101 * mask without c87,
// gives the roughness: a normalized GGX peak of F0 / (4 pi alpha^2) equals the game's at alpha^2 = c87^2.2 / (4 pi).
//
// Every other map material in DS1 multiplies its texture by c100 (0.6 on most of the game's geometry), and
// Remix leaves that out, so the scene around the swamp has its albedo / c100. The swamp's colours (which
// are the game's exact values) are divided by the same scene c100, sceneDiffuseScale, to keep the game's
// ratio to its surroundings; otherwise it alone reads 1/0.6 too dark, about 3x in linear.
//
// The surface shader gives the directional light c92/c95 only a rim lobe, exp(-1.44 * (N.L * c169.y)^2)
// * c169.z, zero in Blighttown (c169.y = 1e5, c169.z = 0); it is lit by its probe (s11 * c86 + hemisphere,
// about 0.85 from above) and the point lights. Remix's sun there is the probe's sun lobe (the proxy keeps
// the swamp's c95 out of the sun vote).

#include "rtx/utility/shader_types.h"

static const uint ds1SwampMaxRings = 16;

struct Ds1SwampArgs {
  // c170: the deep (tar) colour, stored (gamma) value.
  vec3 deepColour;
  // c172.y
  float deepScale;
  // c100: the crust colour, a tint on the colour texture.
  vec3 crustColour;
  // c172.z
  float deepOffset;
  // c87 * c101, stored (gamma) value: the reflection's colour at full mask.
  vec3 reflectColour;
  // c173.x
  float reflectScale;
  // c143.xyz: the three height layers' scales on the mesh UV.
  vec3 layerScale;
  // c173.y
  float reflectOffset;
  // c171.xyz / 3: each layer's weight on its texel's r + g + b.
  vec3 layerWeight;
  // c145.x
  float colourUvScale;

  // c144.x, c172.x
  float detailUvScale;
  float detailStrength;
  // c173.z: the largest height difference the slope takes, in metres.
  float slopeClamp;
  // c142.x * 8 / c142.w: the slope's span per metre of view depth.
  float spanPerDepth;

  // 1 / c163.z
  float invFadeDistance;
  // Texture coordinate units per metre along the surface (the mesh's UV mapping).
  float uvPerMetre;
  // Perceptual roughness.
  float roughness;
  uint ringCount;

  uint enable;
  // 1: the height is the colour texture's alpha (a separate height texture the proxy baked in), 0: its r + g + b.
  uint heightInAlpha;
  // 1 / the c100 of the map materials around the swamp (see above).
  float invSceneDiffuseScale;
  // c169.x: the height's weight on the slope (the colour and reflection masks take the height as is).
  float heightScale;

  // Height pass VS c246.xy, c247.xy, c248.xy: each layer's scroll.
  vec3 layerOffsetU;
  // c169.y
  float rimSharpness;
  vec3 layerOffsetV;
  float pad4;

  // c95 * c169.z: a lobe on the surface alone, added to its diffuse light. In the game's gamma.
  vec3 rimColour;
  float pad5;
  // c92
  vec3 rimDirection;
  float pad6;

  // Each ring's texture coordinates from world x, z: u = dot(rings[2i].xyz, (x, z, 1)), v = dot(rings[2i + 1].xyz, (x, z, 1));
  // rings[2i].w = its strength.
  vec4 rings[ds1SwampMaxRings * 2];
};
