/*
* Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved.
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

#include "imgui_bindings.h"

layout(binding = IMGUI_TEXTURE0_INPUT)
Sampler2D<float4> tex0;

layout(push_constant)
ConstantBuffer<ImGuiPushConstants> pc;

// DS1 HDR10 output: the menu is drawn straight onto the swap chain image, which with HDR10 is PQ BT.2020.
// Its colours are gamma-encoded SDR values: decoded, shown at paper white, encoded like the frame
// (dxvk_present_frag_hdr.frag). Alpha blends in PQ space, as the frame's own UI blends in gamma space.
float3 imguiHdrEncode(float3 v)
{
  v = max(v, float3(0.0f));
  float3 lin;
  if (pc.hdrDecodeGamma > 0.0f)
  {
    lin = pow(v, float3(pc.hdrDecodeGamma));
  }
  else
  {
    const float3 lo = v / 12.92f;
    const float3 hi = pow((v + 0.055f) / 1.055f, float3(2.4f));
    lin = lerp(lo, hi, step(float3(0.04045f), v));
  }
  lin *= pc.hdrPaperWhiteNits;
  const float3 bt2020 = float3(
    0.6274040f * lin.r + 0.3292820f * lin.g + 0.0433136f * lin.b,
    0.0690970f * lin.r + 0.9195400f * lin.g + 0.0113612f * lin.b,
    0.0163916f * lin.r + 0.0880132f * lin.g + 0.8955950f * lin.b);
  const float3 y = pow(clamp(bt2020 / 10000.0f, float3(0.0f), float3(1.0f)), float3(0.1593017578125f));
  return pow((0.8359375f + 18.8515625f * y) / (1.0f + 18.6875f * y), float3(78.84375f));
}

[shader("fragment")]
float4 main(float2 UV : TEXCOORD, float4 color : COLOR) : SV_Target
{
    float4 c = color * tex0.Sample(UV);
    if (pc.hdrMode != 0)
    {
      c.rgb = imguiHdrEncode(c.rgb);
    }
    return c;
}
