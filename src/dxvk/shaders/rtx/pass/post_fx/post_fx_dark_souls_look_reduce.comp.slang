// Fork: FRPG_Fil_Dof_DownSample from the full frame (see post_fx_dark_souls_look.h). The game takes
// one bilinear tap per output texel; this averages the whole footprint, the same value in
// expectation without the tap's crawl on a jittered path-traced frame. rgba alike (alpha = DoF rate).

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/utility/common.slangh"

layout(binding = POST_FX_DS_LOOK_REDUCE_INPUT)
Texture2D<float4> Input;

layout(binding = POST_FX_DS_LOOK_REDUCE_OUTPUT)
RWTexture2D<float4> Output;

layout(binding = POST_FX_DS_LOOK_REDUCE_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookReduceArgs> cb;

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 texel : SV_DispatchThreadID)
{
  if (any(texel >= cb.outputSize))
  {
    return;
  }

  const uint2 begin = texel * cb.inputSize / cb.outputSize;
  const uint2 end = max((texel + 1) * cb.inputSize / cb.outputSize, begin + 1);
  vec4 sum = vec4(0.0f);
  for (uint y = begin.y; y < end.y; ++y)
  {
    for (uint x = begin.x; x < end.x; ++x)
    {
      sum += Input[uint2(x, y)];
    }
  }
  Output[texel] = sum / float((end.x - begin.x) * (end.y - begin.y));
}
