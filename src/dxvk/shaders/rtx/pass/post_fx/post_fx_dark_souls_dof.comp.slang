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

#include "rtx/pass/post_fx/post_fx.h"
#include "rtx/pass/post_fx/post_fx_dark_souls_dof.h"

[[vk::binding(POST_FX_DS_DOF_SOURCE)]]
Texture2D<float4> SourceColorTexture;

[[vk::binding(POST_FX_DS_DOF_OUTPUT)]]
RWTexture2D<float4> OutputColorTexture;

[[vk::binding(POST_FX_DS_DOF_LINEAR_SAMPLER)]]
SamplerState LinearSampler;

layout(push_constant)
ConstantBuffer<PostFxDarkSoulsDofArgs> cb;

static const int kTapCount = 48;
static const float kGoldenAngle = 2.39996323f;
// Scatter-as-gather softness: how many pixels of slack before a sample's
// blur circle is considered to no longer reach the center pixel.
static const float kScatterSoftness = 1.5f;

// Note: this pass does not read the view Z buffer at all. The circle of confusion is computed once
// per pixel by the prepare pass and arrives in the alpha channel of SourceColorTexture, which is why
// the depth ramp helpers and the jitter compensation live over there.
float hashAngle(uint2 pixelPos) {
  const uint hash = pixelPos.x * 1664525u + pixelPos.y * 1013904223u + 2246822519u;
  return frac(float(hash & 0x00FFFFFFu) / 16777216.0f) * 6.28318531f;
}

float2 spiralTap(int tapIndex, float rotation) {
  const float tapT = (float(tapIndex) + 0.5f) / float(kTapCount);
  const float radius = sqrt(tapT);
  const float angle = rotation + float(tapIndex) * kGoldenAngle;
  return float2(cos(angle), sin(angle)) * radius;
}

float3 sampleDispersionColor(float2 uv, float2 direction, float blurAmount) {
  const float3 base = SourceColorTexture.SampleLevel(LinearSampler, uv, 0.0f).rgb;
  const float dispersionPixels = blurAmount * cb.dispersionSq * 0.12f;
  if (dispersionPixels <= 0.01f) {
    return base;
  }

  const float2 dispersionOffset = direction * dispersionPixels * cb.invImageSize;
  const float red = SourceColorTexture.SampleLevel(LinearSampler, uv + dispersionOffset, 0.0f).r;
  const float blue = SourceColorTexture.SampleLevel(LinearSampler, uv - dispersionOffset, 0.0f).b;
  return float3(red, base.g, blue);
}

[shader("compute")]
[numthreads(POST_FX_TILE_SIZE, POST_FX_TILE_SIZE, 1)]
void main(uint2 pixelPos : SV_DispatchThreadID) {
  if (any(pixelPos >= cb.imageSize)) {
    return;
  }

  const float2 uv = (float2(pixelPos) + 0.5f) * cb.invImageSize;
  // The prepare pass packed the stabilized circle of confusion into alpha, so the whole gather reads
  // colour and CoC from one texture and never touches the view Z buffer. That is both the stability
  // fix (the CoC is smoothed once, over the render-resolution texel the jitter makes uncertain) and
  // a large saving - this used to be 49 depth fetches per output pixel.
  const float4 centerSample = SourceColorTexture.SampleLevel(LinearSampler, uv, 0.0f);
  const float centerBlur = centerSample.a;

  // Diagnostic: show the circle of confusion on its own, before any gathering. The CoC is a pure
  // function of the sampled depth, so if THIS flickers the instability is in the depth signal; if it
  // is rock solid and only the final image dances, the instability is in the gather or in the colour
  // the gather reads. Placed ahead of the early-outs so in-focus regions are shown too.
  if (cb.debugVisualizeCoc != 0) {
    OutputColorTexture[pixelPos] = float4(centerBlur, centerBlur, centerBlur, 1.0f);
    return;
  }

  if (centerBlur <= 0.01f || cb.blendWeight <= 0.0f) {
    OutputColorTexture[pixelPos] = float4(centerSample.rgb, 1.0f);
    return;
  }

  const float radiusPixels = centerBlur * cb.maxBlurRadius;
  if (radiusPixels < 0.5f) {
    OutputColorTexture[pixelPos] = float4(centerSample.rgb, 1.0f);
    return;
  }

  // Center pixel contributes less when heavily blurred so the gather dominates
  const float centerWeight = lerp(1.0f, 0.1f, saturate(centerBlur));
  float3 accumulatedColor = centerSample.rgb * centerWeight;
  float accumulatedWeight = centerWeight;
  const float rotation = hashAngle(pixelPos);

  [loop]
  for (int tapIndex = 0; tapIndex < kTapCount; ++tapIndex) {
    const float2 tap = spiralTap(tapIndex, rotation);
    const float tapDist = length(tap);
    const float tapDistPx = tapDist * radiusPixels;
    const float2 sampleUv = saturate(uv + tap * radiusPixels * cb.invImageSize);
    const float sampleBlur = SourceColorTexture.SampleLevel(LinearSampler, sampleUv, 0.0f).a;

    // Scatter-as-gather: a sample contributes if its own blur circle
    // is large enough to reach this center pixel. In-focus samples
    // (sampleBlur ≈ 0) naturally drop out; far-field samples at any
    // depth contribute if their CoC is large enough.
    const float sampleRadiusPx = sampleBlur * cb.maxBlurRadius;
    const float scatterReach = saturate((sampleRadiusPx - tapDistPx + kScatterSoftness) / kScatterSoftness);

    const float radialWeight = exp(-dot(tap, tap) * 1.6f);
    const float weight = scatterReach * radialWeight;

    if (weight <= 0.0f) {
      continue;
    }

    const float tapLength = max(tapDist, 1e-4f);
    const float3 sampleColor = sampleDispersionColor(sampleUv, tap / tapLength, max(centerBlur, sampleBlur));
    accumulatedColor += sampleColor * weight;
    accumulatedWeight += weight;
  }

  const float3 blurredColor = accumulatedColor / accumulatedWeight;
  const float blend = saturate(cb.blendWeight * centerBlur);
  const float3 finalColor = saturate(lerp(centerSample.rgb, blurredColor, blend));
  // Note: alpha is the packed CoC on the way in, so it must not be written back out.
  OutputColorTexture[pixelPos] = float4(finalColor, 1.0f);
}