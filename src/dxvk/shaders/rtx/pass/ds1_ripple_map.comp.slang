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
// bit 29, or a rigid part of one, Surface::isDs1RigidPart) within reach: the shins, a robe's hem, an enemy wading
// beside the player, the head of a club. A blade or a shield's rim is thinner than a cell and encloses none: a cell
// a rigid part's surface passes through is filled too. With rtx.ds1Ripple.enemies off, the surfaces marked as another
// character's than the player's (Surface::isDs1OtherCharacter) move no water; with it on, their bodies may be wider
// (enemyReach): a boss's foot is no leg.
//
// Where a waterfall's sheet (Surface::isDs1FallingWater) stands in a water cell or right beside it, the cell is marked as
// one falling water lands in, and the step stirs it.
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

layout(binding = DS1_RIPPLE_BINDING_TLAS_UNORDERED)
RaytracingAccelerationStructure unorderedAS;

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

// What a surface is to the water. The marks share their bits with the view fade's values, which no character has.
static const uint kNothing = 0;
static const uint kBody = 1;
static const uint kRigidPart = 2;
// Added to either: another character's than the player's.
static const uint kOther = 4;

uint characterKind(uint instanceId, uint geometryIndex, bool countOthers)
{
  const uint record = ((instanceId & CUSTOM_INDEX_SURFACE_MASK) + geometryIndex) * DS1_RIPPLE_SURFACE_UVEC4S;
  const uint marks = surfaceWords[record].w;
  const bool marked = (marks & DS1_RIPPLE_VIEW_FADE_MARK) == 0;
  const bool other = marked && (marks & DS1_RIPPLE_OTHER_CHARACTER_MARK) != 0;
  if (!countOthers && other)
  {
    return kNothing;
  }
  if ((surfaceWords[record + 13].z & DS1_RIPPLE_SKINNED_FLAG) != 0)
  {
    return kBody | (other ? kOther : 0u);
  }
  return marked && (marks & DS1_RIPPLE_RIGID_PART_MARK) != 0 ? (kRigidPart | (other ? kOther : 0u)) : kNothing;
}

bool isFallingWater(uint instanceId, uint geometryIndex)
{
  const uint marks = surfaceWords[((instanceId & CUSTOM_INDEX_SURFACE_MASK) + geometryIndex) * DS1_RIPPLE_SURFACE_UVEC4S].w;
  return (marks & (DS1_RIPPLE_VIEW_FADE_MARK | DS1_RIPPLE_FALLING_WATER_MARK)) == DS1_RIPPLE_FALLING_WATER_MARK;
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

// The distance down to the first swamp surface, or -1. It is opaque, and walkways over it do not hide it.
float traceSwamp(vec3 origin, float length)
{
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = vec3(0.0f, -1.0f, 0.0f);
  ray.TMin = 0.0f;
  ray.TMax = length;

  RayQuery<0> rayQuery = RayQuery<0>();
  rayQuery.TraceRayInline(topLevelAS, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, OBJECT_MASK_OPAQUE, ray);
  while (rayQuery.Proceed())
  {
    if ((surfaceFlags(rayQuery.CandidateInstanceID(), rayQuery.CandidateGeometryIndex()) & DS1_RIPPLE_SWAMP_FLAG) != 0)
    {
      rayQuery.CommitNonOpaqueTriangleHit();
    }
  }
  return rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? rayQuery.CommittedRayT() : -1.0f;
}

// The ground, as a height above the water, may be up to this far above the water for a body there to push it.
static const float kMaxGroundAboveWater = 0.15f;

// Whether a character's surface lies along the ray within reach (only a rigid part's, with partsOnly); the floor, rubble
// and walls do not count. Both faces count: from inside a leg the ray meets its back faces. Another character's surface
// may lie up to otherReach away.
bool hitsBody(vec3 origin, vec3 direction, float reach, float otherReach, bool partsOnly)
{
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.0f;
  ray.TMax = cb.enemies != 0 ? max(reach, otherReach) : reach;

  RayQuery<0> rayQuery = RayQuery<0>();
  rayQuery.TraceRayInline(topLevelAS, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, OBJECT_MASK_OPAQUE, ray);
  while (rayQuery.Proceed())
  {
    const uint kind = characterKind(rayQuery.CandidateInstanceID(), rayQuery.CandidateGeometryIndex(), cb.enemies != 0);
    const uint what = kind & ~kOther;
    if ((what == kRigidPart || (what == kBody && !partsOnly)) &&
        rayQuery.CandidateTriangleRayT() <= ((kind & kOther) != 0 ? otherReach : reach))
    {
      rayQuery.CommitNonOpaqueTriangleHit();
    }
  }
  return rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// Whether a waterfall's sheet stands within reach along the ray. The game blends it, which makes it a decal to the runtime, in
// the unordered TLAS; where a mod made it opaque or it is no decal, it is in the main one.
bool hitsFallingWater(vec3 origin, vec3 direction, float reach)
{
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = 0.0f;
  ray.TMax = reach;

  {
    RayQuery<0> rayQuery = RayQuery<0>();
    rayQuery.TraceRayInline(unorderedAS, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xFF, ray);
    while (rayQuery.Proceed())
    {
      if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
          isFallingWater(rayQuery.CandidateInstanceID(), rayQuery.CandidateGeometryIndex()))
      {
        rayQuery.CommitNonOpaqueTriangleHit();
      }
    }
    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
      return true;
    }
  }

  RayQuery<0> rayQuery = RayQuery<0>();
  rayQuery.TraceRayInline(topLevelAS, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES,
                          OBJECT_MASK_OPAQUE | OBJECT_MASK_ALPHA_BLEND, ray);
  while (rayQuery.Proceed())
  {
    if (isFallingWater(rayQuery.CandidateInstanceID(), rayQuery.CandidateGeometryIndex()))
    {
      rayQuery.CommitNonOpaqueTriangleHit();
    }
  }
  return rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// Whether falling water lands in the cell: its sheet crosses a level segment through the cell, along x or z. Level only: the
// flat pools at a fall's foot share its texture and its mark, and no level segment meets those.
bool fallsInto(vec3 position)
{
  return hitsFallingWater(position - vec3(cb.fallReach, 0.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f), 2.0f * cb.fallReach) ||
         hitsFallingWater(position - vec3(0.0f, 0.0f, cb.fallReach), vec3(0.0f, 0.0f, 1.0f), 2.0f * cb.fallReach);
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
    // No character is ground, whoever's it is.
    if (characterKind(rayQuery.CandidateInstanceID(), rayQuery.CandidateGeometryIndex(), true) == kNothing)
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
  return hitsBody(position, vec3(1.0f, 0.0f, 0.0f), cb.bodyReach, cb.enemyReach, false) &&
         hitsBody(position, vec3(-1.0f, 0.0f, 0.0f), cb.bodyReach, cb.enemyReach, false) &&
         hitsBody(position, vec3(0.0f, 0.0f, 1.0f), cb.bodyReach, cb.enemyReach, false) &&
         hitsBody(position, vec3(0.0f, 0.0f, -1.0f), cb.bodyReach, cb.enemyReach, false);
}

// Whether a rigid part's surface passes through the cell: segments along the three axes through its centre, 3/4 of a cell
// to each side, which a sheet at any angle within about half a cell of the centre meets. The skinned bodies stay with the
// enclosure test alone, as they were tuned.
bool crossesRigidPart(vec3 position)
{
  const float reach = 0.75f * cb.cellSize;
  return hitsBody(position - vec3(reach, 0.0f, 0.0f), vec3(1.0f, 0.0f, 0.0f), 2.0f * reach, 2.0f * reach, true) ||
         hitsBody(position - vec3(0.0f, 0.0f, reach), vec3(0.0f, 0.0f, 1.0f), 2.0f * reach, 2.0f * reach, true) ||
         hitsBody(position - vec3(0.0f, reach, 0.0f), vec3(0.0f, 1.0f, 0.0f), 2.0f * reach, 2.0f * reach, true);
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
  float waterT = traceWater(top, cb.rayLength);
  if (waterT < 0.0f && cb.swampIsLiquid != 0)
  {
    waterT = traceSwamp(top, cb.rayLength);
  }
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
    const vec3 waterline = vec3(top.x, waterHeight + ground + 0.02f + cb.testLift, top.z);
    if (ground <= kMaxGroundAboveWater && (insideBody(waterline) || crossesRigidPart(waterline)))
    {
      body = 1.0f;
    }
  }
  if (inCollision && waterT >= 0.0f && cb.testBodyRadius > 0.0f && length(top.xz - cb.testBody) < cb.testBodyRadius)
  {
    body = 1.0f;
  }
  // Falling water, anywhere in the window: the cell stays open.
  if (body == 0.0f && cb.falls != 0 && waterT >= 0.0f && fallsInto(vec3(top.x, waterHeight + cb.fallHeight, top.z)))
  {
    body = DS1_RIPPLE_OCCUPANCY_FALL;
  }
  occupancy[texel] = body;
  // A body already standing in a cell that just entered the window has pushed no water yet.
  if (entered)
  {
    occupancyPrev[texel] = body;
  }
}
