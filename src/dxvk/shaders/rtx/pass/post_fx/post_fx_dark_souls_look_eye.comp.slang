// Fork: the eye's local adaptation level (see post_fx_dark_souls_look.h): a coarse map of how bright each part of the view is,
// as the scene value the game's exposure gives it, excluding the sky. The scene pass lifts what lies in the dark parts of it.
// A geometric mean over a wide footprint, followed over time like an eye: quickly towards brighter, slowly towards darker.

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/utility/common.slangh"

layout(binding = POST_FX_DS_LOOK_EYE_FRAME)
Texture2D<float4> Frame;

layout(binding = POST_FX_DS_LOOK_EYE_LINEAR_VIEW_Z)
Texture2D<float> LinearViewZ;

layout(binding = POST_FX_DS_LOOK_EYE_HISTORY)
Texture2D<float2> History;

// r: this part of the view's level. g: all the view's surfaces', the mean of last frame's parts in log.
layout(binding = POST_FX_DS_LOOK_EYE_OUTPUT)
RWTexture2D<float2> Output;

layout(binding = POST_FX_DS_LOOK_EYE_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookEyeArgs> cb;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint2 texel : SV_DispatchThreadID)
{
  if (any(texel >= cb.outputSize))
  {
    return;
  }

  // A regular grid over a footprint five map texels wide, so a still view gives a still map.
  const int kTaps = 12;
  const float kFootprint = 5.0f;
  float logSum = 0.0f;
  float count = 0.0f;
  for (int y = 0; y < kTaps; ++y)
  {
    for (int x = 0; x < kTaps; ++x)
    {
      const vec2 offset = (vec2(x, y) + 0.5f) / float(kTaps) - 0.5f;
      const vec2 uv = (vec2(texel) + 0.5f + offset * kFootprint) / vec2(cb.outputSize);
      if (any(uv < 0.0f) || any(uv >= 1.0f))
      {
        continue;
      }
      if (LinearViewZ[uint2(uv * vec2(cb.viewZSize))] == cb.missLinearViewZ)
      {
        continue;
      }
      const vec3 radiance = max(Frame[uint2(uv * vec2(cb.frameSize))].rgb, vec3(0.0f));
      const float luminance = dot(radiance, vec3(0.2126f, 0.7152f, 0.0722f));
      // One bad pixel must not reach the history, and through the whole view's level all of the screen.
      if (isnan(luminance) || isinf(luminance))
      {
        continue;
      }
      logSum += log(pow(luminance, 1.0f / 2.2f) * cb.exposure + 0.01f);
      count += 1.0f;
    }
  }
  const float level = count > 0.0f ? exp(logSum / count) : 1.0f;

  float adapted = level;
  float overall = level;
  if (cb.historyValid != 0)
  {
    const float previous = History[texel].r;
    adapted = lerp(previous, level, level > previous ? cb.riseBlend : cb.fallBlend);
    float overallLog = 0.0f;
    for (uint y = 0; y < 9; ++y)
    {
      for (uint x = 0; x < 16; ++x)
      {
        overallLog += log(max(History[uint2(x * 4u + 2u, y * 4u + 2u)].r, 1e-3f));
      }
    }
    overall = exp(overallLog / 144.0f);
  }
  // All sky: no surface to adapt to. It takes the view's level at once, so it weighs on neither side of the whole view's mean of surfaces.
  Output[texel] = float2(count > 0.0f ? adapted : overall, overall);
}
