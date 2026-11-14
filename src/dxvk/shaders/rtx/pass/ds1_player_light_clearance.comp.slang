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

// The first opaque, non-skinned surface between tMin and tMax. Cutouts and blended surfaces (foliage, grates, glass) let light
// through and do not count. A few character layers at most lie on the way. `bothSides`: a surface counts whichever side the ray
// meets. From the player's side every surface across the way is a wall, whatever its winding: one-sided walls, the game's
// inside-out thin walls and mirrored pieces show the ray their back, and with back faces culled the light went through them.
bool ds1WallHit(vec3 origin, vec3 direction, float tMin, float tMax, bool bothSides, out float hitT)
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
    rayQuery.TraceRayInline(topLevelAS, bothSides ? RAY_FLAG_CULL_NON_OPAQUE : (RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_CULL_NON_OPAQUE),
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
    // A character's rigid parts pass too: weapons and shields hang on bones without weights, so they are no skinned draws. A big
    // weapon in hand or on the back lay across the line to the light behind the player, and with every step of a run the light
    // jumped to the head and back (the user, 2026-11-14).
    const uint marks = surfaceWords[surfaceIndex * DS1_PLC_SURFACE_UVEC4S].w;
    if ((surfaceWords[surfaceIndex * DS1_PLC_SURFACE_UVEC4S + 13].z & DS1_PLC_SKINNED_FLAG) != 0 ||
        (marks & (DS1_PLC_VIEW_FADE_MARK | DS1_PLC_RIGID_PART_MARK)) == DS1_PLC_RIGID_PART_MARK)
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

// A wall along a ray: at least `needed` of five parallel rays (the centre and four `spread` off it) hit one before tMax. Any of
// the five may be the one that misses: a crack between two wall pieces under the centre ray let the light through when the
// centre had to hit. A rope or a bar meets one or two of them. Returns the mean distance of the rays that hit, so one ray's hit
// on bumpy rock does not move the light with every sway of the head.
bool ds1WideWall(vec3 origin, vec3 direction, float tMin, float tMax, float spread, uint needed, bool bothSides, out float distance)
{
  distance = tMax;
  vec3 side, above;
  ds1Across(direction, side, above);
  const vec3 offsets[5] = { vec3(0.0f), side * spread, -side * spread, above * spread, -above * spread };
  uint blocked = 0;
  float sum = 0.0f;
  for (uint i = 0; i < 5; ++i)
  {
    // Not enough rays left to reach `needed`: no wall.
    if (blocked + (5u - i) < needed)
    {
      return false;
    }
    float hitT;
    if (ds1WallHit(origin + offsets[i], direction, tMin, tMax, bothSides, hitT))
    {
      ++blocked;
      sum += hitT;
    }
  }
  if (blocked < needed)
  {
    return false;
  }
  distance = sum / float(blocked);
  return true;
}

// Where the light may go along the line from `origin` towards `target`: short of the first wall across the line, by `keep`.
// `tMin`: hits closer to the origin than this do not count.
vec3 ds1AlongLine(vec3 origin, vec3 target, float keep, float tMin)
{
  const vec3 toTarget = target - origin;
  const float distance = length(toTarget);
  if (!(distance > tMin))
  {
    return target;
  }
  const vec3 direction = toTarget / distance;
  // Three of the five rays 15 cm apart: a wall the line grazes blocks the rays on its side and the centre; a rope blocks one
  // (frame_20261109_171134: the light jumped to the head and back as the player swayed against a ladder). Both sides of a
  // surface count.
  float wall;
  if (!ds1WideWall(origin, direction, tMin, distance + keep, 0.15f, 3u, true, wall))
  {
    return target;
  }
  return origin + direction * clamp(wall - keep, 0.0f, distance);
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

  // Where the lines to the light start: a point the player can be in. The head is not one: backed into a wall it sits at the
  // wall or in it (animations put it there), and a line from it started on the wall's far side or met the wall closer than the
  // `ignore` distance, so the light went behind the wall and the room went dark. The game's collision keeps the player's body,
  // an upright capsule over the feet, out of every wall, narrow ones too: its axis at the head's height (kept inside the
  // capsule) is always on the player's side. Without the feet, the head with its ignore distance as before.
  vec3 start = anchor;
  float startMin = cb.ignore;
  if (feetKnown)
  {
    start = vec3(cb.bodyFeet.x, clamp(cb.anchor.y, cb.bodyFeet.y + 0.3f, cb.bodyFeet.y + cb.bodyHeight - 0.2f), cb.bodyFeet.z);
    startMin = 0.02f;
  }

  // 1. Out from behind a wall across the line from the player.
  vec3 position = ds1AlongLine(start, target, keep, startMin);

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
    // Four of five parallel rays 8 cm apart: a wall, not a rung. Both sides count: a one-sided wall beside the light cut into its
    // sphere from behind.
    if (ds1WideWall(position, directions[i], 0.0f, keep, 0.08f, 4u, true, wall))
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

  // 4. The pushes may cross another wall in a corner, or out through a narrow one: the line from the player decides again.
  if (pushLength > 0.0f || bodyPush)
  {
    position = ds1AlongLine(start, position, keep, startMin);
  }

  // 5. Never under the floor (the user, Manus's arena: the light under the ground and the arena black). Straight down from
  // above the anchor, the light and half a metre over the feet: a floor met above the light, or closer under it than the emitter
  // and the margin, lifts the light onto it. Nothing higher counts, so a bridge or a ceiling over the player does not.
  {
    const vec3 down = vec3(0.0f, -1.0f, 0.0f);
    const float top = max(max(anchor.y, position.y), feetKnown ? cb.bodyFeet.y + 0.5f : position.y) + 0.2f;
    const vec3 origin = vec3(position.x, top, position.z);
    float floorT;
    if (ds1WideWall(origin, down, 0.0f, top - position.y + keep, 0.08f, 4u, false, floorT))
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
