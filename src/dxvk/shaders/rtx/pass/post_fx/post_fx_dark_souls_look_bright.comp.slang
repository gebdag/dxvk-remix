// Fork: FRPG_Fil_BrightPassFilter on the DS1 scene value (see post_fx_dark_souls_look.h).
//
// The game point-samples its scene into the bloom base buffer with one bilinear tap per
// texel (FRPG_Fil_Dof_DownSample), then
//   rate      = sat((viewZ - c71.z) / (c71.w - c71.z))
//   threshold = lerp(c11.x, c71.x, rate)
//   out.rgb   = max(scene - threshold, 0) / (1 - threshold),  out.a = rate
// Here each texel averages the whole footprint it covers instead of one tap: the same value
// in expectation, without the tap's crawl when the path-traced frame jitters.
// With skySource the scene is the light shaft source (post_fx_dark_souls_look.h).

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

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 texel : SV_DispatchThreadID)
{
  if (any(texel >= cb.outputSize))
  {
    return;
  }

  uint2 viewZSize;
  LinearViewZ.GetDimensions(viewZSize.x, viewZSize.y);

  const uint2 begin = texel * cb.sceneSize / cb.outputSize;
  const uint2 end = max((texel + 1) * cb.sceneSize / cb.outputSize, begin + 1);
  vec3 sum = vec3(0.0f);
  for (uint y = begin.y; y < end.y; ++y)
  {
    for (uint x = begin.x; x < end.x; ++x)
    {
      if (cb.skySource == 0)
      {
        sum += Scene[uint2(x, y)].rgb;
        continue;
      }
      // The game's shaft buffer is cleared to black and its sky depth-tested against a copy of the
      // scene's depth: only where the view sees the sky does the sky count.
      const vec2 pixelUv = (vec2(x, y) + 0.5f) / vec2(cb.sceneSize) + cb.depthJitterUv;
      const uint2 pixelViewZ = min(uint2(max(pixelUv, 0.0f) * vec2(viewZSize)), viewZSize - 1);
      if (LinearViewZ[pixelViewZ] == cb.missLinearViewZ)
      {
        sum += saturate(pow(cb.skyBrightness * ds1SkyRadiance(cb.skyGamma, cb.skyUnexpose, Scene[uint2(x, y)].rgb),
                            vec3(1.0f / 2.2f)) * cb.exposure);
      }
    }
  }
  const vec3 scene = sum / float((end.x - begin.x) * (end.y - begin.y));
  const vec2 uv = (vec2(texel) + 0.5f) / vec2(cb.outputSize) + cb.depthJitterUv;
  const uint2 viewZTexel = min(uint2(max(uv, 0.0f) * vec2(viewZSize)), viewZSize - 1);
  const float viewZ = LinearViewZ[viewZTexel];
  // A miss is the sky, at the game's far plane: full rate.
  const float rate = viewZ == cb.missLinearViewZ ? 1.0f
    : saturate((abs(viewZ) - cb.depthBegin) / max(cb.depthEnd - cb.depthBegin, 1e-6f));

  const float threshold = lerp(cb.thresholdNear, cb.thresholdFar, rate);
  Output[texel] = vec4(max(scene - threshold, vec3(0.0f)) / max(1.0f - threshold, 1e-6f), rate);
}
