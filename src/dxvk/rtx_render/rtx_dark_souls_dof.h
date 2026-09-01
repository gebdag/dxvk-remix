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

#include "dxvk_include.h"
#include "dxvk_context.h"
#include "rtx_resources.h"
#include "rtx_options.h"
#include "rtx/pass/post_fx/post_fx_dark_souls_dof.h"

namespace dxvk {

  class DxvkDevice;
  class DxvkSampler;

  class DxvkDarkSoulsDof {
  public:
    explicit DxvkDarkSoulsDof(DxvkDevice* device);
    ~DxvkDarkSoulsDof() = default;

    // jitterOffset is the camera's current sub-pixel jitter in RENDER-resolution pixels, straight
    // from RtCamera::getJittering - same units and sign as DxvkTemporalAA::dispatch takes it. It is
    // used to undo the jitter when reading linearViewZ; see sampleViewDistance() in the shader.
    void dispatch(
      Rc<DxvkContext> ctx,
      const Resources::Resource& sourceColor,
      const Resources::Resource& linearViewZ,
      const Resources::Resource& inOutColor,
      const VkExtent3D& extent,
      const float jitterOffset[2]);

    void showImguiSettings();

    bool isEnabled() const { return enable(); }

  private:
    struct Preset {
      const char* name;
      float farBegin;
      float farEnd;
      float farMul;
      float nearBegin;
      float nearEnd;
      float nearMul;
      float dispersionSq;
    };

    static constexpr Preset kPresets[] = {
      { "Custom",                         60.0f, 360.0f, 100.0f, 3.0f, 0.0f, 100.0f, 5.0f },
      { "Default / Global fallback",      60.0f, 360.0f, 100.0f, 3.0f, 0.0f, 100.0f, 5.0f },
      { "m10 - Undead Burg / Parish",     13.0f, 47.0f, 15.0f, 1.8f, 0.0f, 0.0f, 1.0f },
      { "m11 - Firelink / Asylum",         5.0f, 80.0f, 40.0f, 1.0f, 0.3f, 60.0f, 1.0f },
      { "m12 - Darkroot",                  0.0f, 60.0f, 27.0f, 1.8f, 0.0f, 60.0f, 0.0f },
      { "m13 - Depths / Blighttown",     100.0f, 150.0f, 40.0f, 1.0f, 0.0f, 10.0f, 5.0f },
      { "m14 - Sen's / Anor Londo",      200.0f, 500.0f, 70.0f, 0.0f, 350.0f, 0.0f, 1.0f },
      { "m15 - Demon Ruins / Izalith",     5.0f, 200.0f, 20.0f, 0.5f, 0.1f, 50.0f, 1.0f },
      { "m15_1 - Oolacile Township",       5.0f, 200.0f, 20.0f, 0.5f, 0.1f, 50.0f, 1.0f },
      { "m16 - New Londo / Valley",       25.0f, 250.0f, 70.0f, 1.8f, 0.0f, 100.0f, 1.0f },
      { "m17 - Duke's / Crystal Cave",   100.0f, 500.1002f, 35.0f, 1.8f, 0.0f, 0.0f, 1.0f },
      { "m18 - Kiln of the First Flame", 100.0f, 300.0f, 10.0f, 0.5f, 0.3f, 100.0f, 1.0f },
      { "m99 - Test / Credits",           50.0f, 400.0f, 0.0f, 1.0f, 450.0f, 0.0f, 1.5f },
    };
    static constexpr int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

    void applyPreset(int idx);

    Rc<DxvkSampler> m_linearSampler;

    RTX_OPTION_ARGS("rtx.ds1dof", bool,  enable,            false,  "Enable Dark Souls 1 style depth of field.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1dof", int,   presetIndex,       1,      "Active DS1 DoF preset (0 = Custom).",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1dof", float, blendWeight,       1.0f,   "Overall blend between original image and DS1-style DoF.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1dof", float, farBegin,          60.0f,  "View distance where far blur starts.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1dof", float, farEnd,            360.0f, "View distance where far blur reaches full strength.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1dof", float, farMul,            100.0f, "Far blur strength multiplier from DofBank.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1dof", float, nearBegin,         3.0f,   "View distance where near blur starts.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1dof", float, nearEnd,           0.0f,   "View distance where near blur reaches full strength.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1dof", float, nearMul,           100.0f, "Near blur strength multiplier from DofBank.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1dof", float, dispersionSq,      5.0f,   "Chromatic dispersion scale from DofBank.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1dof", float, maxBlurRadius,     16.0f,  "Maximum blur radius in pixels at full DoF strength.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1dof", float, depthEdgeSoftness, 0.05f,  "Relative depth tolerance for edge-preserving blur.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1dof", float, cocStabilizationTexels, 1.0f,
                    "Radius, in render-resolution depth texels, over which the circle of confusion is averaged.\n"
                    "The view Z buffer is point-sampled at jittering sub-pixel positions while an upscaler is "
                    "active, so at a silhouette - and badly across alpha-tested foliage - which surface owns a "
                    "texel flips every frame, and the circle of confusion flips with it. Averaging over roughly "
                    "the one texel of uncertainty turns that flip into a bounded change in a smooth value. "
                    "0 disables the smoothing and restores a single unfiltered depth fetch. Raise it if edges "
                    "still crawl, lower it if the blur transition looks too soft.",
                    args.flags = RtxOptionFlags::NoSave);
    RTX_OPTION_ARGS("rtx.ds1dof", bool,  debugVisualizeCoc, false,
                    "Diagnostic. Output the circle of confusion as greyscale instead of the blurred image. "
                    "The CoC depends only on the sampled depth, so this splits a depth-side instability "
                    "(the grey image itself flickers) from a gather-side or source-colour one (the grey image "
                    "is stable while the normal output dances).",
                    args.flags = RtxOptionFlags::NoSave);
    RTX_OPTION_ARGS("rtx.ds1dof", bool,  compensateCameraJitter, true,
                    "Undo the camera jitter when reading the view Z buffer. That buffer is rasterized at render "
                    "resolution with the upscaler's sub-pixel jitter applied, while the colour this pass receives "
                    "has already been temporally resolved, so without this the sampled depth walks with the jitter "
                    "sequence and the circle of confusion flickers along depth discontinuities. Only exists to A/B "
                    "the correction; there is no reason to turn it off.",
                    args.flags = RtxOptionFlags::NoSave);
    RTX_OPTION_ARGS("rtx.ds1dof", float, focusExponent,     1.35f,  "Shapes how quickly blur ramps from the DoF thresholds.",
                    args.flags = RtxOptionFlags::UserSetting);
  };

} // namespace dxvk