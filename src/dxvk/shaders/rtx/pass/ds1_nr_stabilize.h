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

// Fork: DLSS-NR's change to the image (its output minus its input) averaged over frames along the motion vectors
// (DlssNeuralRendering::ds1Stabilize).

#include "rtx/utility/shader_types.h"

#define DS1_NR_STABILIZE_BINDING_INPUT          0
#define DS1_NR_STABILIZE_BINDING_MOTION_VECTOR  1
#define DS1_NR_STABILIZE_BINDING_HISTORY_INPUT  2
#define DS1_NR_STABILIZE_BINDING_OUTPUT         3
#define DS1_NR_STABILIZE_BINDING_HISTORY_OUTPUT 4

struct Ds1NrStabilizeArgs {
  uvec2 colorSize;
  uvec2 motionSize;
  // The share of this frame's change in the result (1 = none of the history).
  float currentWeight;
  uint historyValid;
  // 0: the averaged change into the history; 1: the colour as input plus that change (a second dispatch, so no
  // thread reads a neighbour's colour after another has written it).
  uint mode;
  float pad0;
};
