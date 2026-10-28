// Fork: FRPG_Fil_Dof_NearRate then FRPG_Fil_Dof_UnfocusNearRate3x3 (see post_fx_dark_souls_look.h):
// the near DoF rate sat((viewZ - c10.x) / (c10.y - c10.x)) * c10.z at the pixel and at its four
// Unfocus3x3 neighbours one half-size DoF texel away (the game's footprint), taking the largest.
// Computed at full resolution.

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/utility/common.slangh"

layout(binding = POST_FX_DS_LOOK_NEAR_LINEAR_VIEW_Z)
Texture2D<float> LinearViewZ;

layout(binding = POST_FX_DS_LOOK_NEAR_OUTPUT)
RWTexture2D<float> Output;

layout(binding = POST_FX_DS_LOOK_NEAR_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookNearArgs> cb;

float nearRateAt(uint2 t)
{
  const float viewZ = LinearViewZ[t];
  if (viewZ == cb.missLinearViewZ)
  {
    return saturate((1e9f - cb.nearBegin) / (cb.nearEnd - cb.nearBegin)) * cb.nearStrength;
  }
  const float d = cb.nearEnd - cb.nearBegin;
  return saturate((abs(viewZ) - cb.nearBegin) / (abs(d) > 1e-6f ? d : 1e-6f)) * cb.nearStrength;
}

// The rate at uv, averaged over the centre and four taps a stabilization step away: view Z is the
// jittered raster, so the surface owning a silhouette texel flips from frame to frame (as in the
// scene pass).
float nearRate(vec2 uv)
{
  uint2 size;
  LinearViewZ.GetDimensions(size.x, size.y);
  const vec2 centre = uv + cb.depthJitterUv;
  const vec2 tapStep = cb.depthStabilizationTexels / vec2(size);
  const uint tapCount = cb.depthStabilizationTexels > 0.0f ? 5 : 1;
  const vec2 tapOffsets[5] = { vec2(0.0f), vec2(1.0f, 0.0f), vec2(-1.0f, 0.0f), vec2(0.0f, 1.0f), vec2(0.0f, -1.0f) };
  float rate = 0.0f;
  for (uint i = 0; i < tapCount; ++i)
  {
    rate += nearRateAt(min(uint2(clamp(centre + tapOffsets[i] * tapStep, 0.0f, 1.0f) * vec2(size)), size - 1));
  }
  return rate / float(tapCount);
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 texel : SV_DispatchThreadID)
{
  if (any(texel >= cb.outputSize))
  {
    return;
  }

  const vec2 uv = (vec2(texel) + 0.5f) / vec2(cb.outputSize);
  const vec2 step = cb.dilationUv;
  // The Unfocus3x3 tap pattern (its vertex shader): (1, 0.28) and that rotated by 90 degrees.
  float rate = nearRate(uv);
  rate = max(rate, nearRate(uv + vec2(1.0f, 0.28f) * step));
  rate = max(rate, nearRate(uv - vec2(1.0f, 0.28f) * step));
  rate = max(rate, nearRate(uv + vec2(0.28f, -1.0f) * step));
  rate = max(rate, nearRate(uv - vec2(0.28f, -1.0f) * step));
  Output[texel] = rate;
}
