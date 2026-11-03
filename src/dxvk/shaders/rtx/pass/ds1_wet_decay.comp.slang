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

// Fork (DS1): the wet player's grid dries (fork_ds1_ripples.h): each cell runs from 1 to 0 over the drying time.

#include "rtx/pass/fork_ds1_ripples.h"

layout(binding = DS1_WET_BINDING_GRID)
RWTexture2D<float4> grid;

layout(push_constant)
ConstantBuffer<Ds1WetDecayArgs> cb;

[shader("compute")]
[numthreads(DS1_WET_GROUP_SIZE, DS1_WET_GROUP_SIZE, 1)]
void main(uint2 texel : SV_DispatchThreadID)
{
  uint2 size;
  grid.GetDimensions(size.x, size.y);
  if (any(texel >= size))
  {
    return;
  }
  const float wet = cb.reset != 0 ? 0.0f : cb.soak != 0 ? 1.0f : max(grid[texel].x - cb.decay, 0.0f);
  grid[texel] = float4(wet, 0.0f, 0.0f, 0.0f);
}
