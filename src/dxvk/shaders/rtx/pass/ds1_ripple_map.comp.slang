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

// Fork (DS1): the ripple field's water map (fork_ds1_ripples.h). Each cell's water height is the first DS1
// water surface (textureFlags bit 26) straight below the ray top; cells without one hold no water. Cells
// that were outside last frame's window start at rest. Thread (0, 0) also probes under the player: they
// wade when the water there lies above the first solid ground (characters do not count as ground).

#include "rtx/pass/fork_ds1_ripples.h"
#include "rtx/pass/instance_definitions.h"

layout(binding = DS1_RIPPLE_BINDING_TLAS)
RaytracingAccelerationStructure topLevelAS;

layout(binding = DS1_RIPPLE_BINDING_SURFACES)
StructuredBuffer<uint4> surfaceWords;

layout(binding = DS1_RIPPLE_BINDING_HEIGHT)
RWTexture2D<float> height;

layout(binding = DS1_RIPPLE_BINDING_HEIGHT_PREV)
RWTexture2D<float> heightPrev;

layout(binding = DS1_RIPPLE_BINDING_WATER)
RWTexture2D<float> water;

layout(binding = DS1_RIPPLE_BINDING_PROBE)
RWTexture2D<float4> probe;

layout(push_constant)
ConstantBuffer<Ds1RippleMapArgs> cb;

uint surfaceFlags(uint instanceId, uint geometryIndex)
{
  const uint surfaceIndex = (instanceId & CUSTOM_INDEX_SURFACE_MASK) + geometryIndex;
  return surfaceWords[surfaceIndex * DS1_RIPPLE_SURFACE_UVEC4S + 13].z;
}

// The distance down to the first DS1 water surface, or -1.
float traceWater(vec3 origin, float length)
{
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = vec3(0.0f, -1.0f, 0.0f);
  ray.TMin = 0.0f;
  ray.TMax = length;

  // Blended or glass surfaces above the water may come first; a few layers at most.
  for (uint layer = 0; layer < 4; ++layer)
  {
    RayQuery<0> rayQuery = RayQuery<0>();
    rayQuery.TraceRayInline(topLevelAS, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, OBJECT_MASK_TRANSLUCENT, ray);
    while (rayQuery.Proceed())
    {
      if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
          (surfaceFlags(rayQuery.CandidateInstanceID(), rayQuery.CandidateGeometryIndex()) & DS1_RIPPLE_WATER_FLAG) != 0)
      {
        rayQuery.CommitNonOpaqueTriangleHit();
      }
    }
    if (rayQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
      return -1.0f;
    }
    const float hitT = rayQuery.CommittedRayT();
    if ((surfaceFlags(rayQuery.CommittedInstanceID(), rayQuery.CommittedGeometryIndex()) & DS1_RIPPLE_WATER_FLAG) != 0)
    {
      return hitT;
    }
    ray.TMin = hitT + 1e-3f;
  }
  return -1.0f;
}

// The distance down to the first opaque surface that is not a character, or -1.
float traceGround(vec3 origin, float length)
{
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = vec3(0.0f, -1.0f, 0.0f);
  ray.TMin = 0.0f;
  ray.TMax = length;

  // The player's own body and anything they carry lie in the way first.
  for (uint layer = 0; layer < 6; ++layer)
  {
    RayQuery<0> rayQuery = RayQuery<0>();
    rayQuery.TraceRayInline(topLevelAS, RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, OBJECT_MASK_OPAQUE, ray);
    while (rayQuery.Proceed())
    {
    }
    if (rayQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
      return -1.0f;
    }
    const float hitT = rayQuery.CommittedRayT();
    if ((surfaceFlags(rayQuery.CommittedInstanceID(), rayQuery.CommittedGeometryIndex()) & DS1_RIPPLE_SKINNED_FLAG) == 0)
    {
      return hitT;
    }
    ray.TMin = hitT + 1e-3f;
  }
  return -1.0f;
}

int2 wrapTexel(int2 cell, int n)
{
  return ((cell % n) + n) % n;
}

[shader("compute")]
[numthreads(DS1_RIPPLE_GROUP_SIZE, DS1_RIPPLE_GROUP_SIZE, 1)]
void main(uint2 thread : SV_DispatchThreadID)
{
  const int n = int(cb.gridSize);
  if (thread.x >= cb.gridSize || thread.y >= cb.gridSize)
  {
    return;
  }

  const int2 cell = int2(cb.origin.x, cb.origin.y) + int2(thread);
  const int2 texel = wrapTexel(cell, n);
  const int2 previousOrigin = int2(cb.previousOrigin.x, cb.previousOrigin.y);
  const bool entered = any(cell < previousOrigin) || any(cell >= previousOrigin + n);
  if (cb.resetAll != 0 || entered)
  {
    height[texel] = 0.0f;
    heightPrev[texel] = 0.0f;
  }

  const vec3 top = vec3((float(cell.x) + 0.5f) * cb.cellSize, cb.rayTop, (float(cell.y) + 0.5f) * cb.cellSize);
  const float waterT = traceWater(top, cb.rayLength);
  water[texel] = waterT >= 0.0f ? cb.rayTop - waterT : DS1_RIPPLE_NO_WATER;

  if (thread.x == 0 && thread.y == 0)
  {
    const float probeWater = traceWater(cb.probe, cb.rayLength);
    const float probeGround = traceGround(cb.probe, cb.rayLength);
    // Ground under the water, a couple of centimetres down at least (wet floors are no water to wade).
    const bool wading = probeWater >= 0.0f && (probeGround < 0.0f || probeGround > probeWater + 0.02f);
    const float wasWading = cb.resetAll != 0 ? 0.0f : probe[int2(0, 0)].x;
    probe[int2(0, 0)] = float4(wading ? 1.0f : 0.0f, wasWading,
                               probeWater >= 0.0f ? cb.probe.y - probeWater : DS1_RIPPLE_NO_WATER,
                               probeGround >= 0.0f ? cb.probe.y - probeGround : DS1_RIPPLE_NO_WATER);
  }
}
