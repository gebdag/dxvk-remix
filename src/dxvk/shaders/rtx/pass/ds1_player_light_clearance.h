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
#pragma once

// Fork (DS1): keeps the player light out of walls (rtx_dark_souls_light_falloff.h). The bindings sit
// at the common ray tracing indices (BINDING_ACCELERATION_STRUCTURE, BINDING_SURFACE_DATA_BUFFER,
// BINDING_LIGHT_DATA_BUFFER), so they are the resources every later pass binds there anyway.

#include "rtx/utility/shader_types.h"

#define DS1_PLC_BINDING_TLAS     0
#define DS1_PLC_BINDING_SURFACES 4
#define DS1_PLC_BINDING_LIGHTS   9

// A skinned (character) surface: textureFlags (data13.z of the 16 uvec4 surface record) bit 29, set for
// the stage 6 tag 0xD2000000 (Surface::ds1SunLightmap, RtSurface::writeGPUData).
#define DS1_PLC_SURFACE_UVEC4S   16
#define DS1_PLC_SKINNED_FLAG     (1u << 29)
// A character's rigid part (a weapon, a shield): the record's first uvec4's w, low half (RtSurface::writeGPUData's flags0) carries
// it in bit 3 while the view fade's bit 2 is clear (Surface::isDs1RigidPart, DS1_RIPPLE_RIGID_PART_MARK).
#define DS1_PLC_VIEW_FADE_MARK   (1u << 2)
#define DS1_PLC_RIGID_PART_MARK  (1u << 3)

struct Ds1PlayerLightClearanceArgs {
  // The head: where the light hangs before the proxy's offset.
  vec3 anchor;
  // The player light's slot in the light buffer.
  uint lightIndex;
  // Gap left between the emitter and the surface in the way.
  float margin;
  // Hits closer than this to the head are the head itself.
  float ignore;
  // A light farther than this from the anchor is left alone: the anchor is missing (still the
  // option's default) or a frame behind a warp.
  float maxOffset;
  // The player's body: an upright capsule over the feet the light is kept out of (radius 0: none).
  float bodyRadius;
  vec3 bodyFeet;
  float bodyHeight;
};
