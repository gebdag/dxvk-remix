// Fork: FRPG_Fil_HDR_ColAdj on the DS1 scene value (see post_fx_dark_souls_look.h):
//   col = sat(scene * c56.z) + bloom.rgb * lerp(c56.x, c71.y, bloom.a) + lightShaft * c56.y
//   out = sat(dot((col, 1), c62..c64))      (an 8-bit target)
// with the light shafts from the game's LightShaft chain (post_fx_dark_souls_look_shafts.comp.slang).
// The result is the displayed value; it goes back out as value^2.2 so the final output pass
// (linearToGamma, x^(1/2.2)) returns it unchanged.

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/utility/common.slangh"
#include "rtx/pass/tonemap/tonemapping.slangh"  // hdrRollOff (HDR10 output)

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

// One graded channel, y = dot((col, 1), row), with the row's black offset b = row.w. Below b < 0 the game clips everything
// darker than -b before the offset to black. The toe instead takes u = y - b, the value above the row's own black, and from
// u = 0 to u = -2b follows u^2 / (-4b): black stays black, what clipped exactly at black comes out at -b/4, and at u = -2b it
// meets the line with the same value and slope, so everything brighter is the game's grade unchanged.
// Only for dark pixels: the grade's rows carry its saturation (negative off-diagonal terms), so a bright saturated colour can
// put another channel in that range, which the game clips and a toe would grey. The toe fades out between the brightness of a
// grey at the knee (its brightest channel, before the grade) and twice that.
float toeChannel(float y, vec4 row, float brightest, float strength)
{
  const float b = row.w;
  const float clipped = max(y, 0.0f);
  if (b >= 0.0f || strength <= 0.0f)
  {
    return clipped;
  }
  const float kneeIn = -2.0f * b / max(row.x + row.y + row.z, 1e-3f);
  strength *= 1.0f - smoothstep(kneeIn, 2.0f * kneeIn, brightest);
  const float u = max(y - b, 0.0f);
  const float knee = -2.0f * b;
  const float toe = u < knee ? u * u / (2.0f * knee) : y;
  return lerp(clipped, toe, strength);
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(pixel >= cb.imageSize))
  {
    return;
  }

  const vec4 frame = InOutColor[pixel];
  // DS1 HDR10 output (keepHeadroom): ColAdj's clips are left out, so what they cut above white reaches the
  // display; within 0..1 nothing changes.
  const bool keep = cb.keepHeadroom != 0;
  vec3 col = frame.rgb * cb.sceneScale * cb.adjustExposure;
  col = keep ? max(col, vec3(0.0f)) : saturate(col);
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
  const float brightest = max(col.r, max(col.g, col.b));
  vec3 graded = vec3(toeChannel(dot(c, cb.gradeRowR), cb.gradeRowR, brightest, cb.blackToe),
                     toeChannel(dot(c, cb.gradeRowG), cb.gradeRowG, brightest, cb.blackToe),
                     toeChannel(dot(c, cb.gradeRowB), cb.gradeRowB, brightest, cb.blackToe));
  // The game's output clip is per channel at 1 (its 8-bit target). With headroom the range it would cut
  // rolls off from the knee towards the display's limit instead (hdrRollOff); at or below the knee the
  // value is the SDR value exactly.
  graded = keep ? hdrRollOff(graded, cb.hdrKneeGamma, cb.hdrLimitGamma, cb.hdrPerChannel) : saturate(graded);
  // Image adjustments (rtx.ds1Look.adjust*), on the displayed value; skipped entirely at their defaults.
  if (cb.adjustEnable != 0)
  {
    graded = graded * cb.adjustGain;
    graded = pow(max(graded, vec3(0.0f)), vec3(1.0f / max(cb.adjustGamma, 0.01f)));
    graded = cb.adjustLift + (1.0f - cb.adjustLift) * graded;
    graded = (graded - 0.5f) * cb.adjustContrast + 0.5f;
    const float luma = dot(graded, vec3(0.2126f, 0.7152f, 0.0722f));
    graded = lerp(vec3(luma), graded, cb.adjustSaturation);
    graded = keep ? hdrRollOff(graded, cb.hdrKneeGamma, cb.hdrLimitGamma, cb.hdrPerChannel) : saturate(graded);
  }
  InOutColor[pixel] = vec4(pow(graded, vec3(2.2f)), frame.a);
}
