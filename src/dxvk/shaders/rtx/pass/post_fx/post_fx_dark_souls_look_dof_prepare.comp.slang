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
  Output[pixel] = vec4(Frame[pixel].rgb, Rate[pixel]);
}
