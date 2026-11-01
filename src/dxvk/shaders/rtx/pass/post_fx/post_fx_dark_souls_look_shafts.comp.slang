// Fork: Dark Souls' light shafts, the game's own screen-space filter (see post_fx_dark_souls_look.h).
//
// One FRPG_Fil_LightShaft pass (FRPG_PostProcess.fx): each pixel gathers 10 taps of the input along the line
// towards the shaft sun's screen position, each weighted by a decay that shrinks per tap, the line clamped to
// maxLength pixels of the input. The game runs it twice on its blurred bright pass, (100, 0.04, 0.434) then
// (10, 1, 0.92), and ColAdj adds the result times c56.y.

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/utility/common.slangh"

layout(binding = POST_FX_DS_LOOK_SHAFTS_INPUT)
Sampler2D<float4> Input;

layout(binding = POST_FX_DS_LOOK_SHAFTS_OUTPUT)
RWTexture2D<float4> Output;

layout(binding = POST_FX_DS_LOOK_SHAFTS_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookShaftArgs> cb;

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(pixel >= cb.outputSize))
  {
    return;
  }

  const vec2 uv = (vec2(pixel) + 0.5f) * cb.invOutputSize;
  // The step towards the sun in pixels (c12.xy), at most maxLength (c70.x) over all taps, back to uv (c12.zw).
  vec2 delta = (uv - cb.sunUv) * vec2(cb.outputSize);
  const float length = max(sqrt(dot(delta, delta)), 1e-4f);
  delta *= min(cb.maxLength, length) / length;
  delta *= cb.invOutputSize;

  vec3 colour = vec3(0.0f);
  float decay = 1.0f;
  vec2 tap = uv;
  [unroll]
  for (int i = 0; i < 10; i++)
  {
    colour += Input.SampleLevel(tap, 0).rgb * decay;
    tap -= delta * 0.1f;
    decay *= cb.decay;
  }
  Output[pixel] = vec4(colour * cb.weight, 1.0f);
}
