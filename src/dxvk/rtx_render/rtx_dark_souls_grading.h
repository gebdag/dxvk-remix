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
#include "rtx/pass/post_fx/post_fx_dark_souls.h"

namespace dxvk {

  class DxvkDevice;

  // Dark Souls 1 color grading post-FX pass: FRPG_Fil_HDR_ColAdj's grade on the
  // tonemapped frame, gamma encoded as the game applies it: a brightness scale,
  // then an affine colour matrix. The matrix is the game's own (set each frame by
  // the DS1 proxy) or one built from the preset sliders (ToneCorrectBank rows).
  class DxvkDarkSoulsGrading {
  public:
    explicit DxvkDarkSoulsGrading(DxvkDevice* device);
    ~DxvkDarkSoulsGrading() = default;

    void dispatch(
      Rc<DxvkContext> ctx,
      const Resources::Resource& inOut,
      const VkExtent3D& extent);

    void showImguiSettings();

    bool isEnabled() const { return enable(); }

  private:
    // Preset data matching ToneCorrectBank rows from the game files.
    // Fields: bR, bG, bB, cR, cG, cB, saturation
    struct Preset {
      const char* name;
      float bR, bG, bB;
      float cR, cG, cB;
      float saturation;
    };

    static constexpr Preset kPresets[] = {
      // Name                         bR     bG     bB     cR     cG     cB     sat
      { "Custom",                    1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },
      { "m10 - Undead Burg (day)",   1.03f, 1.03f, 1.03f, 1.25f, 1.15f, 1.10f, 0.90f },
      { "m10 - Undead Burg (night)", 1.06f, 1.06f, 1.06f, 1.15f, 1.13f, 1.14f, 0.83f },
      { "m10 - Undead Parish",       1.05f, 1.05f, 1.05f, 1.15f, 1.14f, 1.15f, 0.90f },
      { "m11 - Undead Asylum",       0.90f, 0.90f, 0.90f, 1.23f, 1.15f, 1.08f, 1.00f },
      { "m12 - Darkroot Garden",     1.00f, 1.00f, 1.02f, 1.15f, 1.01f, 1.00f, 1.10f },
      { "m12 - Depths",              1.10f, 1.10f, 1.04f, 1.23f, 1.33f, 1.28f, 0.88f },
      { "m13 - Sen's Fortress",      0.90f, 0.90f, 0.90f, 1.30f, 1.10f, 1.10f, 0.95f },
      { "m13 - Anor Londo",          1.00f, 1.10f, 1.10f, 1.10f, 1.10f, 1.10f, 1.00f },
      { "m13 - Dark Anor Londo",     1.00f, 0.90f, 0.85f, 1.20f, 1.10f, 1.10f, 0.90f },
      { "m14 - Crystal Cave/Duke",   0.98f, 0.98f, 0.98f, 1.35f, 1.25f, 1.37f, 1.00f },
      { "m14 - Demon Ruins/Izalith", 0.90f, 0.80f, 0.80f, 1.20f, 1.30f, 1.30f, 1.10f },
      { "m15 - New Londo Ruins",     1.00f, 1.00f, 1.00f, 1.15f, 1.05f, 1.02f, 1.00f },
      { "m15 - Lost Izalith",        1.20f, 1.20f, 1.20f, 1.08f, 1.15f, 1.13f, 1.20f },
      { "m16 - Tomb of Giants",      0.80f, 0.80f, 0.80f, 1.30f, 1.30f, 1.30f, 1.15f },
      { "m17 - The Abyss",           0.70f, 0.70f, 0.70f, 1.20f, 1.20f, 1.20f, 0.80f },
      { "m18 - Kiln of First Flame", 1.25f, 0.95f, 0.95f, 1.35f, 1.26f, 1.36f, 1.00f },
      { "m99 - Painted World",       0.93f, 0.93f, 0.93f, 1.35f, 1.25f, 1.25f, 1.00f },
    };
    static constexpr int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

    void applyPreset(int idx);

    RTX_OPTION_ARGS("rtx.ds1grading", bool,  enable,       false, "Enable Dark Souls 1 color grading.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1grading", int,   presetIndex,  1,     "Active DS1 preset (0 = Custom).",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1grading", float, blendWeight,  1.0f,  "Blend weight between original (0) and DS1-graded (1) image.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1grading", float, brightnessR,  1.00f, "Brightness multiplier red channel.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1grading", float, brightnessG,  1.00f, "Brightness multiplier green channel.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1grading", float, brightnessB,  1.00f, "Brightness multiplier blue channel.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1grading", float, contrastR,    1.00f, "Contrast red channel. (x-0.5)*contrast+0.5",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1grading", float, contrastG,    1.00f, "Contrast green channel.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1grading", float, contrastB,    1.00f, "Contrast blue channel.",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1grading", float, saturation,   1.00f, "Saturation (0=greyscale, 1=identity, >1=boosted).",
                    args.flags = RtxOptionFlags::UserSetting);
    RTX_OPTION_ARGS("rtx.ds1grading", bool,  useGameValues, true,
                    "Grade with the values the game is using right now (FRPG_Fil_HDR_ColAdj: brightness c56.z, colour matrix "
                    "c62-c64, set each frame by the DS1 proxy) instead of the preset sliders, whenever the proxy sends them.",
                    args.flags = RtxOptionFlags::UserSetting);

    // Game state, set by the proxy. Never saved.
    RTX_OPTION_FLAG("rtx.ds1grading", bool,    gameActive, false, RtxOptionFlags::NoSave, "Set by the DS1 proxy once it has read the game's grade.");
    RTX_OPTION_FLAG("rtx.ds1grading", float,   gameScale,  1.0f,  RtxOptionFlags::NoSave, "The game's pre-grade brightness (c56.z).");
    RTX_OPTION_FLAG("rtx.ds1grading", Vector3, gameRowR,   Vector3(1.0f, 0.0f, 0.0f), RtxOptionFlags::NoSave, "Red row of the game's colour matrix (c62.xyz).");
    RTX_OPTION_FLAG("rtx.ds1grading", Vector3, gameRowG,   Vector3(0.0f, 1.0f, 0.0f), RtxOptionFlags::NoSave, "Green row of the game's colour matrix (c63.xyz).");
    RTX_OPTION_FLAG("rtx.ds1grading", Vector3, gameRowB,   Vector3(0.0f, 0.0f, 1.0f), RtxOptionFlags::NoSave, "Blue row of the game's colour matrix (c64.xyz).");
    RTX_OPTION_FLAG("rtx.ds1grading", Vector3, gameOffset, Vector3(0.0f), RtxOptionFlags::NoSave, "Offsets of the game's colour matrix (c62.w, c63.w, c64.w).");
  };

} // namespace dxvk
