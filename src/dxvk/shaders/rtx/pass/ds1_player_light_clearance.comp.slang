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

// Fork (DS1): keeps the player light out of walls. First, rays from the player's head towards the light: when a wall lies across
// that line, the light moves back along it to stay short of the wall (by its emitter radius and the margin), so a player backed
// into a wall keeps the light on the room's side. Then the light's own sphere: a wall beside it, which the line from the head
// never meets (a stair along a curved shaft wall), pushes it away until the sphere and the margin clear the wall. Characters do
// not count: rays pass through skinned surfaces (the player's own hair, hood, a shield worn on the back). Only surfaces wider
// than a rope, a rung or a bar count, so the light does not jump as the player passes thin things. The light buffer entry is a
// sphere light: data0.xyz its position, data0.w's low half its radius (RtSphereLight::writeGPUData).

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

// The first opaque, non-skinned surface facing the ray between tMin and tMax. Cutouts and blended surfaces (foliage, grates,
// glass) let light through and do not count. A few character layers at most lie on the way.
bool ds1WallHit(vec3 origin, vec3 direction, float tMin, float tMax, out float hitT)
{
  hitT = tMax;

  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = direction;
  ray.TMin = tMin;
  ray.TMax = tMax;

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
      return false;
    }

    const float t = rayQuery.CommittedRayT();
    const uint surfaceIndex = (rayQuery.CommittedInstanceID() & CUSTOM_INDEX_SURFACE_MASK) + rayQuery.CommittedGeometryIndex();
    if ((surfaceWords[surfaceIndex * DS1_PLC_SURFACE_UVEC4S + 13].z & DS1_PLC_SKINNED_FLAG) != 0)
    {
      ray.TMin = t + 1e-3f;
      if (ray.TMin >= ray.TMax)
      {
        return false;
      }
      continue;
    }

    hitT = t;
    return true;
  }
  return false;
}

// Two directions across a ray.
void ds1Across(vec3 direction, out vec3 side, out vec3 above)
{
  side = cross(direction, vec3(0.0f, 1.0f, 0.0f));
  side = dot(side, side) < 1e-4f ? cross(direction, vec3(1.0f, 0.0f, 0.0f)) : side;
  side = normalize(side);
  above = cross(side, direction);
}

// A wall along a ray: its centre ray and at least `needed` of four parallel rays `spread` apart hit one before tMax. Returns the
// mean distance of the rays that hit, so one ray's hit on bumpy rock does not move the light with every sway of the head.
bool ds1WideWall(vec3 origin, vec3 direction, float tMin, float tMax, float spread, uint needed, out float distance)
{
  distance = tMax;
  float hitT;
  if (!ds1WallHit(origin, direction, tMin, tMax, hitT))
  {
    return false;
  }
  vec3 side, above;
  ds1Across(direction, side, above);
  const vec3 offsets[4] = { side * spread, -side * spread, above * spread, -above * spread };
  uint blocked = 0;
  float sum = hitT;
  for (uint i = 0; i < 4; ++i)
  {
    float sideT;
    if (ds1WallHit(origin + offsets[i], direction, tMin, tMax, sideT))
    {
      ++blocked;
      sum += sideT;
    }
  }
  if (blocked < needed)
  {
    return false;
  }
  distance = sum / float(blocked + 1);
  return true;
}

// Where the light may go along the line from `anchor` towards `target`: short of the first wall across the line, by `keep`.
vec3 ds1AlongLine(vec3 anchor, vec3 target, float keep)
{
  const vec3 toTarget = target - anchor;
  const float distance = length(toTarget);
  if (!(distance > cb.ignore))
  {
    return target;
  }
  const vec3 direction = toTarget / distance;
  // Two of the four side rays 15 cm off: a wall the line grazes blocks the rays on its side only; a ladder rung or a rope blocks
  // the centre ray alone (frame_20261109_171134: the light jumped to the head and back as the player swayed against a ladder).
  float wall;
  if (!ds1WideWall(anchor, direction, cb.ignore, distance + keep, 0.15f, 2u, wall))
  {
    return target;
  }
  return anchor + direction * clamp(wall - keep, 0.0f, distance);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
  // Four uint4 per light (MemoryPolymorphicLight).
  const uint first = cb.lightIndex * 4;
  uint4 data0 = lightWords[first];
  const vec3 target = asfloat(data0.xyz);
  const float radius = f16tof32(data0.w & 0xFFFFu);
  const float keep = radius + cb.margin;

  if (length(target - cb.anchor) > cb.maxOffset)
  {
    return;
  }

  // The game's own light source the light hangs from (the anchor) goes under the floor in a roll: 0.36 m below it in
  // frame_20261112_160708, where it sits 0.4-1.0 m above it standing. Lines from there meet the floor from beneath, which no ray
  // counts, and the light stayed under the ground (the user, Manus's arena, three times). The feet stay on the ground: the lines
  // start at least 30 cm above them, and the floor is looked for from at least 50 cm above them.
  const bool feetKnown = any(cb.bodyFeet != vec3(0.0f));
  vec3 anchor = cb.anchor;
  if (feetKnown)
  {
    anchor.y = max(anchor.y, cb.bodyFeet.y + 0.3f);
  }

  // 1. Out from behind a wall across the line from the head.
  vec3 position = ds1AlongLine(anchor, target, keep);

  // 2. Away from walls beside the sphere: every direction that meets a wall closer than `keep` pushes the light back by what
  // is missing. Opposite walls closer together than the sphere cancel out and leave it in the middle.
  const vec3 directions[14] = {
    vec3(1.0f, 0.0f, 0.0f), vec3(-1.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f), vec3(0.0f, -1.0f, 0.0f),
    vec3(0.0f, 0.0f, 1.0f), vec3(0.0f, 0.0f, -1.0f),
    vec3(0.57735f, 0.57735f, 0.57735f), vec3(0.57735f, 0.57735f, -0.57735f), vec3(0.57735f, -0.57735f, 0.57735f),
    vec3(0.57735f, -0.57735f, -0.57735f), vec3(-0.57735f, 0.57735f, 0.57735f), vec3(-0.57735f, 0.57735f, -0.57735f),
    vec3(-0.57735f, -0.57735f, 0.57735f), vec3(-0.57735f, -0.57735f, -0.57735f) };
  vec3 push = vec3(0.0f);
  for (uint i = 0; i < 14; ++i)
  {
    float wall;
    // Three of four parallel rays 8 cm off: a wall, not a rung.
    if (ds1WideWall(position, directions[i], 0.0f, keep, 0.08f, 3u, wall))
    {
      push -= directions[i] * (keep - wall);
    }
  }
  const float pushLength = length(push);
  if (pushLength > keep)
  {
    push *= keep / pushLength;
  }
  position += push;

  // 3. Out of the player's body: rolling and crouching bring the head, and the light hung from it, down into the body, whose
  // shadow then darkens everything the light reaches. The body is an upright capsule over the feet; the light moves straight out
  // from its axis (away from the head's side when it lies on the axis).
  bool bodyPush = false;
  if (cb.bodyRadius > 0.0f)
  {
    const vec3 up = vec3(0.0f, 1.0f, 0.0f);
    const vec3 fromFeet = position - cb.bodyFeet;
    const float along = dot(fromFeet, up);
    if (along > -radius && along < cb.bodyHeight + radius)
    {
      vec3 side = fromFeet - up * along;
      const float need = cb.bodyRadius + radius;
      float sideLength = length(side);
      if (sideLength < need)
      {
        if (sideLength < 1e-3f)
        {
          side = target - cb.anchor;
          side -= up * dot(side, up);
          sideLength = length(side);
          side = sideLength > 1e-3f ? side / sideLength : vec3(1.0f, 0.0f, 0.0f);
          sideLength = 0.0f;
        }
        else
        {
          side /= sideLength;
        }
        position += side * (need - sideLength);
        bodyPush = true;
      }
    }
  }

  // 4. The pushes may cross another wall in a corner: the line from the head decides again.
  if (pushLength > 0.0f || bodyPush)
  {
    position = ds1AlongLine(anchor, position, keep);
  }

  // 5. Never under the floor (the user, Manus's arena: the light under the ground and the arena black). Straight down from
  // above the anchor, the light and half a metre over the feet: a floor met above the light, or closer under it than the emitter
  // and the margin, lifts the light onto it. Nothing higher counts, so a bridge or a ceiling over the player does not.
  {
    const vec3 down = vec3(0.0f, -1.0f, 0.0f);
    const float top = max(max(anchor.y, position.y), feetKnown ? cb.bodyFeet.y + 0.5f : position.y) + 0.2f;
    const vec3 origin = vec3(position.x, top, position.z);
    float floorT;
    if (ds1WideWall(origin, down, 0.0f, top - position.y + keep, 0.08f, 3u, floorT))
    {
      const float floorY = top - floorT;
      position.y = max(position.y, floorY + keep);
    }
  }

  if (any(position != target))
  {
    data0.xyz = asuint(position);
    lightWords[first] = data0;
  }
}
