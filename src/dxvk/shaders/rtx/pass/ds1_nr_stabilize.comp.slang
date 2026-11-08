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

// Fork: DLSS-NR reworks the image differently with every frame's jitter, above all where it restructures characters
// (its automatic character mask and structural strength): its change flickers, the boiling. Here the change, output
// minus input, is averaged over frames: last frame's averaged change is reprojected along the motion vectors (the
// render grid's, previous minus current, in render pixels) and clamped to the range of this frame's change around
// the pixel, so a disocclusion or a change that really moved cannot ghost; the result is the input plus the averaged
// change. What the network adds stays; how it jitters from frame to frame goes.

#include "rtx/pass/ds1_nr_stabilize.h"

layout(binding = DS1_NR_STABILIZE_BINDING_INPUT)
Texture2D<float4> InColor;

layout(binding = DS1_NR_STABILIZE_BINDING_MOTION_VECTOR)
Texture2D<float2> InMotionVector;

layout(binding = DS1_NR_STABILIZE_BINDING_HISTORY_INPUT)
Texture2D<float4> InHistory;

layout(rgba16f, binding = DS1_NR_STABILIZE_BINDING_OUTPUT)
RWTexture2D<float4> InOutColor;

layout(rgba16f, binding = DS1_NR_STABILIZE_BINDING_HISTORY_OUTPUT)
RWTexture2D<float4> OutHistory;

layout(push_constant)
ConstantBuffer<Ds1NrStabilizeArgs> cb;

// A pixel the network left non-finite contributes no change (min, max and clamp are undefined on NaN, and the history would
// carry it on).
float3 finiteChange(float3 change)
{
  return any(isnan(change) || isinf(change)) ? float3(0.0f, 0.0f, 0.0f) : change;
}

float3 changeAt(int2 p)
{
  const int2 q = clamp(p, int2(0, 0), int2(cb.colorSize) - int2(1, 1));
  return finiteChange(InOutColor[q].rgb - InColor[q].rgb);
}

float3 historyAt(float2 uv)
{
  // Bilinear by hand (no sampler bound).
  const float2 texel = uv * float2(cb.colorSize) - 0.5f;
  const int2 base = int2(floor(texel));
  const float2 f = texel - float2(base);
  const int2 hi = int2(cb.colorSize) - int2(1, 1);
  const float3 a = InHistory[clamp(base, int2(0, 0), hi)].rgb;
  const float3 b = InHistory[clamp(base + int2(1, 0), int2(0, 0), hi)].rgb;
  const float3 c = InHistory[clamp(base + int2(0, 1), int2(0, 0), hi)].rgb;
  const float3 d = InHistory[clamp(base + int2(1, 1), int2(0, 0), hi)].rgb;
  return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  if (any(threadId >= cb.colorSize))
  {
    return;
  }
  const int2 p = int2(threadId);
  const float4 input = InColor[p];
  if (cb.mode == 1)
  {
    InOutColor[p] = float4(input.rgb + InHistory[p].rgb, InOutColor[p].a);
    return;
  }
  const float4 output = InOutColor[p];
  float3 change = finiteChange(output.rgb - input.rgb);

  float3 lo = change, hi = change;
  for (int y = -1; y <= 1; ++y)
  {
    for (int x = -1; x <= 1; ++x)
    {
      const float3 c = changeAt(p + int2(x, y));
      lo = min(lo, c);
      hi = max(hi, c);
    }
  }

  const float2 uv = (float2(threadId) + 0.5f) / float2(cb.colorSize);
  const uint2 motionTexel = min(uint2(uv * float2(cb.motionSize)), cb.motionSize - uint2(1, 1));
  const float2 historyUv = uv + InMotionVector[motionTexel] / float2(cb.motionSize);
  if (cb.historyValid != 0 && all(historyUv >= 0.0f) && all(historyUv <= 1.0f))
  {
    change = lerp(clamp(historyAt(historyUv), lo, hi), change, cb.currentWeight);
  }
  OutHistory[p] = float4(change, 1.0f);
}
