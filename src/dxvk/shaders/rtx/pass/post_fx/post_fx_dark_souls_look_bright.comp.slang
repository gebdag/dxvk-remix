// Fork: FRPG_Fil_BrightPassFilter on the DS1 scene value (see post_fx_dark_souls_look.h).
//
// The game point-samples its scene into the bloom base buffer with one bilinear tap per
// texel (FRPG_Fil_Dof_DownSample), then
//   rate      = sat((viewZ - c71.z) / (c71.w - c71.z))
//   threshold = lerp(c11.x, c71.x, rate)
//   out.rgb   = max(scene - threshold, 0) / (1 - threshold),  out.a = rate
// Here each texel averages the whole footprint it covers instead of one tap: the same value
// in expectation, without the tap's crawl when the path-traced frame jitters.

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/utility/common.slangh"

layout(binding = POST_FX_DS_LOOK_BRIGHT_SCENE)
Texture2D<float4> Scene;

layout(binding = POST_FX_DS_LOOK_BRIGHT_LINEAR_VIEW_Z)
Texture2D<float> LinearViewZ;

layout(binding = POST_FX_DS_LOOK_BRIGHT_OUTPUT)
RWTexture2D<float4> Output;

layout(binding = POST_FX_DS_LOOK_BRIGHT_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookBrightArgs> cb;

layout(binding = POST_FX_DS_LOOK_BRIGHT_FOG_GATE_MASK)
Texture2D<float2> FogGateMask;

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 texel : SV_DispatchThreadID)
{
  if (any(texel >= cb.outputSize))
  {
    return;
  }

  const uint2 begin = texel * cb.sceneSize / cb.outputSize;
  const uint2 end = max((texel + 1) * cb.sceneSize / cb.outputSize, begin + 1);
  vec3 sum = vec3(0.0f);
  for (uint y = begin.y; y < end.y; ++y)
  {
    for (uint x = begin.x; x < end.x; ++x)
    {
      // The game's 8-bit scene: with HDR output the frame keeps values above 1 (rtx_hdr_output.h), which
      // the bloom must not see, or it would grow around what SDR clipped.
      sum += saturate(Scene[uint2(x, y)].rgb);
    }
  }
  const vec3 scene = sum / float((end.x - begin.x) * (end.y - begin.y));

  uint2 viewZSize;
  LinearViewZ.GetDimensions(viewZSize.x, viewZSize.y);
  const vec2 uv = (vec2(texel) + 0.5f) / vec2(cb.outputSize) + cb.depthJitterUv;
  const uint2 viewZTexel = min(uint2(max(uv, 0.0f) * vec2(viewZSize)), viewZSize - 1);
  const float viewZ = LinearViewZ[viewZTexel];
  // A miss is the sky, at the game's far plane: full rate.
  float rate = viewZ == cb.missLinearViewZ ? 1.0f
    : saturate((abs(viewZ) - cb.depthBegin) / max(cb.depthEnd - cb.depthBegin, 1e-6f));
  if (cb.useFogGateMask != 0)
  {
    const float2 fog = FogGateMask[min((begin + end) / 2, cb.sceneSize - 1)];
    rate = lerp(rate, saturate((fog.y - cb.depthBegin) / max(cb.depthEnd - cb.depthBegin, 1e-6f)), fog.x);
  }

  const float threshold = lerp(cb.thresholdNear, cb.thresholdFar, rate);
  Output[texel] = vec4(max(scene - threshold, vec3(0.0f)) / max(1.0f - threshold, 1e-6f), rate);
}
