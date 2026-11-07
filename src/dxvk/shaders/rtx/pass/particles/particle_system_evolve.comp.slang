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
#include "rtx/concept/camera/camera.slangh"
    
layout(binding = PARTICLE_SYSTEM_BINDING_PREV_WORLD_POSITION_INPUT)
Texture2D<float4> PreviousWorldPosition_WorldTriangleNormal;

layout(binding = PARTICLE_SYSTEM_BINDING_PREV_PRIMARY_SCREEN_SPACE_MOTION_INPUT)
Texture2D<float2> PreviousPrimaryScreenSpaceMotionVector;

// Outputs

layout(binding = PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT_OUTPUT)
RWStructuredBuffer<GpuParticle> Particles;

layout(binding = PARTICLE_SYSTEM_BINDING_COUNTER_OUTPUT)
RWStructuredBuffer<int> Counter;

// small bias to avoid re-penetration
static const float kCollisionBias = 1e-3f;

void collideParticleWithScene(inout GpuParticle p)
{
  // Reproject current world-space pos into last frame's clip
  float4 prevClip = mul(particleCb.prevWorldToProjection, float4(p.position, 1.0f));
  float2 prevNdc  = prevClip.xy / prevClip.w;
  float2 prevUv   = prevNdc * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

  // early-out if outside [0,1] UV
  if (prevUv.x < 0.0f || prevUv.x > 1.0f || prevUv.y < 0.0f || prevUv.y > 1.0f)
  {
    return;
  }

  // compute pixel coords in your depth/normal buffer
  uint2 pixel = uint2(
    prevUv.x * particleCb.renderingWidth  + 0.5f,
    prevUv.y * particleCb.renderingHeight + 0.5f
  );

  // fetch the MinimalSurfaceInteraction from last frame
  MinimalSurfaceInteraction si = minimalSurfaceInteractionReadFromGBuffer(pixel, PreviousWorldPosition_WorldTriangleNormal);

  if (!si.isValid)
  {
    return;
  }
  
  // unpack, normalize
  float3 n = normalize(float3(si.geometryNormal));
  
  float2 screenVelocity = PreviousPrimaryScreenSpaceMotionVector[pixel];
  
  // fetch the MinimalSurfaceInteraction from 2 frames ago
  MinimalSurfaceInteraction si2 = minimalSurfaceInteractionReadFromGBuffer(pixel + screenVelocity, PreviousWorldPosition_WorldTriangleNormal);

  float3 sceneVel = 0..xxx;
  if (si2.isValid)
  {
    float3 relativePos = (si.position - si2.position);
    sceneVel = relativePos;
  }
  
  float3 relVel = p.velocity - sceneVel;
  if (dot(relVel, n) >= 0.0f)
  {
    return;
  }

  // signed distance along that normal
  float signedDist = dot(p.position - si.position, n);

  // only collide if we've gone inside but not deeper than thickness
  float thickness = particleCb.particleSystem.desc.collisionThickness * particleCb.sceneScale;
  if (signedDist < 0.0f && signedDist > -thickness)
  {
    switch(particleCb.particleSystem.desc.collisionMode) 
    {
      case ParticleCollisionMode::Bounce:
      {
        // reflect & apply restitution
        float3 vRef = reflect(relVel, n);
        p.velocity = vRef * particleCb.particleSystem.desc.collisionRestitution + sceneVel;
      } break;
      case ParticleCollisionMode::Stop:
      {
        p.velocity = 0..xxx;
      } break;
      case ParticleCollisionMode::Kill:
      {
        p.velocity = 0..xxx;
        p.timeToLive = 0.f;
      } break;
    }
  }
}

float3 attractorAcceleration(const GpuParticleSystem particleSystem, float3 particlePos)
{
  float3 toAttractor = particleSystem.desc.attractorPosition - particlePos;
  float distSq = max(dot(toAttractor, toAttractor), 1e-4f);

  // radius tuned inverse-square falloff
  float scaledRadius = particleSystem.desc.attractorRadius * particleCb.sceneScale;
  float invRadius = rcp(max(scaledRadius, 1e-3f));
  float falloff = rcp(1.0f + distSq * invRadius * invRadius);

  return toAttractor * rsqrt(distSq) * (particleSystem.desc.attractorForce * particleCb.sceneScale * falloff);
}


// Updates a particles state for one time step
void evolve(inout GpuParticle particle, const GpuParticleSystem particleSystem)
{
  if (particleSystem.desc.useTurbulence)
  {
    float scaledTurbFreq = particleSystem.desc.turbulenceFrequency * particleCb.sceneScale;
    particle.velocity += curlOfValueNoise(particle.position * scaledTurbFreq, particleCb.absoluteTimeSecs * scaledTurbFreq) * particleSystem.desc.turbulenceForce * particleCb.sceneScale * particleCb.deltaTimeSecs;
  }
  
  // Gravity
  particle.velocity += particleCb.upDirection * particleSystem.desc.gravityForce * particleCb.sceneScale * particleCb.deltaTimeSecs;
  
  // Attractor
  particle.velocity += attractorAcceleration(particleSystem, particle.position) * particleCb.deltaTimeSecs;
  
  // Drag
  particle.velocity *= max(1.f - particleSystem.desc.dragCoefficient * particleCb.deltaTimeSecs, 0.f);

  // Fork (DS1): the player's body pushes the bloodstain's motes: inside a capsule around the player (feet up to the body's
  // height), a mote is drawn towards the player's own velocity plus a push straight out of the body, harder the deeper it
  // is. The screen-space collision below cannot do this: it sees the body only where the mote is behind its visible surface,
  // and its scene velocity is not the body's motion.
  bool ds1Carried = false;
  if (particleSystem.desc.ds1Motes != 0 && particleCb.ds1PlayerPosition.w > 0.0f)
  {
    const float radius = particleCb.ds1PlayerPosition.w;
    const float3 up = particleCb.upDirection;
    const float3 feet = particleCb.ds1PlayerPosition.xyz;
    const float along = clamp(dot(particle.position - feet, up), radius, max(particleCb.ds1MoteShape.z - radius, radius));
    const float3 away = particle.position - (feet + up * along);
    const float dist = length(away);
    if (dist < radius)
    {
      const float depth = 1.0f - dist / radius;
      const float3 sideways = away - up * dot(away, up);
      const float3 outward = dot(sideways, sideways) > 1e-8f ? normalize(sideways) : normalize(cross(up, float3(0.0f, 0.0f, 1.0f)) + 1e-4f);
      const float3 target = particleCb.ds1PlayerVelocity.xyz + outward * particleCb.ds1PlayerVelocity.w * depth;
      particle.velocity = lerp(particle.velocity, target, 1.0f - exp(-12.0f * depth * particleCb.deltaTimeSecs));
      ds1Carried = true;
    }
  }
  
  if (particleSystem.desc.enableCollisionDetection)
  {
    collideParticleWithScene(particle);
  }
  
  // Per axis motion restriction
  particle.velocity = select(bool3(particleSystem.desc.restrictVelocityX, particleSystem.desc.restrictVelocityY, particleSystem.desc.restrictVelocityZ), 0..xxx,  particle.velocity);
  
  // Max velocity clamping
  float16_t3 maxVelocity = particle.maxVelocity(particleSystem, animationDataTex) * float16_t(particleCb.sceneScale);
  particle.velocity = select(maxVelocity > 0.h, clamp(particle.velocity, -maxVelocity, maxVelocity), particle.velocity);
  
  particle.position += particle.velocity * particleCb.deltaTimeSecs;
  // Fork (DS1): how far a mote has travelled on its own, for its fade with distance (particle_system_generate_geometry). Not
  // while the body carries it: a mote swept along by the player stays visible beside them.
  if (particleSystem.desc.ds1Motes != 0 && !ds1Carried)
  {
    particle.setDs1Travel(min(particle.ds1Travel() + length(particle.velocity) * particleCb.deltaTimeSecs, 1.0e6f));
  }
  
  if (!particleSystem.desc.enableMotionTrail)
  {
    if (particleSystem.desc.alignParticlesToVelocity)
    {
      particle.rotation = 0.h;
    }
    else
    {
      particle.rotation = (particle.rotation + particle.rotationSpeed(particleSystem, animationDataTex) * particleCb.deltaTimeSecs) % twoPi;
    }
  }
}

[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint2 threadIdx : SV_DispatchThreadID)
{
  const GpuParticleSystem particleSystem = particleCb.particleSystem;
  
  if (threadIdx.x >= particleSystem.simulateParticleCount)
  {
    return;
  }
  
  const uint particleIdx = (threadIdx.x + particleSystem.particleTailOffset) % particleSystem.desc.maxNumParticles;
  
  GpuParticle particle = Particles[particleIdx];
  
  if (!WaveActiveAllTrue(particle.isSleeping() || particle.isDead()))
  {
    // Save cycles if there are whole warps of sleeping particles
    evolve(particle, particleSystem);
  }
  
  particle.timeToLive -= particleCb.deltaTimeSecs;
  
  // Our counting strategy is conservative, therefore, we must also account for the non-random, max lifetime
  float targetLifetimeForCounter = (particle.initialTimeToLive(particleSystem) - particleSystem.desc.maxTimeToLive);
  if (particle.timeToLive <= targetLifetimeForCounter && !particle.isDead()) 
  {
    particle.setDead();
    
    // Decrease the running count for this frame by 1
    InterlockedAdd(Counter[0], -1);
  }
  
  Particles[particleIdx] = particle;
}
