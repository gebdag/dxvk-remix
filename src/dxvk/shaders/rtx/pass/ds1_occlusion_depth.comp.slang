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

// Fork: the last ray traced frame's primary view depth, scaled to the game's depth target and turned
// into its depth values with the game's projection. Pixels the primary ray missed (sky) are far.

#include "rtx/pass/ds1_occlusion_depth.h"

layout(binding = DS1_OCD_BINDING_LINEAR_VIEW_Z)
Texture2D<float> LinearViewZ;

layout(binding = DS1_OCD_BINDING_OUTPUT)
RWTexture2D<float> OutputDepth;

layout(push_constant)
ConstantBuffer<Ds1OcclusionDepthArgs> cb;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(pixel >= cb.outputSize))
  {
    return;
  }

  uint2 inputSize;
  LinearViewZ.GetDimensions(inputSize.x, inputSize.y);
  const vec2 uv = (vec2(pixel) + 0.5f) / vec2(cb.outputSize);
  const uint2 texel = min(uint2(uv * vec2(inputSize)), inputSize - 1);
  const float viewZ = LinearViewZ[texel];

  float depth = 1.0f;
  if (viewZ != cb.missLinearViewZ && abs(viewZ) > 0.0f)
  {
    depth = cb.projZZ + cb.projWZ / abs(viewZ);
    depth = isfinite(depth) ? saturate(depth) : 1.0f;
  }
  OutputDepth[pixel] = depth;
}
