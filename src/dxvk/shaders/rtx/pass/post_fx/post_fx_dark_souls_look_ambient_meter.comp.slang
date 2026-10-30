// Fork: the DS1 ambient meter (see post_fx_dark_souls_look.h): over the meter grid, the gamma-space
// luminance of the path-traced frame against the game's unoccluded ambient on the same surfaces,
// summed per group.

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/utility/common.slangh"
#include "rtx/utility/packing.slangh"

layout(binding = POST_FX_DS_LOOK_AMBIENT_METER_RADIANCE)
Texture2D<float4> Radiance;

layout(binding = POST_FX_DS_LOOK_AMBIENT_METER_ALBEDO)
Texture2D<float4> Albedo;

layout(binding = POST_FX_DS_LOOK_AMBIENT_METER_NORMAL)
Texture2D<uint> ShadingNormal;

layout(binding = POST_FX_DS_LOOK_AMBIENT_METER_LINEAR_VIEW_Z)
Texture2D<float> LinearViewZ;

layout(binding = POST_FX_DS_LOOK_AMBIENT_METER_PARTIALS)
RWStructuredBuffer<float> Partials;

layout(binding = POST_FX_DS_LOOK_AMBIENT_METER_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookAmbientMeterArgs> cb;

groupshared float3 s_sum[DS_LOOK_METER_GROUP * DS_LOOK_METER_GROUP];

uint2 texelAt(float2 uv, uint2 size)
{
  return min(uint2(uv * float2(size)), size - 1);
}

// The game's luminance of a gamma-space colour, ln with its meter's floor.
float lnLuminance(float3 gamma)
{
  const float luminance = dot(gamma, float3(0.2125f, 0.7154f, 0.0721f));
  return luminance - 1e-4f >= 0.0f ? log(luminance) : -9.2103405f;
}

[shader("compute")]
[numthreads(DS_LOOK_METER_GROUP, DS_LOOK_METER_GROUP, 1)]
void main(uint2 cell : SV_DispatchThreadID, uint2 group : SV_GroupID, uint index : SV_GroupIndex)
{
  // x: sum of ln frame, y: sum of ln estimate, z: taps.
  float3 term = 0.0f;
  if (all(cell < cb.gridSize))
  {
    // Four taps per cell, the same points in every buffer.
    for (uint i = 0; i < 4; ++i)
    {
      const float2 uv = (float2(cell) + float2((i & 1) != 0 ? 0.75f : 0.25f, (i & 2) != 0 ? 0.75f : 0.25f)) / float2(cb.gridSize);
      if (LinearViewZ[texelAt(uv, cb.viewZSize)] == cb.missLinearViewZ)
      {
        continue;
      }
      const uint2 g = texelAt(uv, cb.gbufferSize);
      const float3 albedo = Albedo[g].rgb;
      if (dot(albedo, float3(0.2125f, 0.7154f, 0.0721f)) < cb.minAlbedo)
      {
        continue;
      }
      const float3 normal = signedOctahedralToSphereDirection(snorm2x16ToFloat2x32(ShadingNormal[g]));
      // Lambertian under the ambient cube as radiance from every direction: albedo * ambient(N) is
      // the radiance it leaves, the game's albedo * ambient in gamma space raised to 2.2.
      const float3 estimate = albedo * ds1AmbientRadiance(cb.ambient, normal);
      const float3 radiance = max(Radiance[texelAt(uv, cb.radianceSize)].rgb, float3(0.0f));
      term += float3(lnLuminance(pow(radiance, 1.0f / 2.2f)), lnLuminance(pow(max(estimate, float3(0.0f)), 1.0f / 2.2f)), 1.0f);
    }
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
    const uint base = cb.partialsOffset + 3 * (group.y * cb.groupsX + group.x);
    Partials[base + 0] = s_sum[0].x;
    Partials[base + 1] = s_sum[0].y;
    Partials[base + 2] = s_sum[0].z;
  }
}
