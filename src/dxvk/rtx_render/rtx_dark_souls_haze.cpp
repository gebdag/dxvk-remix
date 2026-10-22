#include "rtx_dark_souls_haze.h"
#include "rtx_dark_souls_exposure.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_imgui.h"
#include "../imgui/imgui.h"

#include <rtx_shaders/post_fx_dark_souls_haze.h>

namespace dxvk {

  namespace {
    class PostFxDarkSoulsHazeShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsHazeShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_haze)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(POST_FX_DS_HAZE_INOUT)
        TEXTURE2D(POST_FX_DS_HAZE_LINEAR_VIEW_Z)
        CONSTANT_BUFFER(POST_FX_DS_HAZE_CONSTANTS)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsHazeShader);

    vec3 toVec3(const Vector3& v) { return vec3 { v.x, v.y, v.z }; }
  }

  DxvkDarkSoulsHaze::DxvkDarkSoulsHaze(DxvkDevice* device)
    : m_device(device) {
  }

  void DxvkDarkSoulsHaze::dispatch(
    Rc<DxvkContext> ctx,
    const Resources::Resource& inOutColor,
    const Resources::Resource& linearViewZ,
    const VkExtent3D& extent,
    const Camera& camera,
    const float jitterOffset[2],
    float missLinearViewZ) {
    ScopedGpuProfileZone(ctx, "Dark Souls 1 Haze");

    if (m_constants == nullptr) {
      DxvkBufferCreateInfo info;
      info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
      info.size = sizeof(PostFxDarkSoulsHazeArgs);
      m_constants = m_device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer,
                                           "Dark Souls Haze Constant Buffer");
    }

    PostFxDarkSoulsHazeArgs args = {};
    args.camera = camera;
    args.imageSize = { extent.width, extent.height };
    const VkExtent3D viewZExtent = linearViewZ.image->mipLevelExtent(0);
    args.invViewZSize = { 1.0f / float(viewZExtent.width), 1.0f / float(viewZExtent.height) };
    // The jitter is in render-resolution pixels (the view Z buffer's own).
    args.depthJitterUv = { jitterOffset[0] / float(viewZExtent.width), jitterOffset[1] / float(viewZExtent.height) };
    args.missLinearViewZ = missLinearViewZ;
    args.skyDistance = skyDistance();

    args.fogColor = toVec3(fogColor());
    args.fogStrength = fogStrength();
    args.fogStart = fogStart();
    args.fogInvRange = fogInvRange();
    args.extinction = toVec3(extinction());
    // The game's shaders scale the extinction exponent by this constant (1 / ln(2)^2).
    args.extinctionScale = extinctionScale() * 2.08136892f;
    args.sunDirection = toVec3(sunDirection());
    args.phase = toVec3(phase());
    args.rayleigh = toVec3(rayleigh());
    args.mie = toVec3(mie());
    args.inverseExtinction = toVec3(inverseExtinction());
    args.inscatterScale = inscatterScale();
    args.surfaceTint = toVec3(surfaceTint());
    args.sunColor = toVec3(sunColor());
    args.scatterBlend = scatterBlend();
    args.displayScale = DxvkDarkSoulsExposure::gameExposureValue() * displayGain();

    ctx->writeToBuffer(m_constants, 0, sizeof(args), &args);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_constants);

    ctx->bindResourceView(POST_FX_DS_HAZE_INOUT, inOutColor.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_HAZE_LINEAR_VIEW_Z, linearViewZ.view, nullptr);
    ctx->bindResourceBuffer(POST_FX_DS_HAZE_CONSTANTS, DxvkBufferSlice(m_constants, 0, m_constants->info().size));
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsHazeShader::getShader());

    const VkExtent3D workgroups = util::computeBlockCount(extent, VkExtent3D { 16, 8, 1 });
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkDarkSoulsHaze::showImguiSettings() {
    if (!ImGui::CollapsingHeader("Haze (game fog and sky scattering)", ImGuiTreeNodeFlags_DefaultOpen)) {
      return;
    }
    ImGui::Indent();
    RemixGui::Checkbox("Game Haze", &enableObject());
    RemixGui::DragFloat("Haze Colour Gain", &displayGainObject(), 0.01f, 0.0f, 8.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Sky Haze Distance", &skyDistanceObject(), 10.0f, 0.0f, 100000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);

    // The game's values, as the proxy last sent them (read-only: they follow the area).
    if (!active()) {
      ImGui::TextDisabled("No values from the game yet (needs the DS1 proxy and a map draw).");
    } else if (ImGui::TreeNode("Game values (set by the proxy)")) {
      auto row = [](const char* label, const Vector3& v) { ImGui::Text("%-16s %.4g, %.4g, %.4g", label, v.x, v.y, v.z); };
      row("fog colour", fogColor());
      ImGui::Text("%-16s %.4g", "fog strength", fogStrength());
      ImGui::Text("%-16s %.4g .. +%.4g", "fog range", fogStart(), fogInvRange() > 0.0f ? 1.0f / fogInvRange() : 0.0f);
      row("extinction", extinction());
      ImGui::Text("%-16s %.4g", "distance scale", extinctionScale());
      row("sun direction", sunDirection());
      row("sun colour", sunColor());
      row("1 / extinction", inverseExtinction());
      row("rayleigh", rayleigh());
      row("mie", mie());
      row("mie phase", phase());
      row("surface tint", surfaceTint());
      ImGui::Text("%-16s %.4g", "in-scatter scale", inscatterScale());
      ImGui::Text("%-16s %.4g", "scatter blend", scatterBlend());
      ImGui::TreePop();
    }
    ImGui::Unindent();
  }

} // namespace dxvk
