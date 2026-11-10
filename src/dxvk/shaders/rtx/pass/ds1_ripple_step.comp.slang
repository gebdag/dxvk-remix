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
// them in, the next heights out. Cells without water, cells inside a body and cells outside the window count
// as 0, so the waves wrap around legs. A body that moved since the last step pushed its water aside: the
// water rises around the cells it has just filled and sinks around the cells it has just left, spread over a
// few cells (the displaced water does not stay in the body's own cells). A body standing still pushes none.

#include "rtx/pass/fork_ds1_ripples.h"

layout(binding = DS1_RIPPLE_BINDING_HEIGHT)
RWTexture2D<float> height;

layout(binding = DS1_RIPPLE_BINDING_HEIGHT_PREV)
RWTexture2D<float> heightPrev;

layout(binding = DS1_RIPPLE_BINDING_HEIGHT_NEXT)
RWTexture2D<float> heightNext;

layout(binding = DS1_RIPPLE_BINDING_WATER)
RWTexture2D<float> water;

layout(binding = DS1_RIPPLE_BINDING_OCCUPANCY)
RWTexture2D<float> occupancy;

layout(binding = DS1_RIPPLE_BINDING_OCCUPANCY_PREV)
RWTexture2D<float> occupancyPrev;

layout(push_constant)
ConstantBuffer<Ds1RippleStepArgs> cb;

#define kTile (DS1_RIPPLE_GROUP_SIZE + 2 * DS1_RIPPLE_KERNEL_RADIUS)
groupshared float tile[kTile][kTile];

int2 wrapTexel(int2 cell, int n)
{
  return ((cell % n) + n) % n;
}

bool isOpen(int2 texel)
{
  return water[texel] > DS1_RIPPLE_NO_WATER && occupancy[texel] < 0.5f;
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
  return isOpen(texel) ? heightPrev[texel] : 0.0f;
}

// The water the bodies pushed aside since the last step around a window cell: the change in what they
// fill, spread over 5x5 cells by binomial weights (1 4 6 4 1)^2 / 256.
float displaced(int2 windowCell)
{
  const int n = int(cb.gridSize);
  const int2 collisionMin = int2(cb.collisionMin.x, cb.collisionMin.y);
  if (any(windowCell < collisionMin - 2) || any(windowCell >= collisionMin + int(cb.collisionSize) + 2))
  {
    return 0.0f;
  }
  const float weights[5] = { 1.0f, 4.0f, 6.0f, 4.0f, 1.0f };
  float sum = 0.0f;
  for (int dy = -2; dy <= 2; ++dy)
  {
    for (int dx = -2; dx <= 2; ++dx)
    {
      const int2 other = windowCell + int2(dx, dy);
      if (any(other < 0) || any(other >= n))
      {
        continue;
      }
      const int2 texel = wrapTexel(int2(cb.origin.x, cb.origin.y) + other, n);
      // Bodies only: a cell falling water lands in is open.
      sum += weights[dy + 2] * weights[dx + 2] * (step(0.5f, occupancy[texel]) - step(0.5f, occupancyPrev[texel]));
    }
  }
  return sum / 256.0f;
}

float latticeValue(int3 p)
{
  uint h = uint(p.x) * 0x8DA6B343u ^ uint(p.y) * 0xD8163841u ^ uint(p.z) * 0xCB1AB31Fu;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return float(h & 0xFFFFu) / 32767.5f - 1.0f;
}

// Falling water's push on a cell it lands in, -1 to 1: smooth noise in patches of a few cells that change fallRate times a
// second, so the water is thrown about in splashes of a hand's size instead of cell by cell.
float fallPush(int2 cell)
{
  const vec3 p = vec3(float(cell.x) * 0.25f, float(cell.y) * 0.25f, cb.fallTime * cb.fallRate);
  const int3 base = int3(floor(p));
  vec3 f = p - floor(p);
  f = f * f * (3.0f - 2.0f * f);
  float value = 0.0f;
  for (int corner = 0; corner < 8; ++corner)
  {
    const int3 o = int3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
    const vec3 w = lerp(1.0f - f, f, vec3(o));
    value += latticeValue(base + o) * w.x * w.y * w.z;
  }
  return value;
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
      h = isOpen(texel) ? height[texel] : 0.0f;
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
  if (!isOpen(texel))
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
  float next = cb.thickDamping > 0.0f
    ? (h + (h - hPrev) * (1.0f - cb.thickDamping) / (1.0f + cb.thickDamping) - cb.gravity * verticalDerivative / (1.0f + cb.thickDamping)) * (1.0f - cb.thickSettle)
    : (h * (2.0f - cb.damping) - hPrev - cb.gravity * verticalDerivative) / (1.0f + cb.damping);

  // Viscous damping, dv/dt = 2 nu Laplacian(v) on the velocity v = h - hPrev: strongest on the shortest waves.
  const float velocityLaplacian =
    (tile[centre.y][centre.x + 1] - previousAt(windowCell + int2(1, 0))) +
    (tile[centre.y][centre.x - 1] - previousAt(windowCell - int2(1, 0))) +
    (tile[centre.y + 1][centre.x] - previousAt(windowCell + int2(0, 1))) +
    (tile[centre.y - 1][centre.x] - previousAt(windowCell - int2(0, 1))) -
    4.0f * (h - hPrev);
  next += cb.viscosity * velocityLaplacian;
  // Rounds what thick mud would otherwise keep as an edge, where a body held it at 0; broad shapes barely notice.
  next += cb.smoothing * (tile[centre.y][centre.x + 1] + tile[centre.y][centre.x - 1] + tile[centre.y + 1][centre.x] + tile[centre.y - 1][centre.x] - 4.0f * h);

  next += cb.displacement * displaced(windowCell);
  if (cb.fallStrength > 0.0f && occupancy[texel] > 0.5f * DS1_RIPPLE_OCCUPANCY_FALL)
  {
    next += cb.fallStrength * fallPush(cell);
  }

  // Damped to 0 over the window's outer cells.
  const int edge = min(min(int(thread.x), int(thread.y)), min(n - 1 - int(thread.x), n - 1 - int(thread.y)));
  next *= lerp(0.8f, 1.0f, saturate(float(edge) / cb.borderCells));

  heightNext[texel] = next;
}
