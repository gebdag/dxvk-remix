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

// Dark Souls 1 color grading compute shader.
// Faithfully recreates the ToneCorrectBank math from FRPG_Fil_HDR_ColAdj.fpo:
//   1. Saturation  — lerp from luma to color using Rec.709 weights
//   2. Contrast    — (x - 0.5) * contrast + 0.5, per channel
//   3. Brightness  — multiply per channel
// All three ops match the 4x4 color matrix (c62-c65) the original shader builds at runtime.

#include "rtx/pass/post_fx/post_fx_dark_souls.h"

layout(binding = POST_FX_DS_INOUT)
RWTexture2D<float4> InOutColor;

layout(push_constant)
ConstantBuffer<PostFxDarkSoulsArgs> cb;

// Rec.709 luminance weights — same constants hardcoded in FRPG_Fil_SampleLumInitial.fpo
static const float3 kLuma = float3(0.2125f, 0.7154f, 0.0721f);

float3 applyDS1Grading(float3 color)
{
  // 1. Saturation
  float luma = dot(color, kLuma);
  color = lerp(float3(luma, luma, luma), color, cb.saturation);

  // 2. Per-channel contrast: (x - 0.5) * contrast + 0.5
  color.r = (color.r - 0.5f) * cb.contrastR + 0.5f;
  color.g = (color.g - 0.5f) * cb.contrastG + 0.5f;
  color.b = (color.b - 0.5f) * cb.contrastB + 0.5f;

  // 3. Per-channel brightness
  color.r *= cb.brightnessR;
  color.g *= cb.brightnessG;
  color.b *= cb.brightnessB;

  return color;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint2 pixelPos : SV_DispatchThreadID)
{
  if (any(pixelPos >= cb.imageSize))
    return;

  float3 original = InOutColor[pixelPos].rgb;
  float3 result = lerp(original, applyDS1Grading(original), cb.blendWeight);
  InOutColor[pixelPos] = float4(result, InOutColor[pixelPos].a);
}
