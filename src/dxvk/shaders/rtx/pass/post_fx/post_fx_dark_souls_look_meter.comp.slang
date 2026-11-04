// Fork: the DS1 luminance meter (see post_fx_dark_souls_look.h): FRPG_Fil_SampleLumInitial's
// per-sample term over the scene pass output on the game's meter grid, summed per group.

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/utility/common.slangh"

layout(binding = POST_FX_DS_LOOK_METER_SCENE)
Texture2D<float4> Scene;

// g: what the eye's lift added to a pixel, which is not the frame's light.
layout(binding = POST_FX_DS_LOOK_METER_LIFT)
Texture2D<float2> Lift;

layout(binding = POST_FX_DS_LOOK_METER_PARTIALS)
RWStructuredBuffer<float> Partials;

layout(binding = POST_FX_DS_LOOK_METER_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookMeterArgs> cb;

groupshared float s_sum[DS_LOOK_METER_GROUP * DS_LOOK_METER_GROUP];

[shader("compute")]
[numthreads(DS_LOOK_METER_GROUP, DS_LOOK_METER_GROUP, 1)]
void main(uint2 cell : SV_DispatchThreadID, uint2 group : SV_GroupID, uint index : SV_GroupIndex)
{
  float term = 0.0f;
  if (all(cell < cb.gridSize))
  {
    // The cell's footprint in the scene: the game's downsample to the grid size.
    const uint2 p0 = cell * cb.sceneSize / cb.gridSize;
    const uint2 p1 = min(max((cell + 1) * cb.sceneSize / cb.gridSize, p0 + 1), cb.sceneSize);
    float3 sum = 0.0f;
    float lifted = 0.0f;
    for (uint y = p0.y; y < p1.y; ++y)
    {
      for (uint x = p0.x; x < p1.x; ++x)
      {
        sum += Scene[uint2(x, y)].rgb;
        lifted += cb.useLift != 0 ? Lift[uint2(x, y)].g : 0.0f;
      }
    }
    const float area = float((p1.x - p0.x) * (p1.y - p0.y));
    const float3 colour = sum / area;
    // DecodeColor (c56.z * c56.w = 1 / exposure), then the game's luminance and its log floor.
    const float luminance = (dot(colour, float3(0.2125f, 0.7154f, 0.0721f)) - lifted / area) / cb.exposure;
    term = luminance - 1e-4f >= 0.0f ? log(luminance) : -9.2103405f;
  }
  s_sum[index] = term;
  GroupMemoryBarrierWithGroupSync();
  for (uint stride = DS_LOOK_METER_GROUP * DS_LOOK_METER_GROUP / 2; stride > 0; stride >>= 1)
  {
    if (index < stride)
    {
      s_sum[index] += s_sum[index + stride];
    }
    GroupMemoryBarrierWithGroupSync();
  }
  if (index == 0)
  {
    Partials[group.y * cb.groupsX + group.x] = s_sum[0];
  }
}
