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

// Fork: lifts the render-resolution depth and motion vector guides onto the colour (output) grid DLSS-NR works on.
// The network accepts guides on another grid (each resource is checked against its own size) but does not reproject
// across it: with an upscaler on, its history lags behind the image in motion and it does not accumulate.
//
// Point sampling: a blend across a silhouette would invent depths and velocities that belong to neither side, the
// inputs a temporal reprojection must not get. Depth (NDC) does not depend on the grid; motion vectors are offsets in
// guide pixels and scale with the grid ratio.

#include "rtx/pass/ds1_nr_guides.h"

layout(binding = DS1_NR_GUIDES_BINDING_DEPTH_INPUT)
Texture2D<float> InDepth;

layout(binding = DS1_NR_GUIDES_BINDING_MOTION_VECTOR_INPUT)
Texture2D<float2> InMotionVector;

layout(r32f, binding = DS1_NR_GUIDES_BINDING_DEPTH_OUTPUT)
RWTexture2D<float> OutDepth;

layout(rg16f, binding = DS1_NR_GUIDES_BINDING_MOTION_VECTOR_OUTPUT)
RWTexture2D<float2> OutMotionVector;

layout(push_constant)
ConstantBuffer<Ds1NrGuidesArgs> cb;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  if (any(threadId >= cb.colorSize))
  {
    return;
  }
  // The nearest guide texel to this pixel's centre; clamped for grids that are not an exact integer ratio.
  const float2 guideOverColor = float2(cb.guideSize) / float2(cb.colorSize);
  const uint2 guide = min(uint2((float2(threadId) + 0.5f) * guideOverColor), cb.guideSize - uint2(1, 1));
  OutDepth[threadId] = InDepth[guide];
  OutMotionVector[threadId] = InMotionVector[guide] * cb.motionVectorScale;
}
