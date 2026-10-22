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

// Dark Souls 1 color grading compute shader: FRPG_Fil_HDR_ColAdj's grade.
// The game grades its display colour as it is, gamma encoded: a brightness scale
// (c56.z), then the 4x4 colour matrix c62-c65 (saturation, contrast and
// brightness of the area's ToneCorrectBank row, built by the game at runtime).

#include "rtx/pass/post_fx/post_fx_dark_souls.h"
#include "rtx/utility/common.slangh"
#include "rtx/utility/color.slangh"

layout(binding = POST_FX_DS_INOUT)
RWTexture2D<float4> InOutColor;

layout(push_constant)
ConstantBuffer<PostFxDarkSoulsArgs> cb;

float3 applyDS1Grading(float3 linearColor)
{
  // The same power both ways, so an identity grade leaves the frame unchanged.
  const float3 g = saturate(pow(max(linearColor, float3(0.0f)), float3(1.0f / kSRGBGamma)) * cb.scale);
  const float3 graded = float3(
    dot(cb.rowR.xyz, g) + cb.rowR.w,
    dot(cb.rowG.xyz, g) + cb.rowG.w,
    dot(cb.rowB.xyz, g) + cb.rowB.w);
  return pow(saturate(graded), float3(kSRGBGamma));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint2 pixelPos : SV_DispatchThreadID)
{
  if (any(pixelPos >= cb.imageSize))
    return;

  const float4 original = InOutColor[pixelPos];
  const float3 result = lerp(original.rgb, applyDS1Grading(original.rgb), cb.blendWeight);
  InOutColor[pixelPos] = float4(result, original.a);
}
