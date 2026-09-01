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

// Dark Souls 1 color grading pass — reconstructed from ToneCorrectBank params.
// Applied as a post-FX pass after tonemapping. Maps directly to the original
// engine's per-channel brightness/contrast/saturation math from FRPG_Fil_HDR_ColAdj.

#define POST_FX_DS_INPUT  0
#define POST_FX_DS_OUTPUT 1
#define POST_FX_DS_INOUT  0

struct PostFxDarkSoulsArgs {
  uint2  imageSize;

  // Per-channel brightness (multiplicative, range ~0.7–1.3). Identity = 1.0
  float brightnessR;
  float brightnessG;
  float brightnessB;

  // Per-channel contrast. Applied as: (x - 0.5) * contrast + 0.5. Identity = 1.0
  float contrastR;
  float contrastG;
  float contrastB;

  // Saturation. 0 = fully desaturated, 1 = identity, >1 = boosted. Uses Rec.709 luma.
  float saturation;

  // Blend weight for the whole effect [0,1].  1 = full DS1 look.
  float blendWeight;

  float _pad0;
  float _pad1;
};
