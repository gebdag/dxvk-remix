/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

// Fork (DS1): one draw of the player's body against the water (fork_ds1_ripples.h). A vertex at or under the water,
// wherever the pose has put it, wets its place in the body's grid: wading wets the legs, and a roll through a puddle
// the back and shoulders that dip into it, and neither stops being wet because the pose changes.

#include "rtx/pass/fork_ds1_ripples.h"

layout(binding = DS1_RIPPLE_BINDING_WATER)
RWTexture2D<float> water;

layout(binding = DS1_WET_BINDING_LEVEL)
RWTexture2D<float2> level;

layout(binding = DS1_WET_BINDING_GRID)
RWTexture2D<float4> grid;

layout(binding = DS1_WET_BINDING_POSITIONS)
StructuredBuffer<float> positions;

layout(binding = DS1_WET_BINDING_COLOURS)
StructuredBuffer<uint> colours;

layout(push_constant)
ConstantBuffer<Ds1WetSplatArgs> cb;

// The water surface over this place in the world, or below DS1_RIPPLE_NO_WATER.
float waterAt(vec3 world)
{
  if (cb.useTestLevel != 0)
  {
    return cb.testLevel;
  }
  const float atPlayer = level[uint2(0, 0)].x;
  if (atPlayer <= DS1_RIPPLE_NO_WATER)
  {
    return DS1_RIPPLE_NO_WATER;
  }
  const int n = int(cb.gridSize);
  const int2 cell = int2(floor(world.xz / cb.cellSize));
  const int2 local = cell - cb.origin;
  if (any(local < 0) || any(local >= n))
  {
    return DS1_RIPPLE_NO_WATER;
  }
  const float here = water[((cell % n) + n) % n];
  // Not above the water the player stands in: an arm over the rim of a higher pool stays dry.
  return here <= DS1_RIPPLE_NO_WATER ? here : min(here, atPlayer + 0.05f);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint id : SV_DispatchThreadID)
{
  if (id >= cb.vertexCount)
  {
    return;
  }
  const uint word = cb.positionWord + id * cb.positionStride;
  const vec4 position = vec4(positions[word], positions[word + 1], positions[word + 2], 1.0f);
  const vec3 world = vec3(dot(cb.objectToWorld[0], position), dot(cb.objectToWorld[1], position), dot(cb.objectToWorld[2], position));
  const vec2 side = world.xz - cb.playerXZ;
  if (dot(side, side) > cb.radiusSquared)
  {
    return;
  }
  const float kind = cb.useTestLevel != 0 ? float(cb.testMud) : level[uint2(0, 0)].y;
  const bool lava = kind > 1.5f;
  // Lava is what the body stands on, at the height found under it; it has no place in the water map.
  const float surface = lava ? (cb.useTestLevel != 0 ? cb.testLevel : level[uint2(0, 0)].x) : waterAt(world);
  if (surface <= DS1_RIPPLE_NO_WATER || world.y >= surface + (lava ? cb.lavaContact : cb.contactMargin))
  {
    return;
  }

  // Mud in the second channel, lava in the third; water washes both off, and lava burns the mud away.
  const float mud = lava ? 0.0f : kind;
  const uint colour = colours[cb.colourWord + id * cb.colourStride];
  const vec3 place = vec3(float((colour >> 16) & 0xFF), float((colour >> 8) & 0xFF), float(colour & 0xFF)) / 255.0f;
  const int3 cells = int3(DS1_WET_CELLS_X, DS1_WET_CELLS_Y, DS1_WET_CELLS_Z);
  const int3 first = int3(floor(place * vec3(cells) - 0.5f));
  // The colour holds the bind position, y = (height + 0.2) / 2.4 in metres. A cell's height in the world is about the vertex's
  // plus the cell's height above it in the bind pose (the body upright). Cells above the surface take nothing: the eight cells
  // round a vertex reach a cell further up, and the effect started above the water line.
  const float topAllowed = surface + (lava ? cb.lavaContact : cb.contactMargin);
  // The eight cells around it: vertices lie further apart than the cells on the larger polygons.
  for (int z = 0; z < 2; ++z)
  {
    for (int y = 0; y < 2; ++y)
    {
      for (int x = 0; x < 2; ++x)
      {
        const int3 c = clamp(first + int3(x, y, z), int3(0), cells - 1);
        const float cellHeight = world.y + ((float(c.y) + 0.5f) / float(DS1_WET_CELLS_Y) - place.y) * 2.4f;
        if (cellHeight > topAllowed)
        {
          continue;
        }
        const int2 slice = int2(c.z % DS1_WET_SLICES_PER_ROW, c.z / DS1_WET_SLICES_PER_ROW);
        const int2 texel = slice * int2(DS1_WET_CELLS_X, DS1_WET_CELLS_Y) + c.xy;
        if (lava)
        {
          // How deep this cell was: 0 at the top of the contact, 1 a hand under the surface. The deepest it has been while the coat lasts.
          const float depth = saturate((topAllowed - cellHeight) / max(cb.lavaContact + 0.12f, 0.05f));
          const vec4 old = grid[texel];
          grid[texel] = float4(old.x, 0.0f, 1.0f, old.z > 0.0f ? max(old.w, depth) : depth);
        }
        else
        {
          grid[texel] = float4(1.0f, mud, 0.0f, 0.0f);
        }
      }
    }
  }
}
