// Fork: the DS1 DoF's level source (see post_fx_dark_souls_look.h): the frame as it is when the
// DoF runs, with the far DoF rate from the scene pass in alpha, which the level blurs gate on.

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/utility/common.slangh"

layout(binding = POST_FX_DS_LOOK_DOF_PREPARE_FRAME)
Texture2D<float4> Frame;

layout(binding = POST_FX_DS_LOOK_DOF_PREPARE_RATE)
Texture2D<float> Rate;

layout(binding = POST_FX_DS_LOOK_DOF_PREPARE_OUTPUT)
RWTexture2D<float4> Output;

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  uint2 size;
  Output.GetDimensions(size.x, size.y);
  if (any(pixel >= size))
  {
    return;
  }
  uint2 source;
  Frame.GetDimensions(source.x, source.y);
  if (all(source == size))
  {
    Output[pixel] = vec4(Frame[pixel].rgb, Rate[pixel]);
    return;
  }

  // Half resolution. An edge pixel takes the side that blurs: its largest rate, and the colours of the samples near that rate. A sharp
  // foreground's colour must not leak into the blur behind it, which is what the gated level blurs are there to prevent.
  float rates[4];
  vec3 colours[4];
  float largest = 0.0f;
  for (int i = 0; i < 4; ++i)
  {
    const uint2 at = min(pixel * 2 + uint2(i & 1, i >> 1), source - 1);
    rates[i] = Rate[at];
    colours[i] = Frame[at].rgb;
    largest = max(largest, rates[i]);
  }
  vec3 colour = 0.0f;
  float count = 0.0f;
  for (int i = 0; i < 4; ++i)
  {
    if (rates[i] >= largest * 0.5f)
    {
      colour += colours[i];
      count += 1.0f;
    }
  }
  Output[pixel] = vec4(colour / count, largest);
}
