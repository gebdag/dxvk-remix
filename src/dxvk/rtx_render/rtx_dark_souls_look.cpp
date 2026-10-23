#include "rtx_dark_souls_look.h"
#include "rtx_dark_souls_exposure.h"
#include "rtx_dark_souls_haze.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_imgui.h"
#include "../imgui/imgui.h"

#include <cmath>

#include <rtx_shaders/post_fx_dark_souls_look_scene.h>
#include <rtx_shaders/post_fx_dark_souls_look_bright.h>
#include <rtx_shaders/post_fx_dark_souls_look_filter.h>
#include <rtx_shaders/post_fx_dark_souls_look_composite.h>
#include <rtx_shaders/post_fx_dark_souls_look_reduce.h>
#include <rtx_shaders/post_fx_dark_souls_look_near.h>
#include <rtx_shaders/post_fx_dark_souls_look_dof.h>

namespace dxvk {

  namespace {
    class PostFxDarkSoulsLookSceneShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsLookSceneShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_look_scene)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(POST_FX_DS_LOOK_SCENE_INOUT)
        TEXTURE2D(POST_FX_DS_LOOK_SCENE_LINEAR_VIEW_Z)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_SCENE_HAZE)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_SCENE_CONSTANTS)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookSceneShader);

    class PostFxDarkSoulsLookBrightShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsLookBrightShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_look_bright)

      BEGIN_PARAMETER()
        TEXTURE2D(POST_FX_DS_LOOK_BRIGHT_SCENE)
        TEXTURE2D(POST_FX_DS_LOOK_BRIGHT_LINEAR_VIEW_Z)
        RW_TEXTURE2D(POST_FX_DS_LOOK_BRIGHT_OUTPUT)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_BRIGHT_CONSTANTS)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookBrightShader);

    class PostFxDarkSoulsLookFilterShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsLookFilterShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_look_filter)

      BEGIN_PARAMETER()
        SAMPLER2D(POST_FX_DS_LOOK_FILTER_INPUT)
        RW_TEXTURE2D(POST_FX_DS_LOOK_FILTER_OUTPUT)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_FILTER_CONSTANTS)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookFilterShader);

    class PostFxDarkSoulsLookCompositeShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsLookCompositeShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_look_composite)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(POST_FX_DS_LOOK_COMPOSITE_INOUT)
        SAMPLER2D(POST_FX_DS_LOOK_COMPOSITE_BLOOM)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_COMPOSITE_CONSTANTS)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookCompositeShader);

    class PostFxDarkSoulsLookReduceShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsLookReduceShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_look_reduce)

      BEGIN_PARAMETER()
        TEXTURE2D(POST_FX_DS_LOOK_REDUCE_INPUT)
        RW_TEXTURE2D(POST_FX_DS_LOOK_REDUCE_OUTPUT)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_REDUCE_CONSTANTS)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookReduceShader);

    class PostFxDarkSoulsLookNearShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsLookNearShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_look_near)

      BEGIN_PARAMETER()
        TEXTURE2D(POST_FX_DS_LOOK_NEAR_LINEAR_VIEW_Z)
        RW_TEXTURE2D(POST_FX_DS_LOOK_NEAR_OUTPUT)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_NEAR_CONSTANTS)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookNearShader);

    class PostFxDarkSoulsLookDofShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsLookDofShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_look_dof)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(POST_FX_DS_LOOK_DOF_INOUT)
        SAMPLER2D(POST_FX_DS_LOOK_DOF_LEVEL1)
        SAMPLER2D(POST_FX_DS_LOOK_DOF_LEVEL2)
        SAMPLER2D(POST_FX_DS_LOOK_DOF_LEVEL3)
        SAMPLER2D(POST_FX_DS_LOOK_DOF_NEAR)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_DOF_CONSTANTS)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookDofShader);

    // FRPG_Fil_Dof_Unfocus3x3: four taps, (1, 0.28) texels and that rotated, each weighted 0.212121.
    constexpr float kUnfocusWeight = 0.212120995f;
    const vec4 kUnfocus[4] = {
      { 1.0f, 0.28f, kUnfocusWeight, 0 }, { -1.0f, -0.28f, kUnfocusWeight, 0 },
      { 0.28f, -1.0f, kUnfocusWeight, 0 }, { -0.28f, 1.0f, kUnfocusWeight, 0 },
    };
    // FRPG_Fil_Dof_BlurUpSample: the four half-texel diagonals of the half-size input, 0.25 each.
    const vec4 kBlurUpSample[4] = {
      { 0.5f, 0.5f, 0.25f, 0 }, { -0.5f, -0.5f, 0.25f, 0 }, { 0.5f, -0.5f, 0.25f, 0 }, { -0.5f, 0.5f, 0.25f, 0 },
    };

    // The game's kernels, read from its draws (PS c22..c36 offsets, c38..c52 weights). Offsets
    // are in texels of the pass's input.
    //
    // FRPG_Fil_GaussBlur5x5: a 13-tap diamond, a sigma 1 Gaussian normalised to 1.
    constexpr float kGaussCentre = 0.183858f, kGaussEdge = 0.111515f, kGaussDiagonal = 0.067638f, kGaussFar = 0.024882f;
    const vec4 kGauss5x5[13] = {
      { -2, 0, kGaussFar, 0 }, { -1, -1, kGaussDiagonal, 0 }, { -1, 0, kGaussEdge, 0 }, { -1, 1, kGaussDiagonal, 0 },
      { 0, -2, kGaussFar, 0 }, { 0, -1, kGaussEdge, 0 }, { 0, 0, kGaussCentre, 0 }, { 0, 1, kGaussEdge, 0 },
      { 0, 2, kGaussFar, 0 }, { 1, -1, kGaussDiagonal, 0 }, { 1, 0, kGaussEdge, 0 }, { 1, 1, kGaussDiagonal, 0 },
      { 2, 0, kGaussFar, 0 },
    };
    // FRPG_Fil_Bloom: 15 taps at whole texels, 2 x a sigma 3 Gaussian (the weights sum to 1.976).
    const float kBloomWeights[8] = { 0.265961f, 0.251589f, 0.212965f, 0.161314f, 0.10934f, 0.066318f, 0.035994f, 0.017481f };
    // FRPG_Fil_Dof_DownSample at half size: one bilinear tap on the corner of four texels.
    const vec4 kDownsample2x[1] = { { 0, 0, 1, 0 } };

    void bloomTaps(vec4 (&taps)[15], bool vertical) {
      taps[0] = vec4 { 0, 0, kBloomWeights[0], 0 };
      for (int i = 1; i < 8; ++i) {
        const float o = float(i);
        taps[i] = vertical ? vec4 { 0, o, kBloomWeights[i], 0 } : vec4 { o, 0, kBloomWeights[i], 0 };
        taps[i + 7] = vertical ? vec4 { 0, -o, kBloomWeights[i], 0 } : vec4 { -o, 0, kBloomWeights[i], 0 };
      }
    }

    vec4 toVec4(const Vector4& v) { return vec4 { v.x, v.y, v.z, v.w }; }
  }

  DxvkDarkSoulsLook::DxvkDarkSoulsLook(DxvkDevice* device)
    : m_device(device) {
    m_linearSampler = device->getCommon()->getResources().getSampler(
      VK_FILTER_LINEAR,
      VK_SAMPLER_MIPMAP_MODE_NEAREST,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
  }

  bool DxvkDarkSoulsLook::isActive() {
    return enable() && DxvkDarkSoulsExposure::gameActive();
  }

  Rc<DxvkBuffer> DxvkDarkSoulsLook::makeConstants(size_t size, const char* name) {
    DxvkBufferCreateInfo info;
    info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
    info.size = size;
    return m_device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, name);
  }

  void DxvkDarkSoulsLook::ensureResources(Rc<DxvkContext>& ctx, uint32_t width, uint32_t height) {
    if (m_sceneConstants == nullptr) {
      m_sceneConstants = makeConstants(sizeof(PostFxDarkSoulsLookSceneArgs), "DS1 Look Scene Constants");
      m_hazeConstants = makeConstants(sizeof(PostFxDarkSoulsHazeArgs), "DS1 Look Haze Constants");
      m_brightConstants = makeConstants(sizeof(PostFxDarkSoulsLookBrightArgs), "DS1 Look Bright Constants");
      for (auto& c : m_filterConstants) {
        c = makeConstants(sizeof(PostFxDarkSoulsLookFilterArgs), "DS1 Look Filter Constants");
      }
      m_compositeConstants = makeConstants(sizeof(PostFxDarkSoulsLookCompositeArgs), "DS1 Look Composite Constants");
      m_reduceConstants = makeConstants(sizeof(PostFxDarkSoulsLookReduceArgs), "DS1 Look Reduce Constants");
      m_nearConstants = makeConstants(sizeof(PostFxDarkSoulsLookNearArgs), "DS1 Look Near Constants");
      for (auto& c : m_dofFilterConstants) {
        c = makeConstants(sizeof(PostFxDarkSoulsLookFilterArgs), "DS1 Look DoF Filter Constants");
      }
      m_dofConstants = makeConstants(sizeof(PostFxDarkSoulsLookDofArgs), "DS1 Look DoF Constants");
    }
    if (width == m_baseWidth && height == m_baseHeight && m_base[0].isValid()) {
      return;
    }
    m_baseWidth = width;
    m_baseHeight = height;
    const VkExtent3D base { width, height, 1 };
    const VkExtent3D half { std::max(width / 2, 1u), std::max(height / 2, 1u), 1 };
    for (int i = 0; i < 2; ++i) {
      m_base[i] = Resources::createImageResource(ctx, "DS1 look bloom base", base, VK_FORMAT_R16G16B16A16_SFLOAT);
      m_half[i] = Resources::createImageResource(ctx, "DS1 look bloom half", half, VK_FORMAT_R16G16B16A16_SFLOAT);
    }
    // The game's DoF buffers are twice the bright pass size (512x360 for its 256x180).
    const VkExtent3D dof { 2 * width, 2 * height, 1 };
    m_dofDown = Resources::createImageResource(ctx, "DS1 look DoF down", dof, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_dofUnfocus = Resources::createImageResource(ctx, "DS1 look DoF unfocus", dof, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_dofUp = Resources::createImageResource(ctx, "DS1 look DoF up", dof, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_dofHalfDown = Resources::createImageResource(ctx, "DS1 look DoF half down", base, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_dofHalfUnfocus = Resources::createImageResource(ctx, "DS1 look DoF half unfocus", base, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_dofNear = Resources::createImageResource(ctx, "DS1 look DoF near", base, VK_FORMAT_R16G16B16A16_SFLOAT);
  }

  void DxvkDarkSoulsLook::dispatchScene(
    Rc<DxvkContext> ctx,
    const Resources::Resource& inOutColor,
    const Resources::Resource& linearViewZ,
    const DxvkDarkSoulsHaze& haze,
    const Camera& camera,
    const float jitterOffset[2],
    float missLinearViewZ) {
    ScopedGpuProfileZone(ctx, "Dark Souls 1 Look: Scene");
    ensureResources(ctx, std::max(bloomWidth(), 2), std::max(bloomHeight(), 2));

    const VkExtent3D extent = inOutColor.image->mipLevelExtent(0);
    PostFxDarkSoulsHazeArgs hazeArgs;
    const bool hazeValid = haze.fillArgs(hazeArgs, extent, linearViewZ, camera, jitterOffset, missLinearViewZ);

    PostFxDarkSoulsLookSceneArgs args = {};
    args.exposure = DxvkDarkSoulsExposure::gameExposureValue();
    args.hazeEnable = hazeEnable() && hazeValid ? 1u : 0u;
    const bool dof = dofEnable() && gameActive();
    args.dofFarBegin = dofFarBegin();
    args.dofFarEnd = dofFarEnd();
    args.dofFarStrength = dof ? dofFarStrength() : 0.0f;

    ctx->writeToBuffer(m_hazeConstants, 0, sizeof(hazeArgs), &hazeArgs);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_hazeConstants);
    ctx->writeToBuffer(m_sceneConstants, 0, sizeof(args), &args);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_sceneConstants);

    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_INOUT, inOutColor.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_LINEAR_VIEW_Z, linearViewZ.view, nullptr);
    ctx->bindResourceBuffer(POST_FX_DS_LOOK_SCENE_HAZE, DxvkBufferSlice(m_hazeConstants, 0, m_hazeConstants->info().size));
    ctx->bindResourceBuffer(POST_FX_DS_LOOK_SCENE_CONSTANTS, DxvkBufferSlice(m_sceneConstants, 0, m_sceneConstants->info().size));
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookSceneShader::getShader());
    const VkExtent3D workgroups = util::computeBlockCount(extent, VkExtent3D { 16, 8, 1 });
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkDarkSoulsLook::dispatchFilter(Rc<DxvkContext>& ctx, const Rc<DxvkBuffer>& constants,
                                         const Resources::Resource& input, const Resources::Resource& output,
                                         const vec4* taps, uint32_t tapCount, bool gated) {
    const VkExtent3D in = input.image->mipLevelExtent(0);
    const VkExtent3D out = output.image->mipLevelExtent(0);
    PostFxDarkSoulsLookFilterArgs args = {};
    args.outputSize = { out.width, out.height };
    args.invInputSize = { 1.0f / float(in.width), 1.0f / float(in.height) };
    args.invOutputSize = { 1.0f / float(out.width), 1.0f / float(out.height) };
    args.tapCount = std::min(tapCount, uint32_t(DS_LOOK_MAX_TAPS));
    args.gated = gated ? 1u : 0u;
    for (uint32_t i = 0; i < args.tapCount; ++i) {
      args.taps[i] = taps[i];
    }
    ctx->writeToBuffer(constants, 0, sizeof(args), &args);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(constants);

    ctx->bindResourceView(POST_FX_DS_LOOK_FILTER_INPUT, input.view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_FILTER_INPUT, m_linearSampler);
    ctx->bindResourceView(POST_FX_DS_LOOK_FILTER_OUTPUT, output.view, nullptr);
    ctx->bindResourceBuffer(POST_FX_DS_LOOK_FILTER_CONSTANTS, DxvkBufferSlice(constants, 0, constants->info().size));
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookFilterShader::getShader());
    const VkExtent3D workgroups = util::computeBlockCount(out, VkExtent3D { 16, 8, 1 });
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkDarkSoulsLook::dispatchDof(
    Rc<DxvkContext> ctx,
    const Resources::Resource& inOutColor,
    const Resources::Resource& linearViewZ,
    const float jitterOffset[2],
    float missLinearViewZ) {
    if (!dofEnable() || !gameActive()) {
      return;
    }
    ScopedGpuProfileZone(ctx, "Dark Souls 1 Look: DoF");
    ensureResources(ctx, std::max(bloomWidth(), 2), std::max(bloomHeight(), 2));
    const VkExtent3D extent = inOutColor.image->mipLevelExtent(0);
    const VkExtent3D dofExtent = m_dofDown.image->mipLevelExtent(0);
    const VkExtent3D halfExtent = m_dofHalfDown.image->mipLevelExtent(0);

    // FRPG_Fil_Dof_DownSample of the frame (rgb, far rate in alpha).
    {
      PostFxDarkSoulsLookReduceArgs args = {};
      args.outputSize = { dofExtent.width, dofExtent.height };
      args.inputSize = { extent.width, extent.height };
      ctx->writeToBuffer(m_reduceConstants, 0, sizeof(args), &args);
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_reduceConstants);
      ctx->bindResourceView(POST_FX_DS_LOOK_REDUCE_INPUT, inOutColor.view, nullptr);
      ctx->bindResourceView(POST_FX_DS_LOOK_REDUCE_OUTPUT, m_dofDown.view, nullptr);
      ctx->bindResourceBuffer(POST_FX_DS_LOOK_REDUCE_CONSTANTS, DxvkBufferSlice(m_reduceConstants, 0, m_reduceConstants->info().size));
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookReduceShader::getShader());
      const VkExtent3D workgroups = util::computeBlockCount(dofExtent, VkExtent3D { 16, 8, 1 });
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    // The game's order: Unfocus3x3, DownSample, Unfocus3x3, BlurUpSample.
    dispatchFilter(ctx, m_dofFilterConstants[0], m_dofDown, m_dofUnfocus, kUnfocus, 4, true);
    dispatchFilter(ctx, m_dofFilterConstants[1], m_dofDown, m_dofHalfDown, kDownsample2x, 1);
    dispatchFilter(ctx, m_dofFilterConstants[2], m_dofHalfDown, m_dofHalfUnfocus, kUnfocus, 4, true);
    dispatchFilter(ctx, m_dofFilterConstants[3], m_dofHalfUnfocus, m_dofUp, kBlurUpSample, 4, true);

    // FRPG_Fil_Dof_NearRate + UnfocusNearRate3x3.
    {
      const VkExtent3D viewZExtent = linearViewZ.image->mipLevelExtent(0);
      PostFxDarkSoulsLookNearArgs args = {};
      args.outputSize = { halfExtent.width, halfExtent.height };
      args.depthJitterUv = { jitterOffset[0] / float(viewZExtent.width), jitterOffset[1] / float(viewZExtent.height) };
      args.missLinearViewZ = missLinearViewZ;
      args.nearBegin = dofNearBegin();
      args.nearEnd = dofNearEnd();
      args.nearStrength = dofNearStrength();
      ctx->writeToBuffer(m_nearConstants, 0, sizeof(args), &args);
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_nearConstants);
      ctx->bindResourceView(POST_FX_DS_LOOK_NEAR_LINEAR_VIEW_Z, linearViewZ.view, nullptr);
      ctx->bindResourceView(POST_FX_DS_LOOK_NEAR_OUTPUT, m_dofNear.view, nullptr);
      ctx->bindResourceBuffer(POST_FX_DS_LOOK_NEAR_CONSTANTS, DxvkBufferSlice(m_nearConstants, 0, m_nearConstants->info().size));
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookNearShader::getShader());
      const VkExtent3D workgroups = util::computeBlockCount(halfExtent, VkExtent3D { 16, 8, 1 });
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    // FRPG_Fil_Dof.
    {
      PostFxDarkSoulsLookDofArgs args = {};
      args.imageSize = { extent.width, extent.height };
      args.invImageSize = { 1.0f / float(extent.width), 1.0f / float(extent.height) };
      args.blendScale = toVec4(dofBlendScale());
      args.blendBias = toVec4(dofBlendBias());
      ctx->writeToBuffer(m_dofConstants, 0, sizeof(args), &args);
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_dofConstants);
      ctx->bindResourceView(POST_FX_DS_LOOK_DOF_INOUT, inOutColor.view, nullptr);
      ctx->bindResourceView(POST_FX_DS_LOOK_DOF_LEVEL1, m_dofDown.view, nullptr);
      ctx->bindResourceSampler(POST_FX_DS_LOOK_DOF_LEVEL1, m_linearSampler);
      ctx->bindResourceView(POST_FX_DS_LOOK_DOF_LEVEL2, m_dofUnfocus.view, nullptr);
      ctx->bindResourceSampler(POST_FX_DS_LOOK_DOF_LEVEL2, m_linearSampler);
      ctx->bindResourceView(POST_FX_DS_LOOK_DOF_LEVEL3, m_dofUp.view, nullptr);
      ctx->bindResourceSampler(POST_FX_DS_LOOK_DOF_LEVEL3, m_linearSampler);
      ctx->bindResourceView(POST_FX_DS_LOOK_DOF_NEAR, m_dofNear.view, nullptr);
      ctx->bindResourceSampler(POST_FX_DS_LOOK_DOF_NEAR, m_linearSampler);
      ctx->bindResourceBuffer(POST_FX_DS_LOOK_DOF_CONSTANTS, DxvkBufferSlice(m_dofConstants, 0, m_dofConstants->info().size));
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookDofShader::getShader());
      const VkExtent3D workgroups = util::computeBlockCount(extent, VkExtent3D { 16, 8, 1 });
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
  }

  void DxvkDarkSoulsLook::dispatchComposite(
    Rc<DxvkContext> ctx,
    const Resources::Resource& inOutColor,
    const Resources::Resource& linearViewZ,
    const float jitterOffset[2],
    float missLinearViewZ) {
    ScopedGpuProfileZone(ctx, "Dark Souls 1 Look: Bloom and ColAdj");
    ensureResources(ctx, std::max(bloomWidth(), 2), std::max(bloomHeight(), 2));

    const VkExtent3D extent = inOutColor.image->mipLevelExtent(0);
    const bool bloom = bloomEnable() && gameActive();

    if (bloom) {
      const VkExtent3D viewZExtent = linearViewZ.image->mipLevelExtent(0);
      PostFxDarkSoulsLookBrightArgs args = {};
      args.outputSize = { m_baseWidth, m_baseHeight };
      args.sceneSize = { extent.width, extent.height };
      args.depthJitterUv = { jitterOffset[0] / float(viewZExtent.width), jitterOffset[1] / float(viewZExtent.height) };
      args.missLinearViewZ = missLinearViewZ;
      args.thresholdNear = thresholdNear();
      args.thresholdFar = thresholdFar();
      args.depthBegin = depthBegin();
      args.depthEnd = depthEnd();
      ctx->writeToBuffer(m_brightConstants, 0, sizeof(args), &args);
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_brightConstants);

      ctx->bindResourceView(POST_FX_DS_LOOK_BRIGHT_SCENE, inOutColor.view, nullptr);
      ctx->bindResourceView(POST_FX_DS_LOOK_BRIGHT_LINEAR_VIEW_Z, linearViewZ.view, nullptr);
      ctx->bindResourceView(POST_FX_DS_LOOK_BRIGHT_OUTPUT, m_base[0].view, nullptr);
      ctx->bindResourceBuffer(POST_FX_DS_LOOK_BRIGHT_CONSTANTS, DxvkBufferSlice(m_brightConstants, 0, m_brightConstants->info().size));
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookBrightShader::getShader());
      const VkExtent3D workgroups = util::computeBlockCount(VkExtent3D { m_baseWidth, m_baseHeight, 1 }, VkExtent3D { 16, 8, 1 });
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);

      vec4 vertical[15], horizontal[15];
      bloomTaps(vertical, true);
      bloomTaps(horizontal, false);
      // The game's order: GaussBlur5x5, downsample, GaussBlur5x5, Bloom vertical, Bloom horizontal.
      dispatchFilter(ctx, m_filterConstants[0], m_base[0], m_base[1], kGauss5x5, 13);
      dispatchFilter(ctx, m_filterConstants[1], m_base[1], m_half[0], kDownsample2x, 1);
      dispatchFilter(ctx, m_filterConstants[2], m_half[0], m_half[1], kGauss5x5, 13);
      dispatchFilter(ctx, m_filterConstants[3], m_half[1], m_half[0], vertical, 15);
      dispatchFilter(ctx, m_filterConstants[4], m_half[0], m_half[1], horizontal, 15);
    }

    PostFxDarkSoulsLookCompositeArgs args = {};
    args.imageSize = { extent.width, extent.height };
    args.invImageSize = { 1.0f / float(extent.width), 1.0f / float(extent.height) };
    args.bloomEnable = bloom ? 1u : 0u;
    if (gameActive()) {
      args.sceneScale = sceneScale();
      args.bloomRateNear = bloomRateNear();
      args.bloomRateFar = bloomRateFar();
      args.gradeRowR = toVec4(gradeRowR());
      args.gradeRowG = toVec4(gradeRowG());
      args.gradeRowB = toVec4(gradeRowB());
    } else {
      // No ColAdj seen yet: the scene's 1.3 headroom undone, no grade.
      args.sceneScale = 1.3f;
      args.gradeRowR = vec4 { 1, 0, 0, 0 };
      args.gradeRowG = vec4 { 0, 1, 0, 0 };
      args.gradeRowB = vec4 { 0, 0, 1, 0 };
    }
    ctx->writeToBuffer(m_compositeConstants, 0, sizeof(args), &args);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_compositeConstants);

    ctx->bindResourceView(POST_FX_DS_LOOK_COMPOSITE_INOUT, inOutColor.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_COMPOSITE_BLOOM, m_half[1].view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_COMPOSITE_BLOOM, m_linearSampler);
    ctx->bindResourceBuffer(POST_FX_DS_LOOK_COMPOSITE_CONSTANTS, DxvkBufferSlice(m_compositeConstants, 0, m_compositeConstants->info().size));
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookCompositeShader::getShader());
    const VkExtent3D workgroups = util::computeBlockCount(extent, VkExtent3D { 16, 8, 1 });
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkDarkSoulsLook::showImguiSettings() {
    if (!ImGui::CollapsingHeader("Dark Souls image pipeline", ImGuiTreeNodeFlags_DefaultOpen)) {
      return;
    }
    ImGui::Indent();
    RemixGui::Checkbox("Game Image Pipeline", &enableObject());
    RemixGui::Checkbox("Game Fog and Scattering", &hazeEnableObject());
    RemixGui::Checkbox("Game Bloom", &bloomEnableObject());
    RemixGui::Checkbox("Game Depth of Field", &dofEnableObject());
    RemixGui::Checkbox("Gamma-Space Albedo (game texture multiply)", &gammaAlbedoObject());
    if (!gameActive()) {
      ImGui::TextDisabled("No ColAdj values from the game yet.");
    } else {
      ImGui::Text("exposure %.3f  scene scale %.3f", DxvkDarkSoulsExposure::gameExposureValue(), sceneScale());
      ImGui::Text("bloom rate %.3f .. %.3f  threshold %.3f .. %.3f  depth %.1f .. %.1f",
                  bloomRateNear(), bloomRateFar(), thresholdNear(), thresholdFar(), depthBegin(), depthEnd());
      ImGui::Text("bloom buffer %d x %d", bloomWidth(), bloomHeight());
      ImGui::Text("DoF far %.1f .. %.1f x %.2f  near %.2f .. %.2f x %.2f", dofFarBegin(), dofFarEnd(), dofFarStrength(),
                  dofNearBegin(), dofNearEnd(), dofNearStrength());
    }
    ImGui::Unindent();
  }

} // namespace dxvk
