/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/pass/particles/particle_system_bindings.slangh"

#include "rtx/utility/common.slangh"
#include "rtx/utility/noise.slangh"
#include "rtx/utility/procedural_noise.slangh"
#include "rtx/utility/brdf.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/gbuffer_helpers.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
#include "rtx/pass/fork_ds1_ripples.h"
#include "rtx/pass/ds1_wet_grid.slangh"
#include "rtx/concept/camera/camera.slangh"
    
layout(binding = PARTICLE_SYSTEM_BINDING_SPAWN_CONTEXT_PARTICLE_MAPPING_INPUT)
StructuredBuffer<uint16_t> SpawnContextParticleMapping;

layout(binding = PARTICLE_SYSTEM_BINDING_SPAWN_CONTEXTS_INPUT)
StructuredBuffer<GpuSpawnContext> SpawnContexts;

// Outputs

layout(binding = PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT_OUTPUT)
RWStructuredBuffer<GpuParticle> Particles;


f16vec3 sampleDirectionInCone(f16vec3 axis, float cosConeHalfAngle, float2 rndUV)
{
  // Sample a direction in a cone about +Z, and get its PDF (we ignore PDF here)
  float solidAnglePdf;
  f16vec3 localDir = calcUniformConeDirectionSample(vec2(rndUV), cosConeHalfAngle, solidAnglePdf);

  f16vec3 tangent, bitangent;
  calcOrthonormalBasis(axis, tangent, bitangent);

  // Rotate the local-Z sample into the world-axis frame
  return localDir.x * tangent + localDir.y * bitangent + localDir.z * axis;
}

// Generate a new particle in-place with random properties
GpuParticle respawn(uint particleIdx, uint spawnCtxIdx, const GpuParticleSystem particleSystem)
{
  GpuParticle newParticle;
  
  // Get spawn context
  GpuSpawnContext spawnCtx = SpawnContexts[spawnCtxIdx];
  
  // Get random triangle
  
  float3 positions[3]; 
  float3 prevPositions[3];
  float4 color = (spawnCtx.spawnMeshColorsIdx != BINDING_INDEX_INVALID) ? 0.0f : 1.0f;
  float4 uvMinMax = float4(0..xx, 1..xx);
  // Fork (DS1): each corner's texcoord, for the effect particles' own spawn (ds1Motes, ds1Sparks).
  float2 ds1Texcoords[3];
  
  uint randomSeed = particleIdx;
    
  float3 random01 = float3(randomFloat(uint2(randomSeed++, particleCb.frameIdx)), randomFloat(uint2(randomSeed++, particleCb.frameIdx)), randomFloat(uint2(randomSeed++, particleCb.frameIdx)));
  float3 randomBary = saturate(random01 / dot(random01, 1..xxx));
  
  uint triIdx = randomFloat(uint2(randomSeed++, particleCb.frameIdx)) * spawnCtx.numTriangles;
  
  for (uint i=0 ; i<3 ; i++)
  {
    uint indexIdx = triIdx * 3 + i;
    uint index = spawnCtx.indices32bit ? BUFFER_ARRAY(indices32, (uint)spawnCtx.spawnMeshIndexIdx, indexIdx) : BUFFER_ARRAY(indices, (uint)spawnCtx.spawnMeshIndexIdx, indexIdx);
    
    const uint baseSrcPositionOffset = (spawnCtx.spawnMeshPositionsOffset + index * spawnCtx.spawnMeshPositionsStride) / 4;
    positions[i] = float3(BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshPositionsIdx, baseSrcPositionOffset + 0),
                          BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshPositionsIdx, baseSrcPositionOffset + 1),
                          BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshPositionsIdx, baseSrcPositionOffset + 2));
    
    if (spawnCtx.spawnMeshPrevPositionsIdx != BINDING_INDEX_INVALID)
    {
      prevPositions[i].x = BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshPrevPositionsIdx, baseSrcPositionOffset + 0);
      prevPositions[i].y = BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshPrevPositionsIdx, baseSrcPositionOffset + 1);
      prevPositions[i].z = BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshPrevPositionsIdx, baseSrcPositionOffset + 2);
    }
    else
    {
      prevPositions[i] = positions[i];
    }
    
    if(spawnCtx.spawnMeshColorsIdx != BINDING_INDEX_INVALID)
    {
      const uint baseSrcColorOffset = (spawnCtx.spawnMeshColorsOffset + index * spawnCtx.spawnMeshColorsStride) / 4;
      const uint colorBits = floatBitsToUint(BUFFER_ARRAY(geometries, (uint) spawnCtx.spawnMeshColorsIdx, baseSrcColorOffset));
      // Format is B8G8R8A8_UNORM
      color += unorm4x8ToFloat4x32(colorBits).zyxw * randomBary[i];
    }
    
    // Only fetch emitter UVs when a valid texcoord buffer is bound. For a mesh without UVs,
    // spawnMeshTexcoordsIdx == BINDING_INDEX_INVALID (0xFFFF); reading geometries[0xFFFF] is an
    // out-of-range bindless descriptor access and faults the GPU (device loss). Mirrors the
    // existing guards on the color and prev-position reads above.
    float2 texcoord = float2(0.0f, 0.0f);
    if (spawnCtx.spawnMeshTexcoordsIdx != BINDING_INDEX_INVALID)
    {
      const uint baseSrcTexcoordOffset = (spawnCtx.spawnMeshTexcoordsOffset + index * spawnCtx.spawnMeshTexcoordsStride) / 4;
      texcoord = float2(BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshTexcoordsIdx, baseSrcTexcoordOffset + 0),
                        BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshTexcoordsIdx, baseSrcTexcoordOffset + 1));
    }
    ds1Texcoords[i] = texcoord;
    
    if (particleSystem.desc.useSpawnTexcoords && spawnCtx.spawnMeshTexcoordsIdx != BINDING_INDEX_INVALID)
    {
      if (i == 0)
      {
        uvMinMax.xy = texcoord;
        uvMinMax.zw = texcoord;
      }
      else
      {
        uvMinMax.xy = min(uvMinMax.xy, texcoord);
        uvMinMax.zw = max(uvMinMax.zw, texcoord);
      }
    }
  }
  
  // Sample random point on triangle
  const float randSeed = randomFloat(uint2(randomSeed++, particleCb.frameIdx));

  float3 prevWorldPosition = mul(spawnCtx.spawnPrevObjectToWorld, float4(interpolateHitAttribute(prevPositions, randomBary), 1.f)).xyz;
  float3 worldPosition = mul(spawnCtx.spawnObjectToWorld, float4(interpolateHitAttribute(positions, randomBary), 1.f));
  
  float3 worldSpawnVelocity = (worldPosition - prevWorldPosition) * particleCb.invDeltaTimeSecs;
  
  const float3 kFallbackNormal = float3(0.f, 0.f, 1.f);
  float3 localNormal = cross(positions[1] - positions[0], positions[2] - positions[0]);
  float3 worldNormal = safeNormalize(mul(spawnCtx.spawnObjectToWorld, float4(localNormal, 0.f)), kFallbackNormal);
  
  float cosCone = cos(radians(particleSystem.desc.initialVelocityConeAngleDegrees));
  f16vec3 dir = sampleDirectionInCone(worldNormal, cosCone, float2(randomFloat(uint2(randomSeed++, particleCb.frameIdx)), randomFloat(uint2(randomSeed++, particleCb.frameIdx))));
  
  float3 initialVelocity = worldSpawnVelocity * particleSystem.desc.initialVelocityFromMotion + dir * particleSystem.desc.initialVelocityFromNormal * particleCb.sceneScale;
  
  float3 spawnPosition = lerp(worldPosition, prevWorldPosition, randSeed);
  
  // Fork (DS1): the game's fronts wind counter-clockwise (its body draws cull D3DCULL_CW), so worldNormal points into its meshes. Picked by it, "the top of the
  // body" was every underside and the inside of the helmet, and the kick sent droplets through the head and out of the face.
  const float3 ds1Outward = -worldNormal;

  // Fork (DS1): drips come off the wet player's downward facing surfaces, above the water, while they are freshly wet.
  // A vertex colour is its place on the body there (fork_ds1_ripples.h), not a colour.
  bool ds1Spawns = true;
  if (particleSystem.desc.ds1Drip != 0)
  {
    const float threshold = particleCb.ds1DripThreshold;
    ds1Spawns = threshold <= 1.0f && ds1Outward.y < -0.25f && color.a > 0.999f && all(color.rgb < 0.999f);
    if (ds1Spawns)
    {
      const float level = textures[particleCb.ds1WetLevelIndex].SampleLevel(samplers[particleCb.ds1WetSamplerIndex], vec2(0.5f), 0.0f).x;
      const float wet = ds1WetGridRead(particleCb.ds1WetGridIndex, particleCb.ds1WetSamplerIndex, color.rgb);
      const float chance = saturate((wet - threshold) / max(1.0f - threshold, 1e-3f));
      ds1Spawns = worldPosition.y > level && randomFloat(uint2(randomSeed++, particleCb.frameIdx)) < chance;
    }
    color = 1.0f;
  }

  // Fork (DS1): falling water breaks on the first surface it meets, seen from above, and only there: nothing comes out from under the outer surface. It runs off along that
  // surface, down its slope and every way on a flat top, with a small kick off it, and falls.
  // Metres per second in the game's scale, times 100 for the centimetres these systems run in.
  if (particleSystem.desc.ds1Splash != 0)
  {
    const vec2 side = worldPosition.xz - particleCb.ds1FallContact.xz;
    ds1Spawns = particleCb.ds1FallContact.w > 0.0f && ds1Outward.y > 0.2f && color.a > 0.999f && all(color.rgb < 0.999f) &&
                dot(side, side) < particleCb.ds1SplashRadius * particleCb.ds1SplashRadius;
    if (ds1Spawns && particleCb.ds1BodyTopIndex != BINDING_INDEX_INVALID)
    {
      const vec2 topUv = saturate((worldPosition.xz - particleCb.ds1BodyTopCentre) / (2.0f * particleCb.ds1SplashRadius) + 0.5f);
      const int2 topCell = min(int2(topUv * float(DS1_SPLASH_TOP_SIZE)), int2(DS1_SPLASH_TOP_SIZE - 1, DS1_SPLASH_TOP_SIZE - 1));
      ds1Spawns = worldPosition.y > textures[particleCb.ds1BodyTopIndex].Load(int3(topCell, 0)).x - 0.05f;
    }
    if (ds1Spawns)
    {
      const float3 falling = float3(0.0f, -1.0f, 0.0f);
      const float3 downSlope = falling - dot(falling, ds1Outward) * ds1Outward;
      const float3 anyWay = sampleDirectionInCone(ds1Outward, 0.0f, float2(randomFloat(uint2(randomSeed++, particleCb.frameIdx)), randomFloat(uint2(randomSeed++, particleCb.frameIdx))));
      const float3 along = safeNormalize(downSlope * 1.5f + (anyWay - dot(anyWay, ds1Outward) * ds1Outward) * 0.8f, anyWay);
      const float offSpeed = lerp(30.0f, 150.0f, randomFloat(uint2(randomSeed++, particleCb.frameIdx)));
      const float alongSpeed = lerp(60.0f, 200.0f, randomFloat(uint2(randomSeed++, particleCb.frameIdx)));
      initialVelocity = (along * alongSpeed + ds1Outward * offSpeed) * particleCb.sceneScale + worldSpawnVelocity * 0.5f;
      // Just off the surface.
      spawnPosition += ds1Outward * 1.5f * particleCb.sceneScale;
    }
    color = 1.0f;
  }

  // Fork (DS1): a bloodstain's motes (rtx_dark_souls_effect_particles.h). The game's quads show a texture that is a cluster of
  // dots, densest in its middle: each particle is one dot, kept with that density. They drift off slowly, mostly upwards.
  if (particleSystem.desc.ds1Motes != 0)
  {
    const float2 uv = ds1Texcoords[0] * randomBary.x + ds1Texcoords[1] * randomBary.y + ds1Texcoords[2] * randomBary.z;
    const float r = length(uv * 2.0f - 1.0f);
    const float density = saturate(1.0f - r);
    ds1Spawns = color.a > 0.004f && randomFloat(uint2(randomSeed++, particleCb.frameIdx)) < density * density * density * 2.0f;
    const float3 drift = sampleDirectionInCone(f16vec3(particleCb.upDirection), cos(radians(75.0f)),
      float2(randomFloat(uint2(randomSeed++, particleCb.frameIdx)), randomFloat(uint2(randomSeed++, particleCb.frameIdx))));
    initialVelocity = drift * lerp(0.3f, 1.0f, randomFloat(uint2(randomSeed++, particleCb.frameIdx))) *
                      particleSystem.desc.initialVelocityFromNormal * particleCb.sceneScale;
    spawnPosition = worldPosition;
  }

  // Fork (DS1): a hit's sparks. Each of the game's streaks is a thin quad crossing the hit's centre, its ends marked u = -1 and +1
  // (the DS1 proxy): a spark flies from the streak's middle towards the end on its side, faster the further out it starts, as
  // the burst would carry it.
  if (particleSystem.desc.ds1Sparks != 0)
  {
    float3 minusEnd = 0.0f, plusEnd = 0.0f;
    float minusCount = 0.0f, plusCount = 0.0f;
    for (uint k = 0; k < 3; k++)
    {
      const float3 corner = mul(spawnCtx.spawnObjectToWorld, float4(positions[k], 1.f));
      if (ds1Texcoords[k].x < 0.0f)
      {
        minusEnd += corner;
        minusCount += 1.0f;
      }
      else
      {
        plusEnd += corner;
        plusCount += 1.0f;
      }
    }
    const float3 axis = minusCount > 0.0f && plusCount > 0.0f ? plusEnd / plusCount - minusEnd / minusCount : float3(0.0f, 0.0f, 0.0f);
    const float len = length(axis);
    const float side = ds1Texcoords[0].x * randomBary.x + ds1Texcoords[1].x * randomBary.y + ds1Texcoords[2].x * randomBary.z;
    ds1Spawns = color.a > 0.004f && len > 1e-4f && abs(side) > 0.05f;
    const float3 along = len > 1e-4f ? axis / len : float3(0.0f, 1.0f, 0.0f);
    // Half the streak's length is the reach of its outer end: speed per metre of it, scaled by how far out the spark starts.
    const float speed = 0.5f * len * abs(side) * particleSystem.desc.initialVelocityFromNormal *
                        lerp(0.7f, 1.3f, randomFloat(uint2(randomSeed++, particleCb.frameIdx)));
    initialVelocity = along * (side > 0.0f ? speed : -speed);
    spawnPosition = worldPosition;
  }

  // Spawn a particle
  newParticle.reset(particleSystem, spawnPosition, initialVelocity, uvMinMax, color, randSeed);
  if (!ds1Spawns)
  {
    newParticle.timeToLive = 0.0f;
    // Fork (DS1): a refused mote or spark dies at the next step instead of holding its slot (counted until its life passes
    // initial - max), which filled the pool at the motes' rate.
    if (particleSystem.desc.ds1Motes != 0 || particleSystem.desc.ds1Sparks != 0)
    {
      newParticle.timeToLive = half(float(newParticle.initialTimeToLive(particleSystem)) - particleSystem.desc.maxTimeToLive - 1.0f);
    }
  }
  
  return newParticle;
}


[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint2 threadIdx : SV_DispatchThreadID)
{
  const GpuParticleSystem particleSystem = particleCb.particleSystem;
  
  if(threadIdx.x >= particleSystem.spawnParticleCount)
  {
    return;
  }
  
  const uint particleIdx = (threadIdx.x + particleSystem.spawnParticleOffset) % particleSystem.desc.maxNumParticles;
  const uint spawnCtxIdx = SpawnContextParticleMapping[threadIdx.x];
  const bool shouldRespawn = Particles[particleIdx].isDead() || Particles[particleIdx].isSleeping();
  if (shouldRespawn)
  {
    Particles[particleIdx] = respawn(particleIdx, spawnCtxIdx, particleSystem);
  }
}
