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

// Fork: the ray traced scene's depth in the game's depth range, for occlusion queries
// (rtx_ds1_occlusion_depth.h).

#include "rtx/utility/shader_types.h"

#define DS1_OCD_BINDING_LINEAR_VIEW_Z 0
#define DS1_OCD_BINDING_OUTPUT        1

struct Ds1OcclusionDepthArgs {
  uvec2 outputSize;
  // The game's projection: depth = projZZ + projWZ / viewZ (D3D _33 and _43, with _34 = 1).
  float projZZ;
  float projWZ;
  // PrimaryLinearViewZ's value where the primary ray hit nothing.
  float missLinearViewZ;
  float pad0;
  float pad1;
  float pad2;
};
