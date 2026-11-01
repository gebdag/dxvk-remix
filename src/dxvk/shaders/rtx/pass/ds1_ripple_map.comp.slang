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
// that were outside last frame's window start at rest. In the collision window, a water cell is inside a
// body when rays along +-x and +-z just above the waterline all meet a character (a skinned surface, textureFlags
// bit 29) within reach: the shins, a robe's hem, an enemy wading beside the player.
//
// Shallow water (the game has plenty: a few centimetres over cobbles, rubble or mud) lies at or under the
// ground in places. Just above its waterline the rays started in the floor or met it first, so only the odd
// deeper spot registered a foot, and each such flicker became a point source. So the test runs just above the
// waterline or the ground under the cell, whichever is higher, and only character surfaces count along the
// rays. Ground more than kMaxGroundAboveWater above the water buries it: a character on a ledge or a bridge
// pushes no water below.

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

layout(binding = DS1_RIPPLE_BINDING_HEIGHT_NEXT)
RWTexture2D<float> heightNext;

layout(binding = DS1_RIPPLE_BINDING_WATER)
RWTexture2D<float> water;

layout(binding = DS1_RIPPLE_BINDING_OCCUPANCY)
RWTexture2D<float> occupancy;

layout(binding = DS1_RIPPLE_BINDING_OCCUPANCY_PREV)
RWTexture2D<float> occupancyPrev;

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

// The ground, as a height above the water, may be up to this far above the water for a body there to push it.
static const float kMaxGroundAboveWater = 0.15f;

// Whether a character's surface lies along the ray within reach; the floor, rubble and walls do not count. Both
// faces count: from inside a leg the ray meets its back faces.
bool hitsBody(vec3 origin, vec3 direction)
{
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.0f;
  ray.TMax = cb.bodyReach;

  RayQuery<0> rayQuery = RayQuery<0>();
  rayQuery.TraceRayInline(topLevelAS, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, OBJECT_MASK_OPAQUE, ray);
  while (rayQuery.Proceed())
  {
    if ((surfaceFlags(rayQuery.CandidateInstanceID(), rayQuery.CandidateGeometryIndex()) & DS1_RIPPLE_SKINNED_FLAG) != 0)
    {
      rayQuery.CommitNonOpaqueTriangleHit();
    }
  }
  return rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// The ground's height above the water at a cell (characters' surfaces skipped: the foot standing there), or
// 0 when the ground is below the waterline. Larger than kMaxGroundAboveWater when the water is buried there.
float groundAboveWater(vec3 waterPoint)
{
  RayDesc ray;
  ray.Origin = waterPoint + vec3(0.0f, kMaxGroundAboveWater + 0.01f, 0.0f);
  ray.Direction = vec3(0.0f, -1.0f, 0.0f);
  ray.TMin = 0.0f;
  ray.TMax = kMaxGroundAboveWater + 0.01f;

  RayQuery<0> rayQuery = RayQuery<0>();
  rayQuery.TraceRayInline(topLevelAS, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, OBJECT_MASK_OPAQUE, ray);
  while (rayQuery.Proceed())
  {
    if ((surfaceFlags(rayQuery.CandidateInstanceID(), rayQuery.CandidateGeometryIndex()) & DS1_RIPPLE_SKINNED_FLAG) == 0)
    {
      rayQuery.CommitNonOpaqueTriangleHit();
    }
  }
  if (rayQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
  {
    return 0.0f;
  }
  // A hit at the very top means the ray started inside solid ground: deeper than the limit.
  const float t = rayQuery.CommittedRayT();
  return t <= 1e-4f ? kMaxGroundAboveWater + 1.0f : kMaxGroundAboveWater + 0.01f - t;
}

bool insideBody(vec3 position)
{
  return hitsBody(position, vec3(1.0f, 0.0f, 0.0f)) && hitsBody(position, vec3(-1.0f, 0.0f, 0.0f)) &&
         hitsBody(position, vec3(0.0f, 0.0f, 1.0f)) && hitsBody(position, vec3(0.0f, 0.0f, -1.0f));
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
  const bool entered = cb.resetAll != 0 || any(cell < previousOrigin) || any(cell >= previousOrigin + n);
  if (entered)
  {
    height[texel] = 0.0f;
    heightPrev[texel] = 0.0f;
    heightNext[texel] = 0.0f;
  }

  const vec3 top = vec3((float(cell.x) + 0.5f) * cb.cellSize, cb.rayTop, (float(cell.y) + 0.5f) * cb.cellSize);
  const float waterT = traceWater(top, cb.rayLength);
  const float waterHeight = waterT >= 0.0f ? cb.rayTop - waterT : DS1_RIPPLE_NO_WATER;
  water[texel] = waterHeight;

  // Just above the waterline or the ground, whichever is higher: the water surface itself is not opaque and does
  // not count anyway.
  const int2 collisionCell = int2(thread) - int2(cb.collisionMin.x, cb.collisionMin.y);
  const bool inCollision = all(collisionCell >= 0) && all(collisionCell < int(cb.collisionSize));
  float body = 0.0f;
  if (inCollision && waterT >= 0.0f)
  {
    const float ground = groundAboveWater(vec3(top.x, waterHeight, top.z));
    if (ground <= kMaxGroundAboveWater && insideBody(vec3(top.x, waterHeight + ground + 0.02f, top.z)))
    {
      body = 1.0f;
    }
  }
  occupancy[texel] = body;
  // A body already standing in a cell that just entered the window has pushed no water yet.
  if (entered)
  {
    occupancyPrev[texel] = body;
  }
}
