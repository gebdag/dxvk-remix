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
#include "rtx_dark_souls_grading.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx/pass/post_fx/post_fx_dark_souls.h"
#include "rtx_imgui.h"

#include <rtx_shaders/post_fx_dark_souls.h>

namespace dxvk {

  namespace {
    class PostFxDarkSoulsShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls)

      PUSH_CONSTANTS(PostFxDarkSoulsArgs)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(POST_FX_DS_INOUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsShader);
  }

  DxvkDarkSoulsGrading::DxvkDarkSoulsGrading(DxvkDevice* /*device*/)
  {
  }

  void DxvkDarkSoulsGrading::applyPreset(const int idx) {
    if (idx <= 0 || idx >= kPresetCount)
      return;
    const Preset& p = kPresets[idx];
    brightnessRObject().setDeferred(p.bR);
    brightnessGObject().setDeferred(p.bG);
    brightnessBObject().setDeferred(p.bB);
    contrastRObject().setDeferred(p.cR);
    contrastGObject().setDeferred(p.cG);
    contrastBObject().setDeferred(p.cB);
    saturationObject().setDeferred(p.saturation);
  }

  void DxvkDarkSoulsGrading::dispatch(
    Rc<DxvkContext>      ctx,
    const Resources::Resource& inOut,
    const VkExtent3D&    extent)
  {
    ScopedGpuProfileZone(ctx, "Dark Souls 1 Grading");

    constexpr uint32_t kTileSize = 8;
    const VkExtent3D workgroups = util::computeBlockCount(extent, VkExtent3D{ kTileSize, kTileSize, 1 });

    PostFxDarkSoulsArgs args = {};
    args.imageSize   = { extent.width, extent.height };
    args.brightnessR = brightnessR();
    args.brightnessG = brightnessG();
    args.brightnessB = brightnessB();
    args.contrastR   = contrastR();
    args.contrastG   = contrastG();
    args.contrastB   = contrastB();
    args.saturation  = saturation();
    args.blendWeight = blendWeight();

    ctx->pushConstants(0, sizeof(args), &args);

    ctx->bindResourceView(POST_FX_DS_INOUT, inOut.view, nullptr);

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsShader::getShader());
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkDarkSoulsGrading::showImguiSettings() {
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Dark Souls 1 Color Grading", ImGuiTreeNodeFlags_DefaultOpen))
      return;

    RemixGui::Checkbox("Enable DS1 Grading", &enableObject());

    if (!enable())
      return;

    // --- Preset dropdown ---
    {
      const char* previewName = kPresets[presetIndex()].name;
      if (ImGui::BeginCombo("Map Preset", previewName)) {
        for (int i = 0; i < kPresetCount; ++i) {
          const bool selected = (presetIndex() == i);
          if (ImGui::Selectable(kPresets[i].name, selected)) {
            presetIndexObject().setDeferred(i);
            applyPreset(i);
          }
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }

    ImGui::Spacing();
    RemixGui::DragFloat("Blend Weight",   &blendWeightObject(),  0.01f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Spacing();
    ImGui::TextDisabled("Saturation");
    RemixGui::DragFloat("Saturation",     &saturationObject(),   0.01f, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Spacing();
    ImGui::TextDisabled("Contrast (per channel)");
    RemixGui::DragFloat("Contrast R",     &contrastRObject(),    0.01f, 0.5f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Contrast G",     &contrastGObject(),    0.01f, 0.5f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Contrast B",     &contrastBObject(),    0.01f, 0.5f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Spacing();
    ImGui::TextDisabled("Brightness (per channel)");
    RemixGui::DragFloat("Brightness R",   &brightnessRObject(),  0.01f, 0.1f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Brightness G",   &brightnessGObject(),  0.01f, 0.1f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Brightness B",   &brightnessBObject(),  0.01f, 0.1f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
  }

} // namespace dxvk
