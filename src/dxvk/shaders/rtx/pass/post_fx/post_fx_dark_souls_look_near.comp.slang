// Fork: FRPG_Fil_Dof_NearRate then FRPG_Fil_Dof_UnfocusNearRate3x3 (see post_fx_dark_souls_look.h):
// the near DoF rate sat((viewZ - c10.x) / (c10.y - c10.x)) * c10.z at the texel and its four
// Unfocus3x3 neighbours, taking the largest (the game dilates the rate it wrote).

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/utility/common.slangh"

layout(binding = POST_FX_DS_LOOK_NEAR_LINEAR_VIEW_Z)
Texture2D<float> LinearViewZ;

layout(binding = POST_FX_DS_LOOK_NEAR_OUTPUT)
RWTexture2D<float4> Output;

layout(binding = POST_FX_DS_LOOK_NEAR_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookNearArgs> cb;

float nearRate(vec2 uv)
{
  uint2 size;
  LinearViewZ.GetDimensions(size.x, size.y);
  const uint2 t = min(uint2(clamp(uv + cb.depthJitterUv, 0.0f, 1.0f) * vec2(size)), size - 1);
  const float viewZ = LinearViewZ[t];
  if (viewZ == cb.missLinearViewZ)
  {
    return saturate((1e9f - cb.nearBegin) / (cb.nearEnd - cb.nearBegin)) * cb.nearStrength;
  }
  const float d = cb.nearEnd - cb.nearBegin;
  return saturate((abs(viewZ) - cb.nearBegin) / (abs(d) > 1e-6f ? d : 1e-6f)) * cb.nearStrength;
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 texel : SV_DispatchThreadID)
{
  if (any(texel >= cb.outputSize))
  {
    return;
  }

  const vec2 texelSize = 1.0f / vec2(cb.outputSize);
  const vec2 uv = (vec2(texel) + 0.5f) * texelSize;
  // The Unfocus3x3 tap pattern (its vertex shader): (1, 0.28) and that rotated by 90 degrees.
  float rate = nearRate(uv);
  rate = max(rate, nearRate(uv + vec2(1.0f, 0.28f) * texelSize));
  rate = max(rate, nearRate(uv - vec2(1.0f, 0.28f) * texelSize));
  rate = max(rate, nearRate(uv + vec2(0.28f, -1.0f) * texelSize));
  rate = max(rate, nearRate(uv - vec2(0.28f, -1.0f) * texelSize));
  Output[texel] = vec4(0.0f, 0.0f, 0.0f, rate);
}
