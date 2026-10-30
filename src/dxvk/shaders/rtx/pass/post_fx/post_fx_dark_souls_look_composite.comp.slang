// Fork: FRPG_Fil_HDR_ColAdj on the DS1 scene value (see post_fx_dark_souls_look.h):
//   col = sat(scene * c56.z) + bloom.rgb * lerp(c56.x, c71.y, bloom.a) + lightShaft * c56.y
//   out = sat(dot((col, 1), c62..c64))      (an 8-bit target)
// with the light shafts path traced (post_fx_dark_souls_look_shafts.comp.slang).
// The result is the displayed value; it goes back out as value^2.2 so the final output pass
// (linearToGamma, x^(1/2.2)) returns it unchanged.

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/utility/common.slangh"

layout(binding = POST_FX_DS_LOOK_COMPOSITE_INOUT)
RWTexture2D<float4> InOutColor;

layout(binding = POST_FX_DS_LOOK_COMPOSITE_BLOOM)
Sampler2D<float4> Bloom;

layout(binding = POST_FX_DS_LOOK_COMPOSITE_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookCompositeArgs> cb;

layout(binding = POST_FX_DS_LOOK_COMPOSITE_SHAFTS)
Sampler2D<float4> Shafts;

layout(binding = POST_FX_DS_LOOK_COMPOSITE_STOCK)
Texture2D<float4> Stock;

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(pixel >= cb.imageSize))
  {
    return;
  }

  const vec4 frame = InOutColor[pixel];
  vec3 col = saturate(frame.rgb * cb.sceneScale);
  if (cb.bloomEnable != 0)
  {
    const vec4 bloom = Bloom.SampleLevel((vec2(pixel) + 0.5f) * cb.invImageSize, 0);
    col += bloom.rgb * lerp(cb.bloomRateNear, cb.bloomRateFar, bloom.a);
  }
  if (cb.shaftEnable != 0)
  {
    col += Shafts.SampleLevel((vec2(pixel) + 0.5f) * cb.invImageSize, 0).rgb * cb.shaftWeight;
  }
  // rtx.ds1Look.intensity: the game's scene (its exposure, haze and DoF, bloom and shafts) over Remix's own
  // tonemapped frame, in display (gamma) space. The grade below is eased toward identity by it on the CPU
  // (DxvkDarkSoulsLook::dispatchComposite).
  if (cb.intensity < 1.0f)
  {
    const vec3 stock = pow(saturate(Stock[pixel].rgb), vec3(1.0f / 2.2f));
    col = lerp(stock, col, cb.intensity);
  }
  const vec4 c = vec4(col, 1.0f);
  const vec3 graded = saturate(vec3(dot(c, cb.gradeRowR), dot(c, cb.gradeRowG), dot(c, cb.gradeRowB)));
  InOutColor[pixel] = vec4(pow(graded, vec3(2.2f)), frame.a);
}
