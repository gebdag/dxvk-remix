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

#define POST_FX_DS_DOF_SOURCE         0
#define POST_FX_DS_DOF_LINEAR_VIEW_Z  1
#define POST_FX_DS_DOF_OUTPUT         2
#define POST_FX_DS_DOF_LINEAR_SAMPLER 3

struct PostFxDarkSoulsDofArgs {
  uint2  imageSize;
  float2 invImageSize;

  float blendWeight;
  float farBegin;
  float farEnd;
  float farMul;

  float nearBegin;
  float nearEnd;
  float nearMul;
  float dispersionSq;

  float maxBlurRadius;
  float depthEdgeSoftness;
  float focusExponent;
  float _pad0;

  // UV offset applied when reading the view Z buffer, to undo the camera jitter that buffer
  // was rasterized with. Zero when no upscaler is active. See sampleViewDistance().
  float2 depthJitterUv;
  float2 _pad1;
};