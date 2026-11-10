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

// Fork (DS1): the water level at the player (fork_ds1_ripples.h), or DS1_RIPPLE_NO_WATER. Only water the player is
// actually in counts. A water surface over the feet with something solid in between, a pool on a ledge or an
// aqueduct overhead, is someone else's water, as is one far above the feet.

#include "rtx/pass/fork_ds1_ripples.h"
#include "rtx/pass/instance_definitions.h"

layout(binding = DS1_RIPPLE_BINDING_TLAS)
RaytracingAccelerationStructure topLevelAS;

layout(binding = DS1_RIPPLE_BINDING_SURFACES)
StructuredBuffer<uint4> surfaceWords;

layout(binding = DS1_RIPPLE_BINDING_WATER)
RWTexture2D<float> water;

layout(binding = DS1_WET_BINDING_LEVEL)
RWTexture2D<float2> level;

layout(push_constant)
ConstantBuffer<Ds1WetLevelArgs> cb;

// Solid ground or a wall between the hips and the water above them; characters' surfaces do not count.
bool blocked(vec2 xz, float top)
{
  const float bottom = cb.feet + 0.25f;
  // Short of the surface itself: a swamp's is solid.
  top -= 0.02f;
  if (top <= bottom)
  {
    return false;
  }
  RayDesc ray;
  ray.Origin = vec3(xz.x, bottom, xz.y);
  ray.Direction = vec3(0.0f, 1.0f, 0.0f);
  ray.TMin = 0.0f;
  ray.TMax = top - bottom;

  RayQuery<0> rayQuery = RayQuery<0>();
  rayQuery.TraceRayInline(topLevelAS, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, OBJECT_MASK_OPAQUE, ray);
  while (rayQuery.Proceed())
  {
    const uint surfaceIndex = (rayQuery.CandidateInstanceID() & CUSTOM_INDEX_SURFACE_MASK) + rayQuery.CandidateGeometryIndex();
    if ((surfaceWords[surfaceIndex * DS1_RIPPLE_SURFACE_UVEC4S + 13].z & DS1_RIPPLE_SKINNED_FLAG) == 0)
    {
      rayQuery.CommitNonOpaqueTriangleHit();
    }
  }
  return rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// Whether the liquid at this height is a swamp's.
bool isSwamp(vec2 xz, float height)
{
  RayDesc ray;
  ray.Origin = vec3(xz.x, height + 0.03f, xz.y);
  ray.Direction = vec3(0.0f, -1.0f, 0.0f);
  ray.TMin = 0.0f;
  ray.TMax = 0.06f;

  RayQuery<0> rayQuery = RayQuery<0>();
  rayQuery.TraceRayInline(topLevelAS, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, OBJECT_MASK_OPAQUE, ray);
  while (rayQuery.Proceed())
  {
    const uint surfaceIndex = (rayQuery.CandidateInstanceID() & CUSTOM_INDEX_SURFACE_MASK) + rayQuery.CandidateGeometryIndex();
    if ((surfaceWords[surfaceIndex * DS1_RIPPLE_SURFACE_UVEC4S + 13].z & DS1_RIPPLE_SWAMP_FLAG) != 0)
    {
      rayQuery.CommitNonOpaqueTriangleHit();
    }
  }
  return rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// The height of lava under the body, if that is what it stands on. The game's lava is a floor, which the map draws as a
// self-lit opaque surface (textureFlags bit 30): the first surface below the hips counts, so a bridge over lava does not.
float lavaUnderFeet(vec2 xz)
{
  RayDesc ray;
  ray.Origin = vec3(xz.x, cb.feet + 0.5f, xz.y);
  ray.Direction = vec3(0.0f, -1.0f, 0.0f);
  ray.TMin = 0.0f;
  ray.TMax = 1.0f;

  RayQuery<0> rayQuery = RayQuery<0>();
  rayQuery.TraceRayInline(topLevelAS, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, OBJECT_MASK_OPAQUE, ray);
  while (rayQuery.Proceed())
  {
    const uint surfaceIndex = (rayQuery.CandidateInstanceID() & CUSTOM_INDEX_SURFACE_MASK) + rayQuery.CandidateGeometryIndex();
    if ((surfaceWords[surfaceIndex * DS1_RIPPLE_SURFACE_UVEC4S + 13].z & DS1_RIPPLE_SKINNED_FLAG) == 0)
    {
      rayQuery.CommitNonOpaqueTriangleHit();
    }
  }
  if (rayQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
  {
    return DS1_RIPPLE_NO_WATER;
  }
  const uint hitIndex = (rayQuery.CommittedInstanceID() & CUSTOM_INDEX_SURFACE_MASK) + rayQuery.CommittedGeometryIndex();
  return (surfaceWords[hitIndex * DS1_RIPPLE_SURFACE_UVEC4S + 13].z & DS1_RIPPLE_SELF_LIT_FLAG) != 0 ? ray.Origin.y - rayQuery.CommittedRayT() : DS1_RIPPLE_NO_WATER;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
  const int n = int(cb.gridSize);
  const int2 centre = cb.origin + int2(n / 2, n / 2);
  const int2 offsets[5] = { int2(0, 0), int2(cb.reachCells, 0), int2(-cb.reachCells, 0), int2(0, cb.reachCells), int2(0, -cb.reachCells) };
  float best = DS1_RIPPLE_NO_WATER;
  bool mud = false;
  for (uint i = 0; i < 5; ++i)
  {
    const int2 cell = centre + offsets[i];
    const float height = water[cell - n * int2(floor(vec2(cell) / float(n)))];
    if (height <= DS1_RIPPLE_NO_WATER || height - cb.feet > cb.maxDepth || height <= best)
    {
      continue;
    }
    const vec2 xz = (vec2(cell) + 0.5f) * cb.cellSize;
    if (!blocked(xz, height))
    {
      best = height;
      mud = cb.swampIsLiquid != 0 && isSwamp(xz, height);
    }
  }
  // y: 0 water, 1 mud, 2 lava.
  const float lava = lavaUnderFeet((vec2(centre) + 0.5f) * cb.cellSize);
  level[uint2(0, 0)] = lava > DS1_RIPPLE_NO_WATER && lava > best ? float2(lava, 2.0f) : float2(best, mud ? 1.0f : 0.0f);
}
