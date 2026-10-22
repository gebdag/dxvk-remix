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

// Dark Souls 1 color grading pass (FRPG_Fil_HDR_ColAdj's grade), applied after
// tonemapping on the gamma-encoded frame:
//   g = saturate(g * scale);  g' = (dot(rowR.xyz, g) + rowR.w, dot(rowG.xyz, g) + rowG.w, dot(rowB.xyz, g) + rowB.w)

#define POST_FX_DS_INOUT 0

struct PostFxDarkSoulsArgs {
  uint2  imageSize;
  // Blend weight for the whole effect [0,1]. 1 = full DS1 look.
  float  blendWeight;
  // Brightness applied before the matrix (the game's c56.z; 1 for the presets).
  float  scale;

  // Rows of the affine colour matrix: xyz = coefficients, w = offset.
  float4 rowR;
  float4 rowG;
  float4 rowB;
};
