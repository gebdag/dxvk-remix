#include "rtx_ds1_occlusion_depth.h"
#include "rtx_context.h"
#include "rtx_shader_manager.h"
#include "dxvk_scoped_annotation.h"
#include "rtx/pass/ds1_occlusion_depth.h"

#include <rtx_shaders/ds1_occlusion_depth.h>

namespace dxvk {

  namespace {
    class Ds1OcclusionDepthShader : public ManagedShader {
      SHADER_SOURCE(Ds1OcclusionDepthShader, VK_SHADER_STAGE_COMPUTE_BIT, ds1_occlusion_depth)

      PUSH_CONSTANTS(Ds1OcclusionDepthArgs)

      BEGIN_PARAMETER()
        TEXTURE2D(DS1_OCD_BINDING_LINEAR_VIEW_Z)
        RW_TEXTURE2D(DS1_OCD_BINDING_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(Ds1OcclusionDepthShader);
  }

  void DxvkDs1OcclusionDepth::fill(RtxContext* ctx, const Rc<DxvkImage>& depth, const Rc<DxvkImage>& viewZ,
                                   const Rc<DxvkImageView>& viewZView, float projZZ, float projWZ) {
    const VkExtent3D extent = depth->info().extent;
    const VkImageSubresourceRange depthRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

    const Resources::Resource& linearViewZ = ctx->getResourceManager().getRaytracingOutput().m_primaryLinearViewZ;
    if (!linearViewZ.isValid()) {
      ctx->clearDepthStencilImage(depth, VkClearDepthStencilValue { 1.0f, 0 }, depthRange);
      return;
    }

    ScopedGpuProfileZone(ctx, "DS1 Occlusion Depth");

    NrdArgs primaryDirect, primaryIndirect, secondary;
    ctx->getDenoiseArgs(primaryDirect, primaryIndirect, secondary);

    Ds1OcclusionDepthArgs args = {};
    args.outputSize = { extent.width, extent.height };
    args.projZZ = projZZ;
    args.projWZ = projWZ;
    args.missLinearViewZ = primaryDirect.missLinearViewZ;
    // This runs between the game's draws, with the D3D9 bank current: its render state constants
    // (alpha ref, fog, point size) are only pushed again when they change. Every D3D9 draw selects
    // its bank again.
    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);
    ctx->pushConstants(0, sizeof(args), &args);

    ctx->bindResourceView(DS1_OCD_BINDING_LINEAR_VIEW_Z, linearViewZ.view, nullptr);
    ctx->bindResourceView(DS1_OCD_BINDING_OUTPUT, viewZView, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, Ds1OcclusionDepthShader::getShader());
    ctx->dispatch((extent.width + 15) / 16, (extent.height + 15) / 16, 1);

    // R32F -> D32 by a fragment-shader copy (copyImageFb): Vulkan has no transfer copy between colour
    // and depth images.
    ctx->copyImage(
      depth, VkImageSubresourceLayers { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 }, VkOffset3D { 0, 0, 0 },
      viewZ, VkImageSubresourceLayers { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, VkOffset3D { 0, 0, 0 },
      extent);
  }

} // namespace dxvk
