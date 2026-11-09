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
#pragma once

#include "rtx/pass/common_binding_indices.h"
#include "rtx/utility/shader_types.h"
#include "particle_system_enums.h"


enum class ParticleAnimationDataRows {
  MinColor = 0,
  MaxColor,

  MinSize,
  MaxSize,

  MinRotationSpeed,
  MaxRotationSpeed,

  MaxVelocity,

  Count
};

struct GpuSpawnContext {
  mat4x3 spawnObjectToWorld;
  mat4x3 spawnPrevObjectToWorld;

  uint spawnMeshPositionsOffset;
  uint spawnMeshColorsOffset;
  uint spawnMeshTexcoordsOffset;
  uint numTriangles : 31;
  uint indices32bit : 1;

  uint16_t spawnMeshPositionsStride;
  uint16_t spawnMeshColorsStride;
  uint16_t spawnMeshTexcoordsStride;
  uint16_t spawnMeshPositionsIdx;

  uint16_t spawnMeshPrevPositionsIdx;
  uint16_t spawnMeshColorsIdx;
  uint16_t spawnMeshIndexIdx;
  uint16_t spawnMeshTexcoordsIdx;
};

// GPU-compatible particle system description - this struct is uploaded to the GPU
// and must contain only POD types with no CPU-specific members like std::vector.
struct GpuParticleSystemDesc { 
  vec3 attractorPosition;
  float attractorForce;

  float minTimeToLive;
  float maxTimeToLive;
  float initialVelocityFromNormal;
  float initialVelocityConeAngleDegrees;

  float turbulenceFrequency;
  float turbulenceForce;
  float motionTrailMultiplier;
  float minSpawnRotationSpeed;

  float initialRotationDeviationDegrees;
  float spawnBurstDuration;
  float dragCoefficient;
  float attractorRadius;
  
  float gravityForce;
  float initialVelocityFromMotion;
  uint maxNumParticles;
  ParticleBillboardType billboardType;
  ParticleSpriteSheetMode spriteSheetMode;
  ParticleCollisionMode collisionMode;
  ParticleRandomFlipAxis randomFlipAxis;

  float spawnRatePerSecond;
  float collisionThickness;
  float collisionRestitution;
  uint8_t spriteSheetRows;
  uint8_t spriteSheetCols;
  uint8_t hideEmitter : 1;
  uint8_t enableMotionTrail : 1;
  uint8_t useTurbulence : 1;
  uint8_t alignParticlesToVelocity : 1;
  uint8_t useSpawnTexcoords : 1;
  uint8_t enableCollisionDetection : 1;
  uint8_t restrictVelocityX : 1;
  uint8_t restrictVelocityY : 1;
  uint8_t restrictVelocityZ : 1;
  // Fork (DS1): drips off the wet player: spawns only where the wet grid says so (rtx_dark_souls_ripples.h).
  uint8_t ds1Drip : 1;
  // Fork (DS1): water bouncing off the player's body where falling water meets it (rtx_dark_souls_ripples.h).
  uint8_t ds1Splash : 1;
  // Fork (DS1): a bloodstain's motes, from the game's own sprites (rtx_dark_souls_effect_particles.h).
  uint8_t ds1Motes : 1;
  uint8_t ds1Reserved : 1;

// Note: Spatial fields (collisionThickness, attractorRadius, gravityForce,
  // initialVelocityFromNormal, attractorForce, turbulenceForce, turbulenceFrequency)
  // are authored in centimeters and scaled by ParticleSystemConstants::sceneScale
  // at the point of use on the GPU.
};

#ifdef __cplusplus
// CPU-side particle system description that extends GpuParticleSystemDesc with
// CPU-only members (std::vector) used for generating animation data textures.
// This struct should NOT be directly uploaded to the GPU.
struct RtxParticleSystemDesc : GpuParticleSystemDesc {
  // These are CPU only, they will be used to generate animation data textures
  std::vector<vec4> minColor;
  std::vector<vec4> maxColor;
  std::vector<vec2> minSize;
  std::vector<vec2> maxSize;
  std::vector<vec3> maxVelocity;
  std::vector<float> minRotationSpeed;
  std::vector<float> maxRotationSpeed;

  RtxParticleSystemDesc() 
    : GpuParticleSystemDesc{}
  {
    attractorPosition = {0.0f, 0.0f, 0.0f};
    attractorForce = 0.0f;
    minTimeToLive = 0.0f;
    maxTimeToLive = 0.0f;
    initialVelocityFromNormal = 0.0f;
    initialVelocityConeAngleDegrees = 0.0f;
    turbulenceFrequency = 0.0f;
    turbulenceForce = 0.0f;
    motionTrailMultiplier = 0.0f;
    minSpawnRotationSpeed = 0.0f;
    initialRotationDeviationDegrees = 0.0f;
    spawnBurstDuration = 0.0f;
    dragCoefficient = 0.0f;
    attractorRadius = 0.0f;
    gravityForce = 0.0f;
    initialVelocityFromMotion = 0.0f;
    maxNumParticles = 0;
    billboardType = FaceCamera_Spherical;
    spriteSheetMode = UseMaterialSpriteSheet;
    collisionMode = Bounce;
    randomFlipAxis = None;
    spawnRatePerSecond = 0.0f;
    collisionThickness = 0.0f;
    collisionRestitution = 0.0f;
    spriteSheetRows = 0;
    spriteSheetCols = 0;
    hideEmitter = 0;
    enableMotionTrail = 0;
    useTurbulence = 0;
    alignParticlesToVelocity = 0;
    useSpawnTexcoords = 0;
    enableCollisionDetection = 0;
    restrictVelocityX = 0;
    restrictVelocityY = 0;
    restrictVelocityZ = 0;
    ds1Drip = 0;
    ds1Splash = 0;
    ds1Motes = 0;
    ds1Reserved = 0;
  }

  XXH64_hash_t calcHash() const {
    // Hash the base GPU-compatible struct plus all animation curve data
    XXH64_hash_t h = XXH3_64bits(static_cast<const GpuParticleSystemDesc*>(this), sizeof(GpuParticleSystemDesc));
    auto hashVec = [&](const auto& v) {
      if (!v.empty()) {
        h = XXH3_64bits_withSeed(v.data(), v.size() * sizeof(v[0]), h);
      }
    };
    hashVec(minColor);
    hashVec(maxColor);
    hashVec(minSize);
    hashVec(maxSize);
    hashVec(maxVelocity);
    hashVec(minRotationSpeed);
    hashVec(maxRotationSpeed);
    return h;
  }

  // Returns only the GPU-compatible portion of this descriptor
  const GpuParticleSystemDesc& getGpuDesc() const {
    return static_cast<const GpuParticleSystemDesc&>(*this);
  }
};
#endif

struct GpuParticleSystem { 
  GpuParticleSystemDesc desc; // GPU-compatible descriptor only

  // These members aren't hashed
  float2 particleVertexOffsets[8];

  uint spawnParticleOffset = 0;
  uint spawnParticleCount = 0;
  uint numVerticesPerParticle = 4;
  uint particleTailOffset = 0; 

  uint simulateParticleCount = 0;
  uint particleHeadOffset = 0;
  uint particleCount = 0;
  uint pad = 0;

#ifndef __cplusplus
  float16_t varyTimeToLive(float rand) {
    return lerp(desc.minTimeToLive, desc.maxTimeToLive, rand);
  }
#else
  GpuParticleSystem() = default;
  GpuParticleSystem(const GpuParticleSystem& other) = default;
  explicit GpuParticleSystem(const RtxParticleSystemDesc& cpuDesc)
    : desc(cpuDesc.getGpuDesc()) { 
  }
#endif
};

struct ParticleSystemConstants {
  GpuParticleSystem particleSystem;

  mat4 viewToWorld;

  mat4 prevWorldToProjection;

  vec3 upDirection;
  float deltaTimeSecs;

  float absoluteTimeSecs;
  float invDeltaTimeSecs;
  uint frameIdx;
  uint16_t renderingWidth;
  uint16_t renderingHeight;

  float resolveTransparencyThreshold;
  float minParticleSize;
  float sceneScale;
  // Fork (DS1): the wet player's grid and water level, for drips; a threshold above 1: no drips.
  uint ds1WetGridIndex;

  uint ds1WetLevelIndex;
  uint ds1WetSamplerIndex;
  // Drips come from grid values above this: the first part of its run from 1 to 0.
  float ds1DripThreshold;
  // Falling water bounces off the body within this of the contact, sideways, in metres.
  float ds1SplashRadius;

  // Where falling water meets the player; w above 0 while it does.
  vec4 ds1FallContact;

  // The topmost body surface there, as of the last frame (ds1_splash_top), around this x and z.
  vec2 ds1BodyTopCentre;
  uint ds1BodyTopIndex;
  uint ds1Pad0;

  // Fork (DS1): the player's body for the bloodstain's motes (rtx_dark_souls_effect_particles.h): feet (xyz) and the radius of
  // the capsule that pushes (w, 0: no push); the player's velocity (xyz, world units per second) and the sideways push speed (w).
  vec4 ds1PlayerPosition;
  vec4 ds1PlayerVelocity;
  // The motes' shape: x the density exponent towards a card's middle, y the distance (world units) over which a mote fades out
  // once it has travelled that far, z the capsule's height.
  vec4 ds1MoteShape;
};
