/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

// Fork (DS1): one ray from the player's head towards the player light. When a surface lies in the
// way, the light moves back along the ray to stay short of it (by its emitter radius and the margin),
// so a player backed into a wall keeps the light on the room's side. Characters do not count: the
// ray passes through skinned surfaces (the player's own hair, hood, a shield worn on the back). The
// light buffer entry is a sphere light: data0.xyz its position, data0.w's low half its radius
// (RtSphereLight::writeGPUData).

#include "rtx/pass/ds1_player_light_clearance.h"
#include "rtx/pass/instance_definitions.h"

layout(binding = DS1_PLC_BINDING_TLAS)
RaytracingAccelerationStructure topLevelAS;

layout(binding = DS1_PLC_BINDING_SURFACES)
StructuredBuffer<uint4> surfaceWords;

layout(binding = DS1_PLC_BINDING_LIGHTS)
RWStructuredBuffer<uint4> lightWords;

layout(push_constant)
ConstantBuffer<Ds1PlayerLightClearanceArgs> cb;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
  // Four uint4 per light (MemoryPolymorphicLight).
  const uint first = cb.lightIndex * 4;
  uint4 data0 = lightWords[first];
  const vec3 target = asfloat(data0.xyz);
  const float radius = f16tof32(data0.w & 0xFFFFu);

  const vec3 toTarget = target - cb.anchor;
  const float distance = length(toTarget);
  if (!(distance > cb.ignore) || distance > cb.maxOffset)
  {
    return;
  }
  const vec3 direction = toTarget / distance;

  RayDesc ray;
  ray.Origin = cb.anchor;
  ray.Direction = direction;
  ray.TMin = cb.ignore;
  ray.TMax = distance + radius + cb.margin;

  // Opaque surfaces seen from the head's side; cutouts and blended surfaces (foliage, grates, glass)
  // let light through and do not count. A few character layers at most lie on the way out.
  for (uint layer = 0; layer < 4; ++layer)
  {
    RayQuery<0> rayQuery = RayQuery<0>();
    rayQuery.TraceRayInline(topLevelAS, RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_CULL_NON_OPAQUE,
                            OBJECT_MASK_OPAQUE, ray);
    while (rayQuery.Proceed())
    {
    }
    if (rayQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
      return;
    }

    const float hitT = rayQuery.CommittedRayT();
    const uint surfaceIndex = (rayQuery.CommittedInstanceID() & CUSTOM_INDEX_SURFACE_MASK) + rayQuery.CommittedGeometryIndex();
    if (surfaceWords[surfaceIndex * DS1_PLC_SURFACE_UVEC4S + 15].w == DS1_PLC_SKINNED_TAG)
    {
      ray.TMin = hitT + 1e-3f;
      continue;
    }

    const float reach = max(hitT - radius - cb.margin, 0.0f);
    if (reach < distance)
    {
      data0.xyz = asuint(cb.anchor + direction * reach);
      lightWords[first] = data0;
    }
    return;
  }
}
