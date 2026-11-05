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

// Fork (DS1): what falling water meets first, seen from above the player: the height of the topmost skinned surface in each cell. Droplets start only there
// (particle_system_spawn), so none comes out of what lies under the outer surface: the inside of a helmet, what a shoulder plate covers, a chest under the chin.

#include "rtx/pass/fork_ds1_ripples.h"
#include "rtx/pass/instance_definitions.h"

layout(binding = DS1_RIPPLE_BINDING_TLAS)
RaytracingAccelerationStructure topLevelAS;

layout(binding = DS1_RIPPLE_BINDING_SURFACES)
StructuredBuffer<uint4> surfaceWords;

layout(binding = DS1_WET_BINDING_BODY_TOP)
RWTexture2D<float> bodyTop;

layout(push_constant)
ConstantBuffer<Ds1SplashTopArgs> cb;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint2 cell : SV_DispatchThreadID)
{
  const vec2 side = ((vec2(cell) + 0.5f) / float(DS1_SPLASH_TOP_SIZE) * 2.0f - 1.0f) * cb.radius;
  RayDesc ray;
  ray.Origin = vec3(cb.centre.x + side.x, cb.top, cb.centre.y + side.y);
  ray.Direction = vec3(0.0f, -1.0f, 0.0f);
  ray.TMin = 0.0f;
  ray.TMax = cb.top - cb.bottom;

  // Skinned only: a waterfall's rock does not stop what is already falling past it. The rays start just over the head (rtx_dark_souls_ripples.cpp). A commit
  // only ever brings the end of the ray closer, so the last one is the closest.
  RayQuery<0> rayQuery = RayQuery<0>();
  rayQuery.TraceRayInline(topLevelAS, RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, OBJECT_MASK_OPAQUE, ray);
  while (rayQuery.Proceed())
  {
    const uint surfaceIndex = (rayQuery.CandidateInstanceID() & CUSTOM_INDEX_SURFACE_MASK) + rayQuery.CandidateGeometryIndex();
    if ((surfaceWords[surfaceIndex * DS1_RIPPLE_SURFACE_UVEC4S + 13].z & DS1_RIPPLE_SKINNED_FLAG) != 0)
    {
      rayQuery.CommitNonOpaqueTriangleHit();
    }
  }
  bodyTop[cell] = rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? cb.top - rayQuery.CommittedRayT() : DS1_RIPPLE_NO_WATER;
}
