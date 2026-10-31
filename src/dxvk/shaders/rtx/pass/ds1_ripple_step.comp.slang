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

// Fork (DS1): one iWave step of the ripple field (fork_ds1_ripples.h): the newest heights and the step before
// them in, the next heights out. Cells without water and cells outside the window count as 0. The wading
// player's bow (fork_ds1_ripples.h, Ds1RippleStepArgs) adds its change over the step: it adds no water
// overall and returns to nothing once they stop, so the field keeps no bump where they stand.

#include "rtx/pass/fork_ds1_ripples.h"

layout(binding = DS1_RIPPLE_BINDING_HEIGHT)
RWTexture2D<float> height;

layout(binding = DS1_RIPPLE_BINDING_HEIGHT_PREV)
RWTexture2D<float> heightPrev;

layout(binding = DS1_RIPPLE_BINDING_HEIGHT_NEXT)
RWTexture2D<float> heightNext;

layout(binding = DS1_RIPPLE_BINDING_WATER)
RWTexture2D<float> water;

layout(binding = DS1_RIPPLE_BINDING_PROBE)
RWTexture2D<float4> probe;

layout(push_constant)
ConstantBuffer<Ds1RippleStepArgs> cb;

#define kTile (DS1_RIPPLE_GROUP_SIZE + 2 * DS1_RIPPLE_KERNEL_RADIUS)
groupshared float tile[kTile][kTile];

int2 wrapTexel(int2 cell, int n)
{
  return ((cell % n) + n) % n;
}

// The step before's height of a window cell: 0 outside the window and where there is no water.
float previousAt(int2 windowCell)
{
  const int n = int(cb.gridSize);
  if (any(windowCell < 0) || any(windowCell >= n))
  {
    return 0.0f;
  }
  const int2 texel = wrapTexel(int2(cb.origin.x, cb.origin.y) + windowCell, n);
  return water[texel] > DS1_RIPPLE_NO_WATER ? heightPrev[texel] : 0.0f;
}

float bow(vec2 position, vec2 at, vec2 direction, float bowHeight)
{
  const vec2 ahead = position - (at + direction * cb.legOffset);
  const vec2 behind = position - (at - direction * cb.legOffset);
  return bowHeight * (exp(dot(ahead, ahead) * cb.bowExponent) - exp(dot(behind, behind) * cb.bowExponent));
}

[shader("compute")]
[numthreads(DS1_RIPPLE_GROUP_SIZE, DS1_RIPPLE_GROUP_SIZE, 1)]
void main(uint2 thread : SV_DispatchThreadID, uint2 groupThread : SV_GroupThreadID, uint2 group : SV_GroupID)
{
  const int n = int(cb.gridSize);
  const int2 origin = int2(cb.origin.x, cb.origin.y);
  const int2 tileFirst = int2(group) * DS1_RIPPLE_GROUP_SIZE - DS1_RIPPLE_KERNEL_RADIUS;

  for (int i = int(groupThread.y * DS1_RIPPLE_GROUP_SIZE + groupThread.x); i < kTile * kTile;
       i += DS1_RIPPLE_GROUP_SIZE * DS1_RIPPLE_GROUP_SIZE)
  {
    const int2 local = int2(i % kTile, i / kTile);
    const int2 windowCell = tileFirst + local;
    float h = 0.0f;
    if (all(windowCell >= 0) && all(windowCell < n))
    {
      const int2 texel = wrapTexel(origin + windowCell, n);
      h = water[texel] > DS1_RIPPLE_NO_WATER ? height[texel] : 0.0f;
    }
    tile[local.y][local.x] = h;
  }
  GroupMemoryBarrierWithGroupSync();

  if (thread.x >= cb.gridSize || thread.y >= cb.gridSize)
  {
    return;
  }

  const int2 windowCell = int2(thread);
  const int2 cell = origin + windowCell;
  const int2 texel = wrapTexel(cell, n);
  if (water[texel] <= DS1_RIPPLE_NO_WATER)
  {
    heightNext[texel] = 0.0f;
    return;
  }

  const int2 centre = int2(groupThread) + DS1_RIPPLE_KERNEL_RADIUS;
  float verticalDerivative = 0.0f;
  for (int dy = -DS1_RIPPLE_KERNEL_RADIUS; dy <= DS1_RIPPLE_KERNEL_RADIUS; ++dy)
  {
    for (int dx = -DS1_RIPPLE_KERNEL_RADIUS; dx <= DS1_RIPPLE_KERNEL_RADIUS; ++dx)
    {
      verticalDerivative += kDs1RippleKernel[abs(dy)][abs(dx)] * tile[centre.y + dy][centre.x + dx];
    }
  }

  const float h = tile[centre.y][centre.x];
  const float hPrev = previousAt(windowCell);
  float next = (h * (2.0f - cb.damping) - hPrev - cb.gravity * verticalDerivative) / (1.0f + cb.damping);

  // Viscous damping, dv/dt = 2 nu Laplacian(v) on the velocity v = h - hPrev: strongest on the shortest waves.
  const float velocityLaplacian =
    (tile[centre.y][centre.x + 1] - previousAt(windowCell + int2(1, 0))) +
    (tile[centre.y][centre.x - 1] - previousAt(windowCell - int2(1, 0))) +
    (tile[centre.y + 1][centre.x] - previousAt(windowCell + int2(0, 1))) +
    (tile[centre.y - 1][centre.x] - previousAt(windowCell - int2(0, 1))) -
    4.0f * (h - hPrev);
  next += cb.viscosity * velocityLaplacian;

  const float4 wading = probe[int2(0, 0)];
  if (wading.x > 0.5f && wading.y > 0.5f)
  {
    const vec2 position = (vec2(cell) + 0.5f) * cb.cellSize;
    next += bow(position, vec2(cb.position1.x, cb.position1.y), vec2(cb.direction1.x, cb.direction1.y), cb.bowHeight1) -
            bow(position, vec2(cb.position0.x, cb.position0.y), vec2(cb.direction0.x, cb.direction0.y), cb.bowHeight0);
  }

  // Damped to 0 over the window's outer cells.
  const int edge = min(min(int(thread.x), int(thread.y)), min(n - 1 - int(thread.x), n - 1 - int(thread.y)));
  next *= lerp(0.8f, 1.0f, saturate(float(edge) / cb.borderCells));

  heightNext[texel] = next;
}
