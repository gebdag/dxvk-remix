// Fork: FRPG_Fil_Dof (see post_fx_dark_souls_look.h). The blur rate is the larger of the far rate
// in the scene's alpha and the dilated near rate; c66/c67 turn it into blend weights over four
// levels: the scene, its downsample, that unfocused, and the half-size unfocus brought back up.
//   w   = sat(rate * c66 + c67)
//   out = scene * w.x + level1 * (w.y - w.x) + level2 * (w.z - w.y) + level3 * (w.w - w.z)

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/utility/common.slangh"

layout(binding = POST_FX_DS_LOOK_DOF_INOUT)
RWTexture2D<float4> InOutColor;

layout(binding = POST_FX_DS_LOOK_DOF_LEVEL1)
Sampler2D<float4> Level1;

layout(binding = POST_FX_DS_LOOK_DOF_LEVEL2)
Sampler2D<float4> Level2;

layout(binding = POST_FX_DS_LOOK_DOF_LEVEL3)
Sampler2D<float4> Level3;

layout(binding = POST_FX_DS_LOOK_DOF_NEAR)
Sampler2D<float4> Near;

layout(binding = POST_FX_DS_LOOK_DOF_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookDofArgs> cb;

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(pixel >= cb.imageSize))
  {
    return;
  }

  const vec2 uv = (vec2(pixel) + 0.5f) * cb.invImageSize;
  const vec4 scene = InOutColor[pixel];
  const float rate = max(scene.a, Near.SampleLevel(uv, 0).a);
  const vec4 w = saturate(rate * cb.blendScale + cb.blendBias);
  const vec3 col = scene.rgb * w.x
    + Level1.SampleLevel(uv, 0).rgb * (w.y - w.x)
    + Level2.SampleLevel(uv, 0).rgb * (w.z - w.y)
    + Level3.SampleLevel(uv, 0).rgb * (w.w - w.z);
  InOutColor[pixel] = vec4(col, scene.a);
}
