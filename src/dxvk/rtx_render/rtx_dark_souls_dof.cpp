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
#include "rtx_dark_souls_dof.h"

#include "dxvk_context.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx/pass/post_fx/post_fx_dark_souls_dof.h"
#include "rtx_imgui.h"

#include <rtx_shaders/post_fx_dark_souls_dof.h>

namespace dxvk {

  namespace {
    constexpr uint32_t kPostFxTileSize = 8;

    class PostFxDarkSoulsDofShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsDofShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_dof)

      PUSH_CONSTANTS(PostFxDarkSoulsDofArgs)

      BEGIN_PARAMETER()
        TEXTURE2D(POST_FX_DS_DOF_SOURCE)
        TEXTURE2D(POST_FX_DS_DOF_LINEAR_VIEW_Z)
        RW_TEXTURE2D(POST_FX_DS_DOF_OUTPUT)
        SAMPLER(POST_FX_DS_DOF_LINEAR_SAMPLER)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsDofShader);
  }

  DxvkDarkSoulsDof::DxvkDarkSoulsDof(DxvkDevice* device) {
    m_linearSampler = device->getCommon()->getResources().getSampler(
      VK_FILTER_LINEAR,
      VK_SAMPLER_MIPMAP_MODE_NEAREST,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
  }

  void DxvkDarkSoulsDof::applyPreset(const int idx) {
    if (idx <= 0 || idx >= kPresetCount) {
      return;
    }

    const Preset& preset = kPresets[idx];
    farBeginObject().setDeferred(preset.farBegin);
    farEndObject().setDeferred(preset.farEnd);
    farMulObject().setDeferred(preset.farMul);
    nearBeginObject().setDeferred(preset.nearBegin);
    nearEndObject().setDeferred(preset.nearEnd);
    nearMulObject().setDeferred(preset.nearMul);
    dispersionSqObject().setDeferred(preset.dispersionSq);
  }

  void DxvkDarkSoulsDof::dispatch(
    Rc<DxvkContext> ctx,
    const Resources::Resource& sourceColor,
    const Resources::Resource& linearViewZ,
    const Resources::Resource& inOutColor,
    const VkExtent3D& extent,
    const float jitterOffset[2]) {
    ScopedGpuProfileZone(ctx, "Dark Souls 1 DoF");

    const VkExtent3D workgroups = util::computeBlockCount(extent, VkExtent3D{ kPostFxTileSize, kPostFxTileSize, 1 });

    PostFxDarkSoulsDofArgs args = {};
    args.imageSize = { extent.width, extent.height };
    args.invImageSize = { 1.0f / float(extent.width), 1.0f / float(extent.height) };
    args.blendWeight = blendWeight();
    args.farBegin = farBegin();
    args.farEnd = farEnd();
    args.farMul = farMul();
    args.nearBegin = nearBegin();
    args.nearEnd = nearEnd();
    args.nearMul = nearMul();
    args.dispersionSq = dispersionSq();
    args.maxBlurRadius = maxBlurRadius();
    args.depthEdgeSoftness = depthEdgeSoftness();
    args.focusExponent = focusExponent();

    // The jitter is expressed in render-resolution pixels, so normalize by the view Z buffer's own
    // extent rather than by the output extent this pass dispatches over - the two differ whenever an
    // upscaler is active, which is exactly when the jitter is non-zero.
    args.depthJitterUv = { 0.0f, 0.0f };
    if (compensateCameraJitter()) {
      const VkExtent3D viewZExtent = linearViewZ.image->mipLevelExtent(0);
      args.depthJitterUv = { jitterOffset[0] / float(viewZExtent.width),
                             jitterOffset[1] / float(viewZExtent.height) };
    }

    ctx->pushConstants(0, sizeof(args), &args);
    ctx->bindResourceView(POST_FX_DS_DOF_SOURCE, sourceColor.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_DOF_LINEAR_VIEW_Z, linearViewZ.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_DOF_OUTPUT, inOutColor.view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_DOF_LINEAR_SAMPLER, m_linearSampler);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsDofShader::getShader());
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkDarkSoulsDof::showImguiSettings() {
    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Dark Souls 1 DoF", ImGuiTreeNodeFlags_DefaultOpen)) {
      return;
    }

    RemixGui::Checkbox("Enable DS1 DoF", &enableObject());
    if (!enable()) {
      return;
    }

    const char* previewName = kPresets[presetIndex()].name;
    if (ImGui::BeginCombo("Map Preset##ds1dof", previewName)) {
      for (int i = 0; i < kPresetCount; ++i) {
        const bool selected = presetIndex() == i;
        if (ImGui::Selectable(kPresets[i].name, selected)) {
          presetIndexObject().setDeferred(i);
          applyPreset(i);
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    ImGui::Spacing();
    RemixGui::DragFloat("Blend Weight##ds1dof", &blendWeightObject(), 0.01f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Max Blur Radius##ds1dof", &maxBlurRadiusObject(), 0.25f, 0.0f, 48.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Depth Edge Softness##ds1dof", &depthEdgeSoftnessObject(), 0.001f, 0.001f, 0.100f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Focus Exponent##ds1dof", &focusExponentObject(), 0.01f, 0.20f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

    ImGui::Spacing();
    ImGui::TextDisabled("DofBank parameters");
    RemixGui::DragFloat("Far Begin##ds1dof", &farBeginObject(), 0.1f, 0.0f, 4000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Far End##ds1dof", &farEndObject(), 0.1f, 0.0f, 4000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Far Mul##ds1dof", &farMulObject(), 0.1f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Near Begin##ds1dof", &nearBeginObject(), 0.1f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Near End##ds1dof", &nearEndObject(), 0.1f, 0.0f, 1000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Near Mul##ds1dof", &nearMulObject(), 0.1f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Dispersion Sq##ds1dof", &dispersionSqObject(), 0.01f, 0.0f, 20.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
  }

} // namespace dxvk