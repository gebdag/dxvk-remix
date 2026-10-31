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

// Fork (DS1): the ripple field's slope for the water material (fork_ds1_ripples.h), between the last two
// steps by the frame's place in the fixed step, so the field moves smoothly at any frame rate. Texel layout
// as the heights (toroidal); z = the cell's water height relative to the reference height.

#include "rtx/pass/fork_ds1_ripples.h"

layout(binding = DS1_RIPPLE_BINDING_HEIGHT)
RWTexture2D<float> height;

layout(binding = DS1_RIPPLE_BINDING_HEIGHT_PREV)
RWTexture2D<float> heightPrev;

layout(binding = DS1_RIPPLE_BINDING_WATER)
RWTexture2D<float> water;

layout(binding = DS1_RIPPLE_BINDING_SLOPE)
RWTexture2D<float4> slope;

layout(push_constant)
ConstantBuffer<Ds1RippleSlopeArgs> cb;

int2 wrapTexel(int2 cell, int n)
{
  return ((cell % n) + n) % n;
}

float heightAt(int2 windowCell)
{
  const int n = int(cb.gridSize);
  if (any(windowCell < 0) || any(windowCell >= n))
  {
    return 0.0f;
  }
  const int2 texel = wrapTexel(int2(cb.origin.x, cb.origin.y) + windowCell, n);
  if (water[texel] <= DS1_RIPPLE_NO_WATER)
  {
    return 0.0f;
  }
  return lerp(heightPrev[texel], height[texel], cb.alpha);
}

[shader("compute")]
[numthreads(DS1_RIPPLE_GROUP_SIZE, DS1_RIPPLE_GROUP_SIZE, 1)]
void main(uint2 thread : SV_DispatchThreadID)
{
  const int n = int(cb.gridSize);
  if (thread.x >= cb.gridSize || thread.y >= cb.gridSize)
  {
    return;
  }

  const int2 windowCell = int2(thread);
  const int2 texel = wrapTexel(int2(cb.origin.x, cb.origin.y) + windowCell, n);
  const float waterHeight = water[texel];
  if (waterHeight <= DS1_RIPPLE_NO_WATER)
  {
    slope[texel] = float4(0.0f, 0.0f, DS1_RIPPLE_NO_WATER_RELATIVE, 0.0f);
    return;
  }

  const float inverseSpan = 0.5f / cb.cellSize;
  const float dhdx = (heightAt(windowCell + int2(1, 0)) - heightAt(windowCell - int2(1, 0))) * inverseSpan;
  const float dhdz = (heightAt(windowCell + int2(0, 1)) - heightAt(windowCell - int2(0, 1))) * inverseSpan;
  slope[texel] = float4(dhdx, dhdz, waterHeight - cb.referenceHeight, heightAt(windowCell));
}
