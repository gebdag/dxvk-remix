/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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

// Disable Slang warning about deprecation of inheritance in favor of composition
// struct Base { int a; };
// struct Inherited : public Base { int b; }; <- no more this
// struct Composed { Base base; int b; }; <- only this
#pragma warning(disable:30816)

#include "surface_shared.h"
#include "rtx/utility/packing.slangh"

struct Surface
{
  // Note: Currently aligned nicely to 240 bytes, avoid changing the size of this structure (as it will require
  // more 128 byte cachelines to be brought in for a single Surface read).
  u16vec4 data0a;
  u16vec4 data0b;
  uint4 data1;
  uint4 data2;
  uint4 data3;
  uint4 data4;
  uint4 data5;
  uint4 data6;
  uint4 data7;
  uint4 data8;
  uint4 data9;
  uint4 data10;
  uint4 data11;
  uint4 data12;
  uint4 data13;
  uint4 data14;
  uint4 data15;

  // flags and properties

  // Note: Potentially temporary flag for "fullbright" rendered things (e.g. the skybox) which should appear emissive-like.
  // This may be able to be determined by some sort of fixed function state in the future, but for now this flag can be used.
  property bool isEmissive
  {
    get { return packedFlagGet(data2.w, 1 << 0); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 0) : packedFlagUnset(data2.w, 1 << 0); }
  }

  // Note: Indicates that there are no opacity-related blend modes (or translucency) on the associated Surface Material.
  // This allows for optimizations in hit logic by being able to early out before expensive material decoding is done.
  property bool isFullyOpaque
  {
    get { return packedFlagGet(data2.w, 1 << 1); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 1) : packedFlagUnset(data2.w, 1 << 1); }
  }

  property bool isStatic
  {
    get { return packedFlagGet(data2.w, 1 << 2); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 2) : packedFlagUnset(data2.w, 1 << 2); }
  }

  property bool invertedBlend
  {
    get { return packedFlagGet(data2.w, 1 << 18); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 18) : packedFlagUnset(data2.w, 1 << 18);  }
  }

  property bool isBlendingDisabled
  {
    get { return packedFlagGet(data2.w, 1 << 19); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 19) : packedFlagUnset(data2.w, 1 << 19); }
  }

  // Note: Not to be confused with isEmissive, this flag indicates that an emissive blend mode is in use. This can be
  // calculated on the GPU if needed but since space is currently available in the MemorySurface struct it is fine to precompute.
  property bool isEmissiveBlend
  {
    get { return packedFlagGet(data2.w, 1 << 20); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 20) : packedFlagUnset(data2.w, 1 << 20);  }
  }

  property bool isParticle
  {
    get { return packedFlagGet(data2.w, 1 << 21); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 21)  : packedFlagUnset(data2.w, 1 << 21); }
  }

  property bool isDecal
  {
    get { return packedFlagGet(data2.w, 1 << 22); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 22) : packedFlagUnset(data2.w, 1 << 22); }
  }

  property bool hasMaterialChanged
  {
    get { return packedFlagGet(data2.w, 1 << 23); }
    set { data2.w = newValue ? packedFlagSet(data2.w,1 << 23) : packedFlagUnset(data2.w, 1 << 23); }
  }

  property bool isAnimatedWater
  {
    get { return packedFlagGet(data2.w, 1 << 24); }
    set { data2.w = newValue ? packedFlagSet(data2.w,1 << 24) : packedFlagUnset(data2.w, 1 << 24); }
  }

  property bool isClipPlaneEnabled
  {
    get { return packedFlagGet(data2.w, 1 << 25); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 25) : packedFlagUnset(data2.w, 1 << 25); }
  }

  property bool isMatte
  {
    get { return packedFlagGet(data2.w, 1 << 26); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 26) : packedFlagUnset(data2.w, 1 << 26); }
  }

  property bool isTextureFactorBlend
  {
    get { return packedFlagGet(data2.w, 1 << 27); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 27) : packedFlagUnset(data2.w, 1 << 27); }
  }

  property bool isMotionBlurMaskOut
  {
    get { return packedFlagGet(data2.w, 1 << 28); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 28) : packedFlagUnset(data2.w, 1 << 28); }
  }

  // Note: A flag to indicate that spritesheet adjustment shouldn't be done in the Surface Interaction, typically
  // because it will be done elsewhere for another reason (e.g. for the Ray Portal case where it is done in the Surface
  // Material Interaction instead).
  property bool skipSurfaceInteractionSpritesheetAdjustment
  {
    get { return packedFlagGet(data2.w, 1 << 29); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 29) : packedFlagUnset(data2.w, 1 << 29); }
  }

  // SceneManager preserve path (usePreservePath); not the same as motion-related isStatic.
  property bool isPreservePath
  {
    get { return packedFlagGet(data2.w, 1 << 30); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 30) : packedFlagUnset(data2.w, 1 << 30); }
  }

  // Fork: a DS1 window glass pass (the proxy's stage-4 tag 0xD4000002), see resolve.slangh.
  property bool isDs1Glass
  {
    get { return packedFlagGet(data2.w, 1u << 31); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1u << 31) : packedFlagUnset(data2.w, 1u << 31); }
  }

  // Matrices

  property mat4x3 prevObjectToWorld
  {
    get 
    {
      const vec4 m0 = asfloat(data3);
      const vec4 m1 = asfloat(data4);
      const vec4 m2 = asfloat(data5);
      return transpose(mat3x4(
        m0.xyz,
        vec3(m0.w, m1.xy),
        vec3(m1.zw, m2.x),
        m2.yzw));
    }
    set 
    {
      mat3x4 transposed = transpose(newValue);
      data3 = asuint(vec4(transposed[0], transposed[1].x));
      data4 = asuint(vec4(transposed[1].yz, transposed[2].xy));
      data5 = asuint(vec4(transposed[2].z, transposed[3]));
    }
  }

  property mat3x3 normalObjectToWorld
  {
    get
    {
      const  vec4 m0 = asfloat(data6);
      const  vec4 m1 = asfloat(data7);
      const float m2 = asfloat(data13.w);
      return transpose(mat3x3(
        m0.xyz,
        vec3(m0.w, m1.xy),
        vec3(m1.zw, m2)));
    }
    set
    {
      mat3x3 transposed = transpose(newValue);
      data6 = asuint(vec4(transposed[0], transposed[1].x));
      data7 = asuint(vec4(transposed[1].yz, transposed[2].xy));
      data13.w = asuint(transposed[2].z);
    }
  }

  property mat4x3 objectToWorld
  {
    get 
    {
      const vec4 m0 = asfloat(data8);
      const vec4 m1 = asfloat(data9);
      const vec4 m2 = asfloat(data10);
      return transpose(mat3x4(
        m0.xyz,
        vec3(m0.w, m1.xy),
        vec3(m1.zw, m2.x),
        m2.yzw));
    }
    set 
    {
      mat3x4 transposed = transpose(newValue);
      data8 = asuint(vec4(transposed[0], transposed[1].x));
      data9 = asuint(vec4(transposed[1].yz, transposed[2].xy));
      data10 = asuint(vec4(transposed[2].z, transposed[3]));
    }
  }

  // Note: This is only a 4x2 matrix as currently our texture transform implementation only supports <= 2 elements, so the 3rd and 4th
  // elements this matrix may generate are currently never used (nor is the perspective division when projection is enabled, though this may
  // justify increasing this to a 4x3 matrix in the future for things projecting 3D coordinates down to 2D coordinates as this should be doable
  // to support).
  property mat4x2 textureTransform
  {
    get 
    {
      const vec4 m0 = asfloat(data11);
      const vec4 m1 = asfloat(data12);
      return mat4x2(m0, m1);
    }
    set 
    {
      data11 = asuint(newValue[0]);
      data12 = asuint(newValue[1]);
    }
  }
  // NOTE: eyeball right/up vectors are aliased with a texture transform
  property vec3 eyeBasisU
  {
    get { return asfloat(data11.xyz); }
  }
  property vec3 eyeBasisV
  {
    get { return asfloat(data12.xyz); }
  }
  property uint eyeCorrectionIndex
  {
    get { return data11.w; }
  }

  // Buffers

  property uint16_t positionBufferIndex
  {
    get { return data0a.x; }
    set { data0a.x = newValue; }
  }

  property uint16_t previousPositionBufferIndex
  {
    get { return data0a.y; }
    set { data0a.y = newValue; }
  }

  property uint16_t normalBufferIndex
  {
    get { return data0a.z; }
    set { data0a.z = newValue; }
  }

  property uint16_t texcoordBufferIndex
  {
    get { return data0a.w; }
    set { data0a.w = newValue; }
  }

  property uint16_t indexBufferIndex
  {
    get { return data0b.x; }
    set { data0b.x = newValue; }
  }

  property uint16_t color0BufferIndex
  {
    get { return data0b.y; }
    set { data0b.y = newValue; }
  }

  property bool normalsEncoded
  {
    get { return packedFlagGet(data0b.z, 1 << 0); }
    set { data0b.z = newValue ? packedFlagSet(data0b.z, 1 << 0) : packedFlagUnset(data0b.z, 1 << 0); }
  }

  property bool isVertexColorBakedLighting
  {
    get { return packedFlagGet(data0b.z, 1 << 1); }
    set { data0b.z = newValue ? packedFlagSet(data0b.z, 1 << 1) : packedFlagUnset(data0b.z, 1 << 1); }
  }

  // Fork: the DS1 view fade (FRPG_NtoA, the proxy's stage-2 tag 0xD6): the game multiplies the
  // surface's alpha by sat((|N.V| - offset) * scale), N the interpolated vertex normal. Edge-on
  // foliage cards and the Bed of Chaos seal shells fade out. data0b.z bit 2 on, bits 3-8 the
  // offset in 63rds, bits 9-15 the scale in 8ths.
  property bool hasDs1ViewFade
  {
    get { return (uint(data0b.z) & 0x4u) != 0; }
  }

  property float ds1ViewFadeOffset
  {
    get { return float((uint(data0b.z) >> 3) & 0x3Fu) / 63.0f; }
  }

  property float ds1ViewFadeScale
  {
    get { return float((uint(data0b.z) >> 9) & 0x7Fu) / 8.0f; }
  }

  property uint16_t hashPacked
  {
    get { return data0b.w; }
    set { data0b.w = newValue; }
  }

  property uint positionOffset
  {
    get { return data1.x; }
    set { data1.x = newValue; }
  }

  property uint normalOffset
  {
    get { return data1.y; }
    set { data1.y = newValue; }
  }

  property uint texcoordOffset
  {
    get { return data1.z; }
    set { data1.z = newValue; }
  }

  property uint color0Offset
  {
    get { return data1.w; }
    set { data1.w = newValue; }
  }

  // Note: Position stride between current and previous position buffer
  property uint8_t positionStride
  {
    get { return (uint8_t)(data2.y & 0xFF); }
    set { data2.y = (data2.y & 0xFFFFFF00) | uint32_t(newValue & 0xFF); }
  }

  property uint8_t normalStride
  {
    get { return (uint8_t) (data2.y >> 8) & 0xFF; }
    set { data2.y = (data2.y & 0xFFFF00FF) | (uint32_t(newValue & 0xFF) << 8); }
  }

  property uint8_t texcoordStride
  {
    get { return (uint8_t) (data2.y >> 16) & 0xFF; }
    set { data2.y = (data2.y & 0xFF00FFFF) | (uint32_t(newValue & 0xFF) << 16); }
  }

  property uint8_t color0Stride
  {
    get { return (uint8_t) (data2.y >> 24) & 0xFF; }
    set { data2.y = (data2.y & 0x00FFFFFF) | (uint32_t(newValue & 0xFF) << 24); }
  }

  property uint firstIndex
  {
    get { return (data2.z & 0xFFFFFF); }
    set { data2.z = (data2.z & 0xFF000000) | (newValue & 0xFFFFFF); }
  }

  property uint8_t indexStride
  {
    get { return (uint8_t) (data2.z >> 24) & 0xFF; }
    set { data2.z = (data2.z & 0x00FFFFFF) | (uint32_t(newValue & 0xFF) << 24); }
  }

  // Sprite sheets

  property uint8_t spriteSheetRows
  {
    get { return uint8_t(data13.x & 0xFF); }
    set { data13.x = (data13.x & ~(0xFF << 0)) | uint32_t(newValue & 0xFF) << 0; }
  }

  property uint8_t spriteSheetCols
  {
    get { return uint8_t((data13.x >> 8) & 0xFF); }
    set { data13.x = (data13.x & ~(0xFF << 8)) | uint32_t(newValue & 0xFF) << 8; }
  }

  property uint8_t spriteSheetFPS
  {
    get { return uint8_t((data13.x >> 16) & 0xFF); }
    set { data13.x = (data13.x & ~(0xFF << 16)) | uint32_t(newValue & 0xFF) << 16; }
  }

  // Fixed function

  property uint8_t alphaTestType
  {
    get { return uint8_t((data2.w >> 3) & alphaTestTypeMask); }
    set { data2.w = (data2.w & ~(alphaTestTypeMask << 3)) | ((newValue & alphaTestTypeMask) << 3); }
  }

  property float16_t alphaTestReferenceValue
  {
    get { return float16_t((uint8_t) ((data2.w >> 6) & 0xFFu)) / float16_t(0xFFu); }
    set
    {
      uint8_t v = uint8_t(float(newValue) * 0xFF);
      data2.w = (data2.w & ~(0xFF << 6)) | (v << 6); 
    }
  }

  property uint8_t blendType
  {
    get { return uint8_t((data2.w >> 14) & surfaceBlendTypeMask); }
    set { data2.w = (data2.w & ~(surfaceBlendTypeMask << 14)) | (uint32_t(newValue & surfaceBlendTypeMask) << 14); }
  }

  property uint8_t textureColorArg1Source
  {
    get { return uint8_t(data13.z & 0x3); }
    set { data13.z = (data13.z & ~0x3) | uint32_t(newValue & 0x3); }
  }

  property uint8_t textureColorArg2Source
  {
    get { return uint8_t((data13.z >> 2) & 0x3); }
    set { data13.z = (data13.z & ~(0x3 << 2)) | (uint32_t(newValue & 0x3) << 2); }
  }

  property uint8_t textureColorOperation
  {
    get { return uint8_t((data13.z >> 4) & 0x7); }
    set { data13.z = (data13.z & ~(0x7 << 4)) | (uint32_t(newValue & 0x7) << 4); }
  }

  property uint8_t textureAlphaArg1Source
  {
    get { return uint8_t((data13.z >> 7) & 0x3); }
    set { data13.z = (data13.z & ~(0x3 << 7)) | (uint32_t(newValue & 0x3) << 7); }
  }

  property uint8_t textureAlphaArg2Source
  {
    get { return uint8_t((data13.z >> 9) & 0x3); }
    set { data13.z = (data13.z & ~(0x3 << 9)) | (uint32_t(newValue & 0x3) << 9); }
  }

  property uint8_t textureAlphaOperation
  {
    get { return uint8_t((data13.z >> 11) & 0x7); }
    set { data13.z = (data13.z & ~(0x7 << 11)) | (uint32_t(newValue & 0x7) << 11); }
  }

  property uint8_t texcoordGenerationMode
  {
    get { return uint8_t((data13.z >> 17) & 0x3); }
    set { data13.z = (data13.z & ~(0x3 << 17)) | (uint32_t(newValue & 0x3) << 17); }
  }

  // A later texture stage modulates alpha by D3DRS_TEXTUREFACTOR (fork). isTextureFactorBlend
  // only carries the colour half of that multiply.
  property bool isTextureFactorAlphaBlend
  {
    get { return packedFlagGet(data13.z, 1 << 15); }
    set { data13.z = newValue ? packedFlagSet(data13.z, 1 << 15) : packedFlagUnset(data13.z, 1 << 15); }
  }

  // Fork: drawn with the unlit marker. The surface takes no light: its texture x vertex
  // colour is its emission (premultiplied by its alpha unless it uses an emissive blend).
  property bool isUnlit
  {
    get { return packedFlagGet(data13.z, 1 << 16); }
    set { data13.z = newValue ? packedFlagSet(data13.z, 1 << 16) : packedFlagUnset(data13.z, 1 << 16); }
  }

  // Fork: drawn with the DS1 swamp marker: an FRPG_Snow surface, shaded from the game's height
  // field (fork_ds1_swamp.h).
  property bool isDs1Swamp
  {
    get { return packedFlagGet(data13.z, 1 << 19); }
    set { data13.z = newValue ? packedFlagSet(data13.z, 1 << 19) : packedFlagUnset(data13.z, 1 << 19); }
  }

  // Fork: a DS1 blend skirt decal (the proxy's skirt decal marker): a lit, alpha-faded mesh the game draws over
  // the geometry it borders, which can hang a little above that geometry. The decal pass lights the pixel on the
  // skirt's own surface, by its coverage, instead of on the surface below (geometryResolverApplyUnorderedDecalsPass).
  property bool isDs1SkirtDecal
  {
    get { return packedFlagGet(data13.z, 1u << 31); }
    set { data13.z = newValue ? packedFlagSet(data13.z, 1u << 31) : packedFlagUnset(data13.z, 1u << 31); }
  }

  // Fork: a DS1 thin wall (the proxy's thin wall marker): a mesh with single-sided faces a few centimetres from
  // faces of its model, their fronts turned towards each other. The game culls whichever face is seen from behind
  // and its lights cast no shadows, so each view shows its own face. A lighting ray leaving one of those faces
  // meets the front of the other at once: it passes (ds1IsThinWallPass), and the wall stays lit. Rays arriving
  // from outside meet a back face or a front not turned towards their origin, and stop.
  property bool isDs1ThinWall
  {
    get { return packedFlagGet(data13.x, 1u << 31) && (data13.x >> 24) != 0xFFu; }
  }

  // Fork: a DS1 lamp whose light sits inside its closed mesh (game/mesh_lights in the proxy): lighting rays pass it. The thin wall
  // bit with a decal sort order no thin wall has.
  property bool isDs1NoShadow
  {
    get { return (data13.x >> 24) == 0xFFu; }
  }

  // Fork: the DS1 haze set the draw was tagged with (1-15, 0 = none): the slot of its own fog and
  // scattering constants in the DS1 look's scene pass (post_fx_dark_souls_haze.h).
  property uint ds1HazeSet
  {
    get { return (data13.z >> 20) & 0xF; }
    set { data13.z = (data13.z & ~(0xFu << 20)) | ((newValue & 0xF) << 20); }
  }

  // Fork: how many times the game's sprite shaders apply the exposure (c135.x / c135.y) to this
  // unlit sprite's colour (0-2): Remix applies its own exposure that many times.
  property uint ds1UnlitExposurePower
  {
    get { return (data13.z >> 24) & 0x3; }
    set { data13.z = (data13.z & ~(0x3u << 24)) | ((newValue & 0x3) << 24); }
  }

  property bool isEye
  {
    get { return packedFlagGet(data13.z, 1 << 14); }
    set { data13.z = newValue ? packedFlagSet(data13.z, 1 << 14) : packedFlagUnset(data13.z, 1 << 14); }
  }

  property uint tFactor
  {
    get { return data13.y; }
    set { data13.y = newValue; }
  }

  // Misc

  property vec4 clipPlane
  {
    get { return asfloat(data14); }
    set { data14 = asuint(newValue); }
  }

  property uint8_t decalSortOrder
  {
    get { return uint8_t((data13.x >> 24) & 0x7F); }
    set { data13.x = (data13.x & 0x80FFFFFF) | (uint32_t(newValue & 0x7F) << 24); }
  }

  property uint objectPickingValue
  {
    get { return data2.x; }
    set { data2.x = newValue; }
  }

  // See RtEyeParams::eyeballOrigin (rtx_materials.h) for what this is decoded from.
  property vec3 eyeOrigin
  {
    get { return asfloat(data15.xyz); }
  }

  // Index into the surfaceMaterials table.
  property uint surfaceMaterialIndex
  {
    get { return data15.w; }
    set { data15.w = newValue; }
  }

  // Fork: the DS1 stage 6 tag of the draw: its lightmap level (0xD1rrggbb), by which the runtime scales
  // the DS1 sun on this surface (ds1SunLightmapScale, fork_ds1_ambient.h), 0xD2000000 on a skinned
  // (character) draw, or 0. The level is data15.x behind textureFlags bit 28 (data15.w is the surface
  // material index); the skinned tag is textureFlags bit 29.
  property uint ds1SunLightmap
  {
    get { return packedFlagGet(data13.z, 1 << 28) ? data15.x : packedFlagGet(data13.z, 1 << 29) ? 0xD2000000u : 0u; }
  }

  // Fork: a skinned DS1 draw: a character (textureFlags bit 29).
  property bool isDs1Skinned
  {
    get { return packedFlagGet(data13.z, 1 << 29); }
  }

  // Fork: a DS1 water surface. Its animated water normals follow the game's wave layers instead of
  // the global motion: layer 1 at the texture coordinate (stage 0's transform places it), layer 2 at
  // uv * ds1WaterLayer2.x + ds1WaterLayer2.yz, each with its own normal strength (ds1WaterStrength).
  property bool isDs1Water
  {
    get { return packedFlagGet(data13.z, 1 << 26); }
  }

  // Fork: a DS1 albedo offset, added to the diffuse texture in the game's stored (gamma) space before
  // the texture stage operations: the character creator's skin tone, PS c156 in the game's character
  // shaders. data15.xyz on surfaces that are neither eyes nor water (textureFlags bit 27).
  property bool hasDs1AlbedoOffset
  {
    get { return packedFlagGet(data13.z, 1 << 27); }
  }

  property vec3 ds1AlbedoOffset
  {
    get { return asfloat(data15.xyz); }
  }

  // Fork: a DS1 self-lit surface, which the game draws as texture * c100 * a bright uniform ambient
  // (atmosphere.hpp tag_self_lit in the proxy). It emits albedo * ds1SelfLit, that ambient's
  // (c100 * colour)^2.2. data15.xyz on surfaces that are neither eyes, water nor offset (textureFlags bit 30).
  property bool hasDs1SelfLit
  {
    get { return packedFlagGet(data13.z, 1 << 30); }
  }

  property vec3 ds1SelfLit
  {
    get { return asfloat(data15.xyz); }
  }

  // data15.xyz (data15.w is the surface material index): x = half scale | unorm16 offset u << 16,
  // y = unorm16 offset v | half refraction smear << 16, both offsets wrapped to [0, 1); z = the two strength
  // halves (RtSurface::writeGPUData).
  property vec3 ds1WaterLayer2
  {
    get { return vec3(f16tof32(data15.x & 0xFFFFu), float(data15.x >> 16) / 65536.0f, float(data15.y & 0xFFFFu) / 65536.0f); }
  }

  // The third wave layer: x = scale and yz = offset relative to layer 1's uv, w = its normal strength. data14
  // (the clip plane's words, which a water surface never uses; RtSurface::writeGPUData).
  property vec4 ds1WaterLayer3
  {
    get { return asfloat(data14); }
  }

  // Fork: a DS1 SpEffect colouring: the game's FRPG_Gst_* shaders (phantoms, Iron Flesh) add a colour by view
  // angle, from the rim colour edge-on to the facing colour seen straight on, and scale the specular. data14 as
  // halfs on surfaces with no clip plane that are not water, all 0 for none (RtSurface::writeGPUData).
  property bool hasDs1Effect
  {
    get { return !isDs1Water && !isClipPlaneEnabled && any(data14 != 0u); }
  }

  property vec3 ds1EffectRim
  {
    get { return vec3(f16tof32(data14.x & 0xFFFFu), f16tof32(data14.x >> 16), f16tof32(data14.y & 0xFFFFu)); }
  }

  property vec3 ds1EffectFacing
  {
    get { return vec3(f16tof32(data14.y >> 16), f16tof32(data14.z & 0xFFFFu), f16tof32(data14.z >> 16)); }
  }

  // The effect's specular map scale over the material's own (c101), and its Phong exponent (c102).
  property float ds1EffectSpecularBoost
  {
    get { return f16tof32(data14.w & 0xFFFFu); }
  }

  property float ds1EffectSpecularExponent
  {
    get { return f16tof32(data14.w >> 16); }
  }

  // The specular boost above 1 in linear terms: the game scales its specular in display gamma, so by the boost to
  // the 2.2 power in linear. A base reflectivity cannot hold it (Iron Flesh: 10 over 1.5, 65x), so the composite
  // scales the primary specular by it (kPrimarySurfaceFlagDs1SpecularGainShift).
  property float ds1EffectSpecularGain
  {
    get { return hasDs1Effect && ds1EffectSpecularBoost > 1.0f ? pow(ds1EffectSpecularBoost, 2.2f) : 1.0f; }
  }

  // Layer 1 and layer 2 normal strengths, relative to the water normal map as authored.
  property vec2 ds1WaterStrength
  {
    get { return vec2(f16tof32(data15.z & 0xFFFFu), f16tof32(data15.z >> 16)); }
  }

  // The game's refraction smear: it reads the scene behind the water at the screen position shifted by
  // its wave normal's xz * c125 * vertex alpha. This is that shift as a view angle per unit of the shading
  // normal's tilt, c125 * 2 tan(fov_x / 2) over the slope scale of the water normal map (the proxy's
  // texture_scroll.cpp).
  property float ds1WaterSmear
  {
    get { return f16tof32(data15.y >> 16); }
  }

  // Fork: the game's environment reflection weight for this draw, on the scale of a DS1 specular colour
  // material's specular_color_scale (OpaqueSurfaceMaterialInteraction::ds1EnvSpecularScale), or -1 when the
  // proxy measured none. data15.z (bit 31 set, a half below) on surfaces whose data15 holds no eye, water,
  // albedo offset or self-lit emission (textureFlags bits 14, 26, 27 and 30 clear).
  property float ds1EnvReflection
  {
    get
    {
      const bool data15Taken = packedFlagGet(data13.z, (1 << 14) | (1 << 26) | (1 << 27) | (1 << 30));
      return !data15Taken && (data15.z & 0x80000000u) != 0 ? f16tof32(data15.z & 0xFFFFu) : -1.0f;
    }
  }

  // Fork: a DS1 soft sprite's feather in metres, or 0 when it has none. The game fades the sprite's alpha
  // by saturate(0.5 + (scene depth - sprite depth) / feather) (FRPG_Sfx_SimpleSprite_Base.fxh ZFeather; see
  // ds1ParticleSoftBlend), the feather a per-vertex distance of which the proxy sends the draw's mean.
  // data15.y on the same surfaces as ds1EnvReflection (written only for particles).
  property float ds1ParticleFeather
  {
    get
    {
      const bool data15Taken = packedFlagGet(data13.z, (1 << 14) | (1 << 26) | (1 << 27) | (1 << 30));
      return data15Taken ? 0.0f : asfloat(data15.y);
    }
  }
};

// Note: Minimal version of typical Surface Interaction for transmission across passes.
struct MinimalSurfaceInteraction
{
  vec3 position = 0..xxx;
  // Floating-point error of position representation in object space or world space, whichever is larger.
  // Used for calculating ray offsets.
  float positionError = 0.f;
  // TODO this could just be a `quaternion geometryTBN` 
  vec3 geometryNormal = 0.f;
  vec3 geometryTangent = 0.f;
  vec3 geometryBitangent = 0.f;
  float shadowTerminatorOffset = 0.f;

  // Surfaces created from gbuffer may not be valid (i.e. if this pixel was a ray miss)
  property bool isValid
  {
    get { return all(!isinf(position)); }
  }
};

struct SurfaceInteraction : MinimalSurfaceInteraction
{
  // Raw AOVs retain the triangle normal before normal bending.
  vec3 triangleNormal = 0.f;

#ifdef GBUFFER_FEATURE_DEBUG_VIEW
#if GBUFFER_FEATURE_DEBUG_VIEW
  vec3 debugInterpolatedVertexNormal = 0.f;
#endif
#endif

  vec3 motion = 0..xxx;
  vec2 textureCoordinates = 0..xx;
  vec2 textureGradientX = 0..xx;
  vec2 textureGradientY = 0..xx;
  vec3 rawTangent = 0.f;
  vec3 rawBitangent = 0.f;
  vec4 vertexColor = 0.0f;
  float triangleArea = 0.f;
};

struct GBufferMemoryMinimalSurfaceInteraction
{
  vec4 encodedWorldPositionWorldTriangleTBN;
  float positionError;
};
