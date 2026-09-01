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

// Dark Souls DoF - prepare pass.
//
// Replaces what used to be a raw copyImage of the frame into the post-FX intermediate texture.
// It still stages that copy, but it also computes the circle of confusion once per pixel and packs
// it into the intermediate's alpha channel.
//
// Two reasons.
//
// Stability. The view Z buffer is a point-sampled raster of a DISCONTINUOUS depth field, taken at
// sub-pixel positions that move every frame while an upscaler is active. At a silhouette - and
// especially across alpha-tested foliage - which surface owns a given texel flips from frame to
// frame. Compensating the read position for the jitter (which the main pass still does) corrects
// WHERE we read; it cannot correct WHAT was rasterized, so the fetched depth still flips and the
// CoC flips with it. Averaging the CoC over a small neighbourhood, sized in render-resolution
// texels to cover that one-texel uncertainty, turns the flip into a bounded change in a smooth
// value. DoF transitions want to be soft anyway, so this costs nothing visually.
//
// Cost. The main pass used to fetch depth 49 times per output pixel (once for the centre, once per
// gather tap) and derive a CoC from each. Now the CoC rides along in the alpha of the colour texture
// the gather is already sampling, so those 49 fetches become zero and this pass adds a handful.

#include "rtx/pass/post_fx/post_fx.h"
#include "rtx/pass/post_fx/post_fx_dark_souls_dof.h"

[[vk::binding(POST_FX_DS_DOF_SOURCE)]]
Texture2D<float4> SourceColorTexture;

[[vk::binding(POST_FX_DS_DOF_LINEAR_VIEW_Z)]]
Texture2D<float> LinearViewZTexture;

[[vk::binding(POST_FX_DS_DOF_OUTPUT)]]
RWTexture2D<float4> OutputColorTexture;

[[vk::binding(POST_FX_DS_DOF_LINEAR_SAMPLER)]]
SamplerState LinearSampler;

layout(push_constant)
ConstantBuffer<PostFxDarkSoulsDofArgs> cb;

float sampleViewDistance(float2 uv) {
  return abs(LinearViewZTexture.SampleLevel(LinearSampler, uv + cb.depthJitterUv, 0.0f));
}

float computeFarRate(float depth) {
  if (cb.farMul <= 0.0f || cb.farEnd <= cb.farBegin) {
    return 0.0f;
  }

  const float ramp = saturate((depth - cb.farBegin) / max(cb.farEnd - cb.farBegin, 1e-4f));
  return ramp * saturate(cb.farMul / 100.0f);
}

float computeNearRate(float depth) {
  if (cb.nearMul <= 0.0f) {
    return 0.0f;
  }

  const float nearStart = max(cb.nearBegin, cb.nearEnd);
  const float nearFull = min(cb.nearBegin, cb.nearEnd);
  if (nearStart <= nearFull + 1e-4f) {
    return 0.0f;
  }

  const float ramp = saturate((nearStart - depth) / max(nearStart - nearFull, 1e-4f));
  return ramp * saturate(cb.nearMul / 100.0f);
}

float computeBlurAmount(float farRate, float nearRate) {
  const float blur = saturate(max(farRate, nearRate));
  return pow(blur, max(cb.focusExponent, 0.2f));
}

float cocAt(float2 uv) {
  const float depth = sampleViewDistance(uv);
  return computeBlurAmount(computeFarRate(depth), computeNearRate(depth));
}

[shader("compute")]
[numthreads(POST_FX_TILE_SIZE, POST_FX_TILE_SIZE, 1)]
void main(uint2 pixelPos : SV_DispatchThreadID) {
  if (any(pixelPos >= cb.imageSize)) {
    return;
  }

  const float2 uv = (float2(pixelPos) + 0.5f) * cb.invImageSize;

  float coc = cocAt(uv);

  // Note: the offsets are in view Z texels, not output pixels. The uncertainty being smoothed over
  // is one texel of the RENDER-resolution raster, so at a 2x upscale this is a two-output-pixel
  // neighbourhood - which is why invViewZSize is used here rather than invImageSize.
  if (cb.cocStabilizationTexels > 0.0f) {
    const float2 step = cb.invViewZSize * cb.cocStabilizationTexels;

    coc += cocAt(uv + float2( step.x, 0.0f));
    coc += cocAt(uv + float2(-step.x, 0.0f));
    coc += cocAt(uv + float2(0.0f,  step.y));
    coc += cocAt(uv + float2(0.0f, -step.y));
    coc *= 0.2f;
  }

  const float3 color = SourceColorTexture.SampleLevel(LinearSampler, uv, 0.0f).rgb;

  OutputColorTexture[pixelPos] = float4(color, coc);
}
