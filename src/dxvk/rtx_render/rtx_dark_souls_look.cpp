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

    // Spread (standard deviation per axis, in texels of the game's 512x360 DoF level) of the
    // game's three DoF levels as they reach the screen, measured by running its chain on point
    // sources: the downsample read back bilinearly, that after Unfocus3x3, and the 256x180
    // Unfocus3x3 after BlurUpSample.
    constexpr float kDofLevelSigma[3] = { 0.41f, 0.84f, 2.02f };

    // One side of a normalised Gaussian as bilinear taps (pairs of texels merged into one tap at
    // their weighted position); the centre texel is not a tap, the gated filter keeps its weight.
    uint32_t gaussianTaps(float sigma, bool vertical, vec4* taps, uint32_t maxTaps) {
      if (sigma <= 0.0f) {
        return 0;
      }
      const int radius = std::max(1, int(std::ceil(3.0f * sigma)));
      float norm = 0.0f;
      for (int k = -radius; k <= radius; ++k) {
        norm += std::exp(-0.5f * float(k * k) / (sigma * sigma));
      }
      uint32_t count = 0;
      for (int k = 1; k <= radius && count + 2 <= maxTaps; k += 2) {
        const float w0 = std::exp(-0.5f * float(k * k) / (sigma * sigma)) / norm;
        const float w1 = k + 1 <= radius ? std::exp(-0.5f * float((k + 1) * (k + 1)) / (sigma * sigma)) / norm : 0.0f;
        const float w = w0 + w1;
        const float o = (float(k) * w0 + float(k + 1) * w1) / w;
        taps[count++] = vertical ? vec4 { 0.0f, o, w, 0.0f } : vec4 { o, 0.0f, w, 0.0f };
        taps[count++] = vertical ? vec4 { 0.0f, -o, w, 0.0f } : vec4 { -o, 0.0f, w, 0.0f };
      }
      return count;
    }
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

  }

  void DxvkDarkSoulsLook::ensureDofResources(Rc<DxvkContext>& ctx, const VkExtent3D& extent) {
    if (m_dofLevel[0].isValid() && extent.width == m_dofExtent.width && extent.height == m_dofExtent.height) {
      return;
    }
    m_dofExtent = extent;
    const VkExtent3D size { extent.width, extent.height, 1 };
    for (auto& level : m_dofLevel) {
      level = Resources::createImageResource(ctx, "DS1 look DoF level", size, VK_FORMAT_R16G16B16A16_SFLOAT);
    }
    m_dofScratch = Resources::createImageResource(ctx, "DS1 look DoF scratch", size, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_dofNear = Resources::createImageResource(ctx, "DS1 look DoF near rate", size, VK_FORMAT_R16_SFLOAT);
  }

  void DxvkDarkSoulsLook::dispatchDofGaussian(Rc<DxvkContext>& ctx, int constantsIndex, const Resources::Resource& input,
                                              const Resources::Resource& output, float sigmaX, float sigmaY) {
    vec4 taps[DS_LOOK_MAX_TAPS];
    const uint32_t horizontal = gaussianTaps(sigmaX, false, taps, DS_LOOK_MAX_TAPS);
    dispatchFilter(ctx, m_dofFilterConstants[constantsIndex], input, m_dofScratch, taps, horizontal, true);
    const uint32_t vertical = gaussianTaps(sigmaY, true, taps, DS_LOOK_MAX_TAPS);
    dispatchFilter(ctx, m_dofFilterConstants[constantsIndex + 1], m_dofScratch, output, taps, vertical, true);
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
    ensureDofResources(ctx, extent);

    // The game's DoF level is twice its bright pass size (512x360 for 256x180): one of its texels
    // covers this many output pixels.
    const float texelX = float(extent.width) / float(2 * m_baseWidth);
    const float texelY = float(extent.height) / float(2 * m_baseHeight);

    // Three levels, each from the one before: the increment is the Gaussian that takes the
    // previous spread to the next.
    float previous = 0.0f;
    for (int level = 0; level < 3; ++level) {
      const float step = std::sqrt(std::max(kDofLevelSigma[level] * kDofLevelSigma[level] - previous * previous, 0.0f));
      dispatchDofGaussian(ctx, 2 * level, level == 0 ? inOutColor : m_dofLevel[level - 1], m_dofLevel[level],
                          step * texelX, step * texelY);
      previous = kDofLevelSigma[level];
    }

    // FRPG_Fil_Dof_NearRate + UnfocusNearRate3x3, at full resolution with the game's footprint
    // (one texel of its half-size level).
    {
      const VkExtent3D viewZExtent = linearViewZ.image->mipLevelExtent(0);
      PostFxDarkSoulsLookNearArgs args = {};
      args.outputSize = { extent.width, extent.height };
      args.depthJitterUv = { jitterOffset[0] / float(viewZExtent.width), jitterOffset[1] / float(viewZExtent.height) };
      args.missLinearViewZ = missLinearViewZ;
      args.nearBegin = dofNearBegin();
      args.nearEnd = dofNearEnd();
      args.nearStrength = dofNearStrength();
      args.dilationUv = { 1.0f / float(m_baseWidth), 1.0f / float(m_baseHeight) };
      ctx->writeToBuffer(m_nearConstants, 0, sizeof(args), &args);
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_nearConstants);
      ctx->bindResourceView(POST_FX_DS_LOOK_NEAR_LINEAR_VIEW_Z, linearViewZ.view, nullptr);
      ctx->bindResourceView(POST_FX_DS_LOOK_NEAR_OUTPUT, m_dofNear.view, nullptr);
      ctx->bindResourceBuffer(POST_FX_DS_LOOK_NEAR_CONSTANTS, DxvkBufferSlice(m_nearConstants, 0, m_nearConstants->info().size));
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookNearShader::getShader());
      const VkExtent3D workgroups = util::computeBlockCount(extent, VkExtent3D { 16, 8, 1 });
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
      ctx->bindResourceView(POST_FX_DS_LOOK_DOF_LEVEL1, m_dofLevel[0].view, nullptr);
      ctx->bindResourceSampler(POST_FX_DS_LOOK_DOF_LEVEL1, m_linearSampler);
      ctx->bindResourceView(POST_FX_DS_LOOK_DOF_LEVEL2, m_dofLevel[1].view, nullptr);
      ctx->bindResourceSampler(POST_FX_DS_LOOK_DOF_LEVEL2, m_linearSampler);
      ctx->bindResourceView(POST_FX_DS_LOOK_DOF_LEVEL3, m_dofLevel[2].view, nullptr);
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
