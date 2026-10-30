#include "rtx_dark_souls_look.h"
#include "rtx_dark_souls_ambient.h"
#include "rtx_dark_souls_exposure.h"
#include "rtx_dark_souls_haze.h"
#include "rtx_context.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_imgui.h"
#include "../imgui/imgui.h"

#include <cmath>

#include <rtx_shaders/post_fx_dark_souls_look_scene.h>
#include <rtx_shaders/post_fx_dark_souls_look_region.h>
#include <rtx_shaders/post_fx_dark_souls_look_bright.h>
#include <rtx_shaders/post_fx_dark_souls_look_filter.h>
#include <rtx_shaders/post_fx_dark_souls_look_composite.h>
#include <rtx_shaders/post_fx_dark_souls_look_near.h>
#include <rtx_shaders/post_fx_dark_souls_look_dof.h>
#include <rtx_shaders/post_fx_dark_souls_look_dof_prepare.h>
#include <rtx_shaders/post_fx_dark_souls_look_meter.h>
#include <rtx_shaders/post_fx_dark_souls_look_ambient_meter.h>
#include <rtx_shaders/post_fx_dark_souls_look_shafts.h>

namespace dxvk {

  namespace {
    // The scene pass's gamma-space exposure: the game's (c135.x / c135.y) times the user brightness
    // (rtx.userBrightness, an EV on linear radiance, so 2^(EV / 2.2) here). The meter divides by the same
    // value, so the brightness does not feed back into the game's adaptation, and unlit sprites'
    // exposure compensation (RtxContext::updateRaytraceArgs) divides by its linear equivalent.
    float sceneExposure() {
      return DxvkDarkSoulsExposure::gameExposureValue() * std::exp2(RtxOptions::calcUserEVBias() / 2.2f);
    }
  }

  namespace {
    class PostFxDarkSoulsLookSceneShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsLookSceneShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_look_scene)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(POST_FX_DS_LOOK_SCENE_INOUT)
        TEXTURE2D(POST_FX_DS_LOOK_SCENE_LINEAR_VIEW_Z)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_SCENE_HAZE)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_SCENE_CONSTANTS)
        RW_TEXTURE2D(POST_FX_DS_LOOK_SCENE_DOF_RATE)
        TEXTURE2D(POST_FX_DS_LOOK_SCENE_SURFACE_FLAGS)
        TEXTURE2D(POST_FX_DS_LOOK_SCENE_MOTION_VECTOR)
        SAMPLER2D(POST_FX_DS_LOOK_SCENE_HISTORY_A_IN)
        SAMPLER2D(POST_FX_DS_LOOK_SCENE_HISTORY_B_IN)
        RW_TEXTURE2D(POST_FX_DS_LOOK_SCENE_HISTORY_A_OUT)
        RW_TEXTURE2D(POST_FX_DS_LOOK_SCENE_HISTORY_B_OUT)
        TEXTURE2D(POST_FX_DS_LOOK_SCENE_HAZE_REGION)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookSceneShader);

    class PostFxDarkSoulsLookRegionShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsLookRegionShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_look_region)

      BEGIN_PARAMETER()
        TEXTURE2D(POST_FX_DS_LOOK_REGION_LINEAR_VIEW_Z)
        TEXTURE2D(POST_FX_DS_LOOK_REGION_SURFACE_FLAGS)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_REGION_HAZE)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_REGION_CONSTANTS)
        RW_TEXTURE2D(POST_FX_DS_LOOK_REGION_OUTPUT)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookRegionShader);

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
        SAMPLER2D(POST_FX_DS_LOOK_COMPOSITE_SHAFTS)
        TEXTURE2D(POST_FX_DS_LOOK_COMPOSITE_STOCK)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookCompositeShader);

    class PostFxDarkSoulsLookShaftsShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsLookShaftsShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_look_shafts)

      BEGIN_PARAMETER()
        ACCELERATION_STRUCTURE(POST_FX_DS_LOOK_SHAFTS_TLAS)
        RW_TEXTURE2D(POST_FX_DS_LOOK_SHAFTS_OUTPUT)
        TEXTURE2D(POST_FX_DS_LOOK_SHAFTS_LINEAR_VIEW_Z)
        SAMPLERCUBE(POST_FX_DS_LOOK_SHAFTS_SKY_PROBE)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_SHAFTS_CONSTANTS)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookShaftsShader);

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
        TEXTURE2D(POST_FX_DS_LOOK_DOF_RATE)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookDofShader);

    class PostFxDarkSoulsLookDofPrepareShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsLookDofPrepareShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_look_dof_prepare)

      BEGIN_PARAMETER()
        TEXTURE2D(POST_FX_DS_LOOK_DOF_PREPARE_FRAME)
        TEXTURE2D(POST_FX_DS_LOOK_DOF_PREPARE_RATE)
        RW_TEXTURE2D(POST_FX_DS_LOOK_DOF_PREPARE_OUTPUT)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookDofPrepareShader);

    class PostFxDarkSoulsLookMeterShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsLookMeterShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_look_meter)

      BEGIN_PARAMETER()
        TEXTURE2D(POST_FX_DS_LOOK_METER_SCENE)
        RW_STRUCTURED_BUFFER(POST_FX_DS_LOOK_METER_PARTIALS)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_METER_CONSTANTS)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookMeterShader);

    class PostFxDarkSoulsLookAmbientMeterShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsLookAmbientMeterShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_look_ambient_meter)

      BEGIN_PARAMETER()
        TEXTURE2D(POST_FX_DS_LOOK_AMBIENT_METER_RADIANCE)
        TEXTURE2D(POST_FX_DS_LOOK_AMBIENT_METER_ALBEDO)
        TEXTURE2D(POST_FX_DS_LOOK_AMBIENT_METER_NORMAL)
        TEXTURE2D(POST_FX_DS_LOOK_AMBIENT_METER_LINEAR_VIEW_Z)
        RW_STRUCTURED_BUFFER(POST_FX_DS_LOOK_AMBIENT_METER_PARTIALS)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_AMBIENT_METER_CONSTANTS)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookAmbientMeterShader);

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
    return enable() && intensityClamped() > 0.0f && DxvkDarkSoulsExposure::gameActive();
  }

  const Resources::Resource& DxvkDarkSoulsLook::blendImage(Rc<DxvkContext> ctx, const Resources::Resource& like, uint32_t index) {
    Resources::Resource& image = m_blend[std::min(index, 1u)];
    const VkExtent3D extent = like.image->mipLevelExtent(0);
    const VkFormat format = like.image->info().format;
    if (!image.isValid() || image.image->info().format != format ||
        image.image->mipLevelExtent(0).width != extent.width || image.image->mipLevelExtent(0).height != extent.height) {
      image = Resources::createImageResource(ctx, index == 0 ? "DS1 look blend radiance" : "DS1 look blend stock frame",
                                             extent, format);
    }
    return image;
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
      m_regionConstants = makeConstants(sizeof(PostFxDarkSoulsLookRegionArgs), "DS1 Look Haze Region Constants");
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
      m_shaftConstants = makeConstants(sizeof(PostFxDarkSoulsLookShaftArgs), "DS1 Look Shaft Constants");
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
    m_dofRate = Resources::createImageResource(ctx, "DS1 look DoF far rate", size, VK_FORMAT_R16_SFLOAT);
    for (auto& history : m_depthHistory) {
      history[0] = Resources::createImageResource(ctx, "DS1 look depth terms history A", size, VK_FORMAT_R16G16B16A16_SFLOAT);
      history[1] = Resources::createImageResource(ctx, "DS1 look depth terms history B", size, VK_FORMAT_R16G16B16A16_SFLOAT);
    }
    m_depthHistoryFrame = kInvalidFrameIndex;
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
    const Resources::Resource& surfaceFlags,
    const Resources::Resource& motionVector,
    const Resources::Resource& albedo,
    const Resources::Resource& shadingNormal,
    const DxvkDarkSoulsHaze& haze,
    const Camera& camera,
    const float jitterOffset[2],
    float missLinearViewZ,
    bool resetHistory) {
    ScopedGpuProfileZone(ctx, "Dark Souls 1 Look: Scene");
    ensureResources(ctx, std::max(bloomWidth(), 2), std::max(bloomHeight(), 2));

    // The ambient meter reads the radiance the scene pass is about to overwrite.
    if (DxvkDarkSoulsExposure::wantsMeter()) {
      dispatchAmbientMeter(ctx, inOutColor, albedo, shadingNormal, linearViewZ, missLinearViewZ);
    }

    const VkExtent3D extent = inOutColor.image->mipLevelExtent(0);
    ensureDofResources(ctx, extent);
    PostFxDarkSoulsHazeArgs hazeArgs;
    const bool hazeValid = haze.fillArgs(hazeArgs, extent, linearViewZ, camera, jitterOffset, missLinearViewZ);

    PostFxDarkSoulsLookSceneArgs args = {};
    args.exposure = sceneExposure();
    args.hazeEnable = hazeEnable() && hazeValid ? 1u : 0u;
    const bool dof = dofEnable() && gameActive();
    args.dofFarBegin = dofFarBegin();
    args.dofFarEnd = dofFarEnd();
    args.dofFarStrength = dof ? dofFarStrength() : 0.0f;
    // Without camera jitter the raster does not flip, so there is nothing to stabilize.
    const bool jittered = jitterOffset[0] != 0.0f || jitterOffset[1] != 0.0f;
    args.depthStabilizationTexels = jittered ? std::max(depthStabilizationTexels(), 0.0f) : 0.0f;
    args.temporalBlend = std::clamp(depthTemporalBlend(), 0.01f, 1.0f);
    // The history is last frame's output of this pass; any gap (a frame without the game's image
    // pipeline, a resize), a camera cut or a history reset leaves it describing another image.
    const uint32_t frameId = ctx->getDevice()->getCurrentFrameId();
    args.historyValid = !resetHistory && m_depthHistoryFrame != kInvalidFrameIndex && m_depthHistoryFrame + 1 == frameId ? 1u : 0u;
    const auto& historyIn = m_depthHistory[m_depthHistoryIndex ^ 1];
    const auto& historyOut = m_depthHistory[m_depthHistoryIndex];

    ctx->writeToBuffer(m_hazeConstants, 0, sizeof(hazeArgs), &hazeArgs);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_hazeConstants);
    ctx->writeToBuffer(m_sceneConstants, 0, sizeof(args), &args);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_sceneConstants);

    // The haze region blend of each view Z texel, which the scene pass reads.
    const VkExtent3D viewZExtent = linearViewZ.image->mipLevelExtent(0);
    if (!m_hazeRegion.isValid() || m_hazeRegion.image->mipLevelExtent(0).width != viewZExtent.width ||
        m_hazeRegion.image->mipLevelExtent(0).height != viewZExtent.height) {
      m_hazeRegion = Resources::createImageResource(ctx, "DS1 look haze region", viewZExtent, VK_FORMAT_R32_UINT);
    }
    if (args.hazeEnable != 0) {
      ScopedGpuProfileZone(ctx, "Dark Souls 1 Look: Haze Region");
      PostFxDarkSoulsLookRegionArgs regionArgs = {};
      regionArgs.size = { viewZExtent.width, viewZExtent.height };
      regionArgs.radius = std::max(hazeRegionBlendRadius(), 0.0f);
      regionArgs.maxUvRadius = 0.5f;
      regionArgs.tapCount = 32;
      regionArgs.frameIndex = frameId;
      ctx->writeToBuffer(m_regionConstants, 0, sizeof(regionArgs), &regionArgs);
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_regionConstants);
      ctx->bindResourceView(POST_FX_DS_LOOK_REGION_LINEAR_VIEW_Z, linearViewZ.view, nullptr);
      ctx->bindResourceView(POST_FX_DS_LOOK_REGION_SURFACE_FLAGS, surfaceFlags.view, nullptr);
      ctx->bindResourceBuffer(POST_FX_DS_LOOK_REGION_HAZE, DxvkBufferSlice(m_hazeConstants, 0, m_hazeConstants->info().size));
      ctx->bindResourceBuffer(POST_FX_DS_LOOK_REGION_CONSTANTS, DxvkBufferSlice(m_regionConstants, 0, m_regionConstants->info().size));
      ctx->bindResourceView(POST_FX_DS_LOOK_REGION_OUTPUT, m_hazeRegion.view, nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookRegionShader::getShader());
      const VkExtent3D regionGroups = util::computeBlockCount(viewZExtent, VkExtent3D { 16, 8, 1 });
      ctx->dispatch(regionGroups.width, regionGroups.height, regionGroups.depth);
    }

    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_INOUT, inOutColor.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_LINEAR_VIEW_Z, linearViewZ.view, nullptr);
    ctx->bindResourceBuffer(POST_FX_DS_LOOK_SCENE_HAZE, DxvkBufferSlice(m_hazeConstants, 0, m_hazeConstants->info().size));
    ctx->bindResourceBuffer(POST_FX_DS_LOOK_SCENE_CONSTANTS, DxvkBufferSlice(m_sceneConstants, 0, m_sceneConstants->info().size));
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_DOF_RATE, m_dofRate.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_SURFACE_FLAGS, surfaceFlags.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_MOTION_VECTOR, motionVector.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_HISTORY_A_IN, historyIn[0].view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_SCENE_HISTORY_A_IN, m_linearSampler);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_HISTORY_B_IN, historyIn[1].view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_SCENE_HISTORY_B_IN, m_linearSampler);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_HISTORY_A_OUT, historyOut[0].view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_HISTORY_B_OUT, historyOut[1].view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_HAZE_REGION, m_hazeRegion.view, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookSceneShader::getShader());
    const VkExtent3D workgroups = util::computeBlockCount(extent, VkExtent3D { 16, 8, 1 });
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    m_depthHistoryIndex ^= 1;
    m_depthHistoryFrame = frameId;

    if (DxvkDarkSoulsExposure::wantsMeter()) {
      dispatchMeter(ctx, inOutColor, args.exposure);
    }
  }

  DxvkDarkSoulsLook::MeterGrid DxvkDarkSoulsLook::meterGrid() {
    // The game meters a copy of its scene at its bright pass size (256x180).
    MeterGrid grid;
    grid.width = uint32_t(std::max(bloomWidth(), 1));
    grid.height = uint32_t(std::max(bloomHeight(), 1));
    grid.groupsX = (grid.width + DS_LOOK_METER_GROUP - 1) / DS_LOOK_METER_GROUP;
    grid.groupsY = (grid.height + DS_LOOK_METER_GROUP - 1) / DS_LOOK_METER_GROUP;
    return grid;
  }

  void DxvkDarkSoulsLook::ensureMeterResources(uint32_t groups) {
    if (groups > m_meterGroups) {
      DxvkBufferCreateInfo info;
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      info.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
      info.access = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
      info.size = kMeterFloatsPerGroup * groups * sizeof(float);
      m_meterPartials = m_device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer,
                                               "DS1 look meter partials");
      DxvkBufferCreateInfo readback;
      readback.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      readback.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT;
      readback.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
      readback.size = kMeterSlots * kMeterFloatsPerGroup * groups * sizeof(float);
      m_meterReadback = m_device->createBuffer(readback, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                               DxvkMemoryStats::Category::RTXBuffer, "DS1 look meter readback");
      m_meterGroups = groups;
      for (auto& c : m_meterCells) {
        c = 0;
      }
    }
    if (m_meterConstants == nullptr) {
      m_meterConstants = makeConstants(sizeof(PostFxDarkSoulsLookMeterArgs), "DS1 Look Meter Constants");
      m_ambientMeterConstants = makeConstants(sizeof(PostFxDarkSoulsLookAmbientMeterArgs), "DS1 Look Ambient Meter Constants");
    }
  }

  void DxvkDarkSoulsLook::dispatchAmbientMeter(Rc<DxvkContext>& ctx, const Resources::Resource& radiance,
                                               const Resources::Resource& albedo, const Resources::Resource& shadingNormal,
                                               const Resources::Resource& linearViewZ, float missLinearViewZ) {
    m_ambientMetered = false;
    if (!DxvkDarkSoulsExposure::ambientClamp() || !albedo.isValid() || !shadingNormal.isValid()) {
      return;
    }
    PostFxDarkSoulsLookAmbientMeterArgs args = {};
    DxvkDarkSoulsAmbient::fillShaderArgs(args.ambient);
    if (args.ambient.enable == 0) {
      return;
    }
    ScopedGpuProfileZone(ctx, "Dark Souls 1 Look: Ambient Meter");
    const MeterGrid grid = meterGrid();
    ensureMeterResources(grid.groups());

    const VkExtent3D radianceExtent = radiance.image->mipLevelExtent(0);
    const VkExtent3D gbufferExtent = albedo.image->mipLevelExtent(0);
    const VkExtent3D viewZExtent = linearViewZ.image->mipLevelExtent(0);
    args.gridSize = { grid.width, grid.height };
    args.radianceSize = { radianceExtent.width, radianceExtent.height };
    args.gbufferSize = { gbufferExtent.width, gbufferExtent.height };
    args.viewZSize = { viewZExtent.width, viewZExtent.height };
    args.missLinearViewZ = missLinearViewZ;
    args.minAlbedo = 0.01f;
    args.groupsX = grid.groupsX;
    args.partialsOffset = grid.groups();
    ctx->writeToBuffer(m_ambientMeterConstants, 0, sizeof(args), &args);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_ambientMeterConstants);

    ctx->bindResourceView(POST_FX_DS_LOOK_AMBIENT_METER_RADIANCE, radiance.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_AMBIENT_METER_ALBEDO, albedo.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_AMBIENT_METER_NORMAL, shadingNormal.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_AMBIENT_METER_LINEAR_VIEW_Z, linearViewZ.view, nullptr);
    ctx->bindResourceBuffer(POST_FX_DS_LOOK_AMBIENT_METER_PARTIALS,
                            DxvkBufferSlice(m_meterPartials, 0, kMeterFloatsPerGroup * grid.groups() * sizeof(float)));
    ctx->bindResourceBuffer(POST_FX_DS_LOOK_AMBIENT_METER_CONSTANTS,
                            DxvkBufferSlice(m_ambientMeterConstants, 0, m_ambientMeterConstants->info().size));
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookAmbientMeterShader::getShader());
    ctx->dispatch(grid.groupsX, grid.groupsY, 1);
    m_ambientMetered = true;
  }

  void DxvkDarkSoulsLook::dispatchMeter(Rc<DxvkContext>& ctx, const Resources::Resource& scene, float exposure) {
    ScopedGpuProfileZone(ctx, "Dark Souls 1 Look: Meter");
    const MeterGrid grid = meterGrid();
    const uint32_t cells = grid.width * grid.height;
    const uint32_t groups = grid.groups();
    ensureMeterResources(groups);

    // This frame's slot still holds the sums from kMeterSlots frames ago: the game's lum for an
    // adaptation step (FRPG_Fil_SampleLumFinal: exp of the mean), and with them the ambient meter's.
    const uint32_t slot = m_meterFrame % kMeterSlots;
    const VkDeviceSize slotOffset = VkDeviceSize(slot) * kMeterFloatsPerGroup * m_meterGroups * sizeof(float);
    if (m_meterCells[slot] == cells) {
      const float* sums = reinterpret_cast<const float*>(m_meterReadback->mapPtr(slotOffset));
      double total = 0.0;
      for (uint32_t i = 0; i < groups; ++i) {
        total += sums[i];
      }
      // The frame's luminance against the game's unoccluded ambient on the same surfaces: the share of
      // the game's light the path tracer keeps, which scales the game's luminance clamp.
      float ambientScale = 1.0f;
      if (m_meterAmbient[slot]) {
        double frame = 0.0, estimate = 0.0, taps = 0.0;
        for (uint32_t i = 0; i < groups; ++i) {
          frame += sums[groups + 3 * i + 0];
          estimate += sums[groups + 3 * i + 1];
          taps += sums[groups + 3 * i + 2];
        }
        // Too few surface taps (looking at the sky) say nothing about the light.
        if (taps >= 0.05 * 4.0 * double(cells)) {
          ambientScale = float(std::exp((frame - estimate) / taps));
        }
      }
      DxvkDarkSoulsExposure::adapt(float(std::exp(total / double(cells))), ambientScale);
    }

    const VkExtent3D extent = scene.image->mipLevelExtent(0);
    PostFxDarkSoulsLookMeterArgs args = {};
    args.gridSize = { grid.width, grid.height };
    args.sceneSize = { extent.width, extent.height };
    args.exposure = std::max(exposure, 1e-4f);
    args.groupsX = grid.groupsX;
    ctx->writeToBuffer(m_meterConstants, 0, sizeof(args), &args);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_meterConstants);

    ctx->bindResourceView(POST_FX_DS_LOOK_METER_SCENE, scene.view, nullptr);
    ctx->bindResourceBuffer(POST_FX_DS_LOOK_METER_PARTIALS, DxvkBufferSlice(m_meterPartials, 0, groups * sizeof(float)));
    ctx->bindResourceBuffer(POST_FX_DS_LOOK_METER_CONSTANTS, DxvkBufferSlice(m_meterConstants, 0, m_meterConstants->info().size));
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookMeterShader::getShader());
    ctx->dispatch(grid.groupsX, grid.groupsY, 1);
    ctx->copyBuffer(m_meterReadback, slotOffset, m_meterPartials, 0, kMeterFloatsPerGroup * groups * sizeof(float));
    m_meterCells[slot] = cells;
    m_meterAmbient[slot] = m_ambientMetered;
    m_ambientMetered = false;
    m_meterFrame++;
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

    // The level source: the frame as it is now (passes may have run since the scene pass) with the
    // far rate in alpha, staged in the last level, which is only written after the first two.
    {
      ctx->bindResourceView(POST_FX_DS_LOOK_DOF_PREPARE_FRAME, inOutColor.view, nullptr);
      ctx->bindResourceView(POST_FX_DS_LOOK_DOF_PREPARE_RATE, m_dofRate.view, nullptr);
      ctx->bindResourceView(POST_FX_DS_LOOK_DOF_PREPARE_OUTPUT, m_dofLevel[2].view, nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookDofPrepareShader::getShader());
      const VkExtent3D workgroups = util::computeBlockCount(extent, VkExtent3D { 16, 8, 1 });
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    // Three levels, each from the one before: the increment is the Gaussian that takes the
    // previous spread to the next.
    float previous = 0.0f;
    for (int level = 0; level < 3; ++level) {
      const float step = std::sqrt(std::max(kDofLevelSigma[level] * kDofLevelSigma[level] - previous * previous, 0.0f));
      dispatchDofGaussian(ctx, 2 * level, level == 0 ? m_dofLevel[2] : m_dofLevel[level - 1], m_dofLevel[level],
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
      args.depthStabilizationTexels = std::max(depthStabilizationTexels(), 0.0f);
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
      ctx->bindResourceView(POST_FX_DS_LOOK_DOF_RATE, m_dofRate.view, nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookDofShader::getShader());
      const VkExtent3D workgroups = util::computeBlockCount(extent, VkExtent3D { 16, 8, 1 });
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
  }

  void DxvkDarkSoulsLook::dispatchShafts(
    RtxContext* ctx,
    const VkExtent3D& outputExtent,
    const Resources::Resource& linearViewZ,
    const Camera& camera,
    const float jitterOffset[2],
    float missLinearViewZ) {
    m_shaftsReady = false;
    if (!shaftEnable() || !shaftActive() || !gameActive()) {
      return;
    }
    const auto& tlas = ctx->getResourceManager().getTLAS(Tlas::Opaque).accelStructure;
    if (tlas.ptr() == nullptr) {
      return;
    }
    ScopedGpuProfileZone(ctx, "Dark Souls 1 Look: Light Shafts");
    Rc<DxvkContext> dxvkCtx = ctx;
    ensureResources(dxvkCtx, std::max(bloomWidth(), 2), std::max(bloomHeight(), 2));

    const float scale = std::clamp(shaftResolutionScale(), 0.1f, 1.0f);
    const VkExtent3D extent {
      std::max(uint32_t(float(outputExtent.width) * scale), 1u),
      std::max(uint32_t(float(outputExtent.height) * scale), 1u), 1 };
    if (!m_shafts.isValid() || m_shafts.image->info().extent.width != extent.width ||
        m_shafts.image->info().extent.height != extent.height) {
      m_shafts = Resources::createImageResource(dxvkCtx, "DS1 look light shafts", extent, VK_FORMAT_R16G16B16A16_SFLOAT);
    }

    const Vector3 direction = shaftDirection();
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (!(length > 0.0f)) {
      return;
    }

    const VkExtent3D viewZExtent = linearViewZ.image->mipLevelExtent(0);
    PostFxDarkSoulsLookShaftArgs args = {};
    args.camera = camera;
    DxvkDarkSoulsAmbient::fillShaderArgs(args.ambient);
    args.outputSize = { extent.width, extent.height };
    args.invOutputSize = { 1.0f / float(extent.width), 1.0f / float(extent.height) };
    args.depthJitterUv = { jitterOffset[0] / float(viewZExtent.width), jitterOffset[1] / float(viewZExtent.height) };
    args.missLinearViewZ = missLinearViewZ;
    args.exposure = sceneExposure();
    args.direction = { direction.x / length, direction.y / length, direction.z / length };
    args.threshold = std::min(shaftThreshold(), 0.999f);
    args.cloudDistance = std::max(shaftCloudDistance(), 1.0f);
    args.occlusionDistance = std::max(shaftOcclusionDistance(), 0.0f);
    args.samples = uint32_t(std::clamp(shaftSamples(), 1, 256));
    args.frameIndex = uint32_t(ctx->getDevice()->getCurrentFrameId());
    args.gain = shaftGain();
    args.anisotropy = std::clamp(shaftAnisotropy(), -0.95f, 0.95f);
    args.layerDepth = std::max(shaftLayerDepth(), 1.0f);
    ctx->writeToBuffer(m_shaftConstants, 0, sizeof(args), &args);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_shaftConstants);

    ctx->bindAccelerationStructure(POST_FX_DS_LOOK_SHAFTS_TLAS, tlas);
    ctx->bindResourceView(POST_FX_DS_LOOK_SHAFTS_OUTPUT, m_shafts.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_SHAFTS_LINEAR_VIEW_Z, linearViewZ.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_SHAFTS_SKY_PROBE, ctx->getResourceManager().getSkyProbe(dxvkCtx).view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_SHAFTS_SKY_PROBE, m_linearSampler);
    ctx->bindResourceBuffer(POST_FX_DS_LOOK_SHAFTS_CONSTANTS, DxvkBufferSlice(m_shaftConstants, 0, m_shaftConstants->info().size));
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookShaftsShader::getShader());
    const VkExtent3D workgroups = util::computeBlockCount(extent, VkExtent3D { 16, 8, 1 });
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    m_shaftsReady = true;
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
    args.shaftEnable = m_shaftsReady ? 1u : 0u;
    args.shaftWeight = shaftWeight();
    // intensity below 1: the composite blends over Remix's own frame (m_blend[1], RtxContext), and the grade
    // eases toward identity with it.
    args.intensity = m_blend[1].isValid() ? intensityClamped() : 1.0f;
    if (args.intensity < 1.0f) {
      const float t = args.intensity;
      auto ease = [t](const vec4& row, const vec4& identity) {
        return vec4 { identity.x + (row.x - identity.x) * t, identity.y + (row.y - identity.y) * t,
                      identity.z + (row.z - identity.z) * t, identity.w + (row.w - identity.w) * t };
      };
      args.gradeRowR = ease(args.gradeRowR, vec4 { 1, 0, 0, 0 });
      args.gradeRowG = ease(args.gradeRowG, vec4 { 0, 1, 0, 0 });
      args.gradeRowB = ease(args.gradeRowB, vec4 { 0, 0, 1, 0 });
    }
    ctx->writeToBuffer(m_compositeConstants, 0, sizeof(args), &args);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_compositeConstants);

    ctx->bindResourceView(POST_FX_DS_LOOK_COMPOSITE_INOUT, inOutColor.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_COMPOSITE_BLOOM, m_half[1].view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_COMPOSITE_BLOOM, m_linearSampler);
    // Without shafts this frame the slot still needs a view; the shader does not read it.
    ctx->bindResourceView(POST_FX_DS_LOOK_COMPOSITE_SHAFTS, m_shaftsReady ? m_shafts.view : m_half[1].view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_COMPOSITE_SHAFTS, m_linearSampler);
    m_shaftsReady = false;
    // Without a blend this frame the slot still needs a view; the shader does not read it.
    ctx->bindResourceView(POST_FX_DS_LOOK_COMPOSITE_STOCK, args.intensity < 1.0f ? m_blend[1].view : inOutColor.view, nullptr);
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
    ImGui::BeginDisabled(!enable());
    RemixGui::SliderFloat("Game Look Intensity (0 = Remix's own)", &intensityObject(), 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    RemixGui::Checkbox("Game Fog and Scattering", &hazeEnableObject());
    RemixGui::Checkbox("Game Bloom", &bloomEnableObject());
    RemixGui::Checkbox("Game Depth of Field", &dofEnableObject());
    RemixGui::DragFloat("Fog / DoF Depth Stabilization (texels)", &depthStabilizationTexelsObject(), 0.05f, 0.0f, 4.0f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Fog / DoF Temporal Blend (1 = no history)", &depthTemporalBlendObject(), 0.01f, 0.01f, 1.0f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
    RemixGui::Checkbox("Gamma-Space Albedo (game texture multiply)", &gammaAlbedoObject());
    ImGui::BeginDisabled(!gammaAlbedo());
    RemixGui::DragFloat("Bounce Albedo Power (2.2 = game)", &bounceAlbedoGammaObject(), 0.01f, 0.5f, 2.2f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
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
    if (ImGui::TreeNode("Dev")) {
      RemixGui::Checkbox("Light Shaft Glow (path traced)", &shaftEnableObject());
      ImGui::BeginDisabled(!shaftEnable());
      RemixGui::DragInt("Shaft Samples", &shaftSamplesObject(), 1.0f, 1, 256);
      RemixGui::DragFloat("Shaft Gain", &shaftGainObject(), 0.01f, 0.0f, 8.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Shaft Cloud Distance", &shaftCloudDistanceObject(), 10.0f, 10.0f, 100000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Shaft Haze Layer Depth", &shaftLayerDepthObject(), 10.0f, 1.0f, 100000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Shaft Occlusion Distance", &shaftOcclusionDistanceObject(), 10.0f, 0.0f, 100000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Shaft Anisotropy", &shaftAnisotropyObject(), 0.01f, -0.95f, 0.95f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Shaft Resolution Scale", &shaftResolutionScaleObject(), 0.01f, 0.1f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::EndDisabled();
      if (shaftActive()) {
        const Vector3 d = shaftDirection();
        ImGui::Text("shaft sun %.3f, %.3f, %.3f  threshold %.3f  weight %.2f", d.x, d.y, d.z, shaftThreshold(), shaftWeight());
      } else {
        ImGui::TextDisabled("The game runs no light shaft filter here.");
      }
      ImGui::TreePop();
    }
    ImGui::Unindent();
  }

} // namespace dxvk
