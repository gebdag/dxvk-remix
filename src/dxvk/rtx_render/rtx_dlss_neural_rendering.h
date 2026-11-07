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

#include "rtx_dlss.h"

namespace dxvk {

  class NGXNeuralRenderingContext;

  class DlssNeuralRendering : public DxvkDLSS {
  public:
    explicit DlssNeuralRendering(DxvkDevice* device);

    bool supportsDlssNeuralRendering() const;

    void showDlssNeuralRenderingImguiSettings();

    // Returns true when NGX produced a valid output image.
    bool dispatch(
      Rc<RtxContext> ctx,
      DxvkBarrierSet& barriers,
      const Resources::RaytracingOutput& rtOutput,
      bool resetHistory,
      bool useRayReconstructionGuides);

    void release();

    bool useDlssNeuralRendering() const;

    void setDlssNeuralRenderingSettings(const uint32_t displaySize[2]);

    virtual void onDestroy();

    enum class Model : int {
      Model0 = 0,
      Model1 = 1,
      Model2 = 2,
    };

    RTX_OPTION("rtx.dlssNeuralRendering", bool, enable, false, "Enable DLSS 3D-Guided Neural Rendering.\n");

    RTX_OPTION_ARGS("rtx.dlssNeuralRendering", float, intensity, 1.0f,
                    "Controls the overall DLSS 3D-Guided Neural Rendering effect. Lower values reduce the effect; higher values increase it.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.dlssNeuralRendering", float, structuralStrength, 0.7f,
                    "Controls DLSS 3D-Guided Neural Rendering adjustments to details, shadows, and materials. Lower values preserve the original structure; higher values apply stronger adjustments.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.dlssNeuralRendering", float, toneStrength, 0.3f,
                    "Controls DLSS 3D-Guided Neural Rendering adjustments to lightness, darkness, and color. Lower values preserve the original tone and color; higher values apply stronger adjustments.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION("rtx.dlssNeuralRendering", Model, model, Model::Model0,
               "Selects among three DLSS 3D-Guided Neural Rendering model variants. Their appearance is content-dependent; compare them in the target scene and choose the preferred result. 0: Model A, 1: Model B, 2: Model C.");

    // Fork (DS1): the network preset the feature is created with (NVSDK_NGX_DLSSNR_Hint_Render_Preset).
    // Stock hard-coded Preset B, the lightweight network, which visibly weakened the effect next to
    // the earlier integration that set no hint; Default lets the SDK choose, as that one did.
    enum class RenderPreset : int {
      Default = 0,
      A = 1,
      B = 2,
      C = 3,
      D = 4,
      E = 5,
      F = 6,
      G = 7,
    };
    RTX_OPTION("rtx.dlssNeuralRendering", RenderPreset, renderPreset, RenderPreset::Default,
               "The DLSS 3D-Guided Neural Rendering network preset, fixed at feature creation (changing it re-creates the feature). 0: Default (the SDK chooses), 1-7: Presets A-G; Preset B is the lightweight network.");
    RTX_OPTION("rtx.dlssNeuralRendering", bool,  useAutoMask, true, "Combine the automatic character mask with the material control mask.");
    // Fork (DS1): how Auto Mask and the material control mask (per material: intensity, tone, structure,
    // 1 / 0.3 / 0.7 by default) interact. Stock passes the material mask always and, with Auto Mask,
    // takes the per-pixel minimum of both. The earlier integration passed it only without Auto Mask:
    // with Auto Mask on, the network got no material mask and the sliders applied unscaled everywhere.
    RTX_OPTION("rtx.dlssNeuralRendering", bool, combineMaterialMaskWithAutoMask, true,
               "With Auto Mask on, also pass the material control mask (stock: per-pixel minimum of both). False: the earlier behaviour, "
               "Auto Mask alone with no material mask, so Structure / Tone Intensity apply unscaled. Without Auto Mask the material mask is always used.");

    // Whether the material control mask reaches the SDK (and is written) this frame.
    static bool passesMaterialControlMask() {
      return combineMaterialMaskWithAutoMask() || !useAutoMask();
    }
    RTX_OPTION_ARGS("rtx.dlssNeuralRendering", float, skinStructureStrength, 0.5f,
                    "Controls DLSS 3D-Guided Neural Rendering adjustments to characters when Auto Mask is enabled. Lower values preserve the original structure; higher values apply stronger structural adjustments.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);

    // Highlight recovery substitutes original pre-NR HDR for pixels that were very bright
    // and would land in the active tone mapper's shoulder.
    //   .x/.y = exposed HDR max-channel preserve range (default 1.0..8.0)
    //   .z/.w = exposed LDR max-channel shoulder range (default 0.75..0.99)
    RTX_OPTION("rtx.dlssNeuralRendering", bool,    enableHighlightRecovery, true, "Recover highlights compressed by DLSS 3D-Guided Neural Rendering's LDR-domain processing.");
    RTX_OPTION("rtx.dlssNeuralRendering", Vector4, highlightRecoveryThresholds, Vector4(1.0f, 8.0f, 0.75f, 0.99f), "Smoothstep edges for highlight recovery: (exposed HDR max-channel min, exposed HDR max-channel max, LDR max-channel min, LDR max-channel max).");

    // Fork (DS1): with the game's image pipeline on, the SDR round trip around the network uses the game's own curve (linear,
    // soft shoulder) instead of ACES, whose toe amplified the network's changes in dark areas about fivefold on the way back.
    RTX_OPTION("rtx.dlssNeuralRendering", bool, ds1LinearCurve, true,
               "With Dark Souls' image pipeline on: map the frame to SDR for DLSS-NR with the game's curve (linear, soft shoulder) "
               "instead of ACES. ACES's toe amplified the network's changes in dark areas about fivefold (boiling in dim interiors).");

    // Fork (DS1): with an upscaler on, Remix's depth and motion vectors are at render resolution while the network works on
    // the upscaled colour. It accepts guides on another grid but does not reproject across it: its history lags in motion
    // and it does not accumulate (the full network shows it; the lightweight one leans less on history). The guides are
    // resampled onto the colour grid first (ds1_nr_guides), motion vectors scaled by the grid ratio.
    RTX_OPTION("rtx.dlssNeuralRendering", bool, resampleGuides, true,
               "With an upscaler on: resample the depth and motion vector guides onto DLSS-NR's colour (output) grid before the "
               "network. Off: the render-resolution guides as they are, which the network does not reproject across in motion.");

    RTX_OPTION("rtx.dlssNeuralRendering", bool, enableVolumetricControlMask, true, "Modulate the DLSS 3D-Guided Neural Rendering control mask with fog, volumetric, and alpha-blended transmittance.");

    RTX_OPTION("rtx.dlssNeuralRendering", bool, showVramUsage, false,
               "Shows SDK VRAM allocation in the DLSS 3D-Guided Neural Rendering settings. Queries aggregate SDK allocation at feature creation; excludes Remix textures. Enable before activating the feature.");

  protected:
    bool isEnabled() const override;
    bool onActivation(Rc<DxvkContext>& ctx) override;
    void onDeactivation() override;

  private:
    bool initializeDlssNeuralRendering(Rc<DxvkContext> pRenderContext);
    void disableAfterFailure(const char* pReason);

    uint32_t m_displaySize[2] = {};
    // Fork (DS1): the guides on the colour grid (resampleGuides), made when the grids differ.
    Resources::Resource m_ds1GuideDepth;
    Resources::Resource m_ds1GuideMotionVector;
    bool m_ds1ResampledGuides = true;
    // Fork (DS1): the preset the live feature was created with.
    RenderPreset m_createdPreset = RenderPreset::Default;

    std::unique_ptr<NGXNeuralRenderingContext> m_dlssNeuralRenderingContext;
  };
} // namespace dxvk
