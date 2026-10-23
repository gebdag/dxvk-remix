// Fork: the game's bloom filter passes (FRPG_Fil_GaussBlur5x5, FRPG_Fil_Dof_DownSample,
// FRPG_Fil_Bloom): a weighted sum of bilinear taps at offsets in input texels, rgba alike
// (alpha carries the bright pass's depth rate). See post_fx_dark_souls_look.h.

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/utility/common.slangh"

layout(binding = POST_FX_DS_LOOK_FILTER_INPUT)
Sampler2D<float4> Input;

layout(binding = POST_FX_DS_LOOK_FILTER_OUTPUT)
RWTexture2D<float4> Output;

layout(binding = POST_FX_DS_LOOK_FILTER_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookFilterArgs> cb;

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 texel : SV_DispatchThreadID)
{
  if (any(texel >= cb.outputSize))
  {
    return;
  }

  const vec2 uv = (vec2(texel) + 0.5f) * cb.invOutputSize;
  vec4 sum = vec4(0.0f);
  if (cb.gated != 0)
  {
    const vec4 centre = Input.SampleLevel(uv, 0);
    float centreWeight = 1.0f;
    for (uint i = 0; i < cb.tapCount; ++i)
    {
      const vec4 tap = Input.SampleLevel(uv + cb.taps[i].xy * cb.invInputSize, 0);
      const float w = tap.a >= centre.a ? cb.taps[i].z : 0.0f;
      sum += w * tap;
      centreWeight -= w;
    }
    sum += centreWeight * centre;
  }
  else
  {
    for (uint i = 0; i < cb.tapCount; ++i)
    {
      sum += cb.taps[i].z * Input.SampleLevel(uv + cb.taps[i].xy * cb.invInputSize, 0);
    }
  }
  Output[texel] = sum;
}
