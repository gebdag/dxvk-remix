#include "rtx_dark_souls_look.h"
#include "rtx_dark_souls_ambient.h"
#include "rtx_dark_souls_exposure.h"
#include "rtx_dark_souls_haze.h"
#include "rtx_dark_souls_ripples.h"
#include "rtx_global_volumetrics.h"
#include "rtx_hdr_output.h"
#include "rtx_scene_manager.h"
#include "rtx_context.h"
#include "rtx_camera.h"
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
#include <chrono>
#include <rtx_shaders/post_fx_dark_souls_look_dof.h>
#include <rtx_shaders/post_fx_dark_souls_look_dof_prepare.h>
#include <rtx_shaders/post_fx_dark_souls_look_eye.h>
#include <rtx_shaders/post_fx_dark_souls_fog_gate_noise.h>
#include <rtx_shaders/post_fx_dark_souls_fog_gate_sim.h>
#include <rtx_shaders/post_fx_dark_souls_fog_gate_march.h>
#include <rtx_shaders/post_fx_dark_souls_look_meter.h>
#include <rtx_shaders/post_fx_dark_souls_look_ambient_meter.h>
#include <rtx_shaders/post_fx_dark_souls_look_shafts.h>
#include "rtx_ds1_menu_frame.h"

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
        SAMPLER2D(POST_FX_DS_LOOK_SCENE_ADDITIVE)
        SAMPLER2D(POST_FX_DS_LOOK_SCENE_EYE)
        SAMPLER2D(POST_FX_DS_LOOK_SCENE_SKY_MATTE)
        SAMPLER2D(POST_FX_DS_LOOK_SCENE_HAZED_LAYERS)
        SAMPLER2D(POST_FX_DS_LOOK_SCENE_HAZED_LAYER_DEPTH)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookSceneShader);

    class PostFxDarkSoulsLookEyeShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsLookEyeShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_look_eye)

      BEGIN_PARAMETER()
        TEXTURE2D(POST_FX_DS_LOOK_EYE_FRAME)
        TEXTURE2D(POST_FX_DS_LOOK_EYE_LINEAR_VIEW_Z)
        TEXTURE2D(POST_FX_DS_LOOK_EYE_HISTORY)
        RW_TEXTURE2D(POST_FX_DS_LOOK_EYE_OUTPUT)
        CONSTANT_BUFFER(POST_FX_DS_LOOK_EYE_CONSTANTS)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsLookEyeShader);

    class PostFxDarkSoulsFogGateNoiseShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsFogGateNoiseShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_fog_gate_noise)

      BEGIN_PARAMETER()
        RW_TEXTURE3D(POST_FX_DS_FOG_GATE_NOISE_OUTPUT)
      END_PARAMETER()
    };

    class PostFxDarkSoulsFogGateSimShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsFogGateSimShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_fog_gate_sim)

      BEGIN_PARAMETER()
        SAMPLER3D(POST_FX_DS_FOG_GATE_SIM_HISTORY)
        RW_TEXTURE3D(POST_FX_DS_FOG_GATE_SIM_OUTPUT)
        CONSTANT_BUFFER(POST_FX_DS_FOG_GATE_SIM_CONSTANTS)
      END_PARAMETER()
    };

    class PostFxDarkSoulsFogGateMarchShader : public ManagedShader {
      SHADER_SOURCE(PostFxDarkSoulsFogGateMarchShader, VK_SHADER_STAGE_COMPUTE_BIT, post_fx_dark_souls_fog_gate_march)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(POST_FX_DS_FOG_GATE_MARCH_INOUT)
        TEXTURE2D(POST_FX_DS_FOG_GATE_MARCH_VIEW_Z)
        SAMPLER3D(POST_FX_DS_FOG_GATE_MARCH_NOISE)
        SAMPLER3D(POST_FX_DS_FOG_GATE_MARCH_SIM)
        SAMPLER3D(POST_FX_DS_FOG_GATE_MARCH_RADIANCE_Y)
        SAMPLER3D(POST_FX_DS_FOG_GATE_MARCH_RADIANCE_COCG)
        CONSTANT_BUFFER(POST_FX_DS_FOG_GATE_MARCH_HAZE)
        CONSTANT_BUFFER(POST_FX_DS_FOG_GATE_MARCH_CONSTANTS)
        RW_TEXTURE2D(POST_FX_DS_FOG_GATE_MARCH_DOF_RATE)
        RW_TEXTURE2D(POST_FX_DS_FOG_GATE_MARCH_MASK)
      END_PARAMETER()
    };
    // The first gate must not stall the game.
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsFogGateNoiseShader);
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsFogGateSimShader);
    PREWARM_SHADER_PIPELINE(PostFxDarkSoulsFogGateMarchShader);

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
        TEXTURE2D(POST_FX_DS_LOOK_BRIGHT_FOG_GATE_MASK)
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
        SAMPLERCUBE(POST_FX_DS_LOOK_SHAFTS_SOURCE)
        SAMPLERCUBE(POST_FX_DS_LOOK_SHAFTS_INPUT)
        RW_TEXTURE2DARRAY(POST_FX_DS_LOOK_SHAFTS_OUTPUT_CUBE)
        RW_TEXTURE2D(POST_FX_DS_LOOK_SHAFTS_OUTPUT)
        TEXTURE2D(POST_FX_DS_LOOK_SHAFTS_LINEAR_VIEW_Z)
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
        TEXTURE2D(POST_FX_DS_LOOK_METER_LIFT)
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
    // Side of the light shaft chain's cubes: 0.35 degrees a texel, about the game's 256-wide buffer at its
    // field of view.
    constexpr uint32_t kShaftCubeSize = 256;

    // FRPG_Fil_GaussBlur5x5: a 13-tap diamond, a sigma 1 Gaussian normalised to 1.
    constexpr float kGaussCentre = 0.183858f, kGaussEdge = 0.111515f, kGaussDiagonal = 0.067638f, kGaussFar = 0.024882f;
    const vec4 kGauss5x5[13] = {
      { -2, 0, kGaussFar, 0 }, { -1, -1, kGaussDiagonal, 0 }, { -1, 0, kGaussEdge, 0 }, { -1, 1, kGaussDiagonal, 0 },
      { 0, -2, kGaussFar, 0 }, { 0, -1, kGaussEdge, 0 }, { 0, 0, kGaussCentre, 0 }, { 0, 1, kGaussEdge, 0 },
      { 0, 2, kGaussFar, 0 }, { 1, -1, kGaussDiagonal, 0 }, { 1, 0, kGaussEdge, 0 }, { 1, 1, kGaussDiagonal, 0 },
      { 2, 0, kGaussFar, 0 },
    };
    // FRPG_Fil_Bloom: 15 taps at whole texels, 2 x a sigma 3 Gaussian (the weights sum to 1.976), alpha weights all 1. Its
    // targets are 8-bit in the game, so each pass clips at 1, which is what keeps this gain in check (filter's gated = 2).
    const float kBloomWeights[8] = { 0.265961f, 0.251589f, 0.212965f, 0.161314f, 0.10934f, 0.066318f, 0.035994f, 0.017481f };
    // FRPG_Fil_Dof_DownSample at half size: one bilinear tap on the corner of four texels.
    const vec4 kDownsample2x[1] = { { 0, 0, 1, 0 } };

    void bloomTaps(vec4 (&taps)[15], bool vertical) {
      taps[0] = vec4 { 0, 0, kBloomWeights[0], 1 };
      for (int i = 1; i < 8; ++i) {
        const float o = float(i);
        taps[i] = vertical ? vec4 { 0, o, kBloomWeights[i], 1 } : vec4 { o, 0, kBloomWeights[i], 1 };
        taps[i + 7] = vertical ? vec4 { 0, -o, kBloomWeights[i], 1 } : vec4 { -o, 0, kBloomWeights[i], 1 };
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
    m_fogGateRepeatSampler = device->getCommon()->getResources().getSampler(
      VK_FILTER_LINEAR,
      VK_SAMPLER_MIPMAP_MODE_NEAREST,
      VK_SAMPLER_ADDRESS_MODE_REPEAT);
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
      m_eyeConstants = makeConstants(sizeof(PostFxDarkSoulsLookEyeArgs), "DS1 Look Eye Constants");
      m_fogGateSimConstants = makeConstants(sizeof(DsFogGateSimArgs), "DS1 Fog Gate Sim Constants");
      m_fogGateMarchConstants = makeConstants(sizeof(DsFogGateMarchArgs), "DS1 Fog Gate March Constants");
      const VkExtent3D fogGateGrid { DS_FOG_GATE_SIM_X, DS_FOG_GATE_SIM_Y, DS_FOG_GATE_SIM_Z };
      m_fogGateSim[0] = Resources::createImageResource(ctx, "DS1 fog gate sim", fogGateGrid, VK_FORMAT_R16G16B16A16_SFLOAT, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);
      m_fogGateSim[1] = Resources::createImageResource(ctx, "DS1 fog gate sim", fogGateGrid, VK_FORMAT_R16G16B16A16_SFLOAT, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);
      m_fogGateNoise = Resources::createImageResource(ctx, "DS1 fog gate noise", VkExtent3D { DS_FOG_GATE_NOISE_SIZE, DS_FOG_GATE_NOISE_SIZE, DS_FOG_GATE_NOISE_SIZE }, VK_FORMAT_R16_SFLOAT, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);
      m_eyeLevel[0] = Resources::createImageResource(ctx, "DS1 look eye level", VkExtent3D { 64, 36, 1 }, VK_FORMAT_R16G16_SFLOAT);
      m_eyeLevel[1] = Resources::createImageResource(ctx, "DS1 look eye level", VkExtent3D { 64, 36, 1 }, VK_FORMAT_R16G16_SFLOAT);
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
      for (auto& c : m_shaftConstants) {
        c = makeConstants(sizeof(PostFxDarkSoulsLookShaftArgs), "DS1 Look Shaft Constants");
      }
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
    const bool fullRes = dofFullRes();
    const bool sameExtent = m_dofLevel[0].isValid() && extent.width == m_dofExtent.width && extent.height == m_dofExtent.height;
    if (sameExtent && fullRes == m_dofIsFullRes) {
      return;
    }
    m_dofIsFullRes = fullRes;
    const VkExtent3D levelSize = fullRes ? VkExtent3D { extent.width, extent.height, 1 }
                                         : VkExtent3D { std::max((extent.width + 1) / 2, 1u), std::max((extent.height + 1) / 2, 1u), 1 };
    for (auto& level : m_dofLevel) {
      level = Resources::createImageResource(ctx, "DS1 look DoF level", levelSize, VK_FORMAT_R16G16B16A16_SFLOAT);
    }
    m_dofScratch = Resources::createImageResource(ctx, "DS1 look DoF scratch", levelSize, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_dofNear = Resources::createImageResource(ctx, "DS1 look DoF near rate", levelSize, VK_FORMAT_R16_SFLOAT);
    if (sameExtent) {
      return;
    }
    m_dofExtent = extent;
    const VkExtent3D size { extent.width, extent.height, 1 };
    m_dofRate = Resources::createImageResource(ctx, "DS1 look DoF far rate", size, VK_FORMAT_R16G16_SFLOAT);
    m_fogGateMask = Resources::createImageResource(ctx, "DS1 fog gate mask", size, VK_FORMAT_R16G16_SFLOAT);
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
    dispatchFilter(ctx, m_dofFilterConstants[constantsIndex], input, m_dofScratch, taps, horizontal, 1);
    const uint32_t vertical = gaussianTaps(sigmaY, true, taps, DS_LOOK_MAX_TAPS);
    dispatchFilter(ctx, m_dofFilterConstants[constantsIndex + 1], m_dofScratch, output, taps, vertical, 1);
  }

  bool DxvkDarkSoulsLook::dispatchSceneBeforeUpscale(
    Rc<DxvkContext> ctx,
    const Resources::Resource& compositeOutput,
    const Resources::Resource& linearViewZ,
    const Resources::Resource& surfaceFlags,
    const Resources::Resource& motionVector,
    const Resources::Resource& albedo,
    const Resources::Resource& shadingNormal,
    const Resources::Resource& additiveRadiance,
    const Resources::Resource& hazedLayers,
    const Resources::Resource& hazedLayerDepth,
    const DxvkDarkSoulsHaze& haze,
    const Camera& camera,
    float missLinearViewZ,
    bool resetHistory) {
    // Only at the composite's own size, which is view Z's: then each pixel is its own view Z texel.
    const VkExtent3D colorExtent = compositeOutput.image->mipLevelExtent(0);
    const VkExtent3D viewZExtent = linearViewZ.image->mipLevelExtent(0);
    if (!hazeBeforeUpscale() || colorExtent.width != viewZExtent.width || colorExtent.height != viewZExtent.height) {
      return false;
    }
    const float noJitter[2] = { 0.0f, 0.0f };
    m_sceneMode = 1;
    dispatchScene(ctx, compositeOutput, linearViewZ, surfaceFlags, motionVector, albedo, shadingNormal, additiveRadiance, hazedLayers, hazedLayerDepth,
                  haze, camera, noJitter, missLinearViewZ, resetHistory);
    m_sceneMode = 0;
    m_preFrame = ctx->getDevice()->getCurrentFrameId();
    return true;
  }

  void DxvkDarkSoulsLook::dispatchScene(
    Rc<DxvkContext> ctx,
    const Resources::Resource& inOutColor,
    const Resources::Resource& linearViewZ,
    const Resources::Resource& surfaceFlags,
    const Resources::Resource& motionVector,
    const Resources::Resource& albedo,
    const Resources::Resource& shadingNormal,
    const Resources::Resource& additiveRadiance,
    const Resources::Resource& hazedLayers,
    const Resources::Resource& hazedLayerDepth,
    const DxvkDarkSoulsHaze& haze,
    const Camera& camera,
    const float jitterOffset[2],
    float missLinearViewZ,
    bool resetHistory) {
    ScopedGpuProfileZone(ctx, "Dark Souls 1 Look: Scene");
    ensureResources(ctx, std::max(bloomWidth(), 2), std::max(bloomHeight(), 2));
    const uint32_t frameId = ctx->getDevice()->getCurrentFrameId();
    // 1: called by dispatchSceneBeforeUpscale on the composite. 2: the upscaled frame after it (only the clip, DoF rate and meter left).
    const uint32_t mode = m_sceneMode == 1 ? 1u : (m_preFrame == frameId ? 2u : 0u);

    // The ambient meter reads the radiance the scene pass is about to overwrite.
    if (mode != 2 && DxvkDarkSoulsExposure::wantsMeter()) {
      dispatchAmbientMeter(ctx, inOutColor, albedo, shadingNormal, linearViewZ, missLinearViewZ);
    }

    const VkExtent3D extent = inOutColor.image->mipLevelExtent(0);
    if (mode == 1) {
      if (!m_preLifted.isValid() || m_preLifted.image->mipLevelExtent(0).width != extent.width ||
          m_preLifted.image->mipLevelExtent(0).height != extent.height) {
        m_preLifted = Resources::createImageResource(ctx, "DS1 look lift before upscale", extent, VK_FORMAT_R16G16_SFLOAT);
      }
    } else {
      ensureDofResources(ctx, extent);
    }
    PostFxDarkSoulsHazeArgs hazeArgs;
    const bool hazeValid = haze.fillArgs(hazeArgs, extent, linearViewZ, camera, jitterOffset, missLinearViewZ);

    PostFxDarkSoulsLookSceneArgs args = {};
    args.mode = mode;
    args.exposure = sceneExposure();
    const bool layerTargets = hazedLayers.isValid() && hazedLayerDepth.isValid();
    // What this frame's resolve was told (setLayerHazeMode): in mode 2 the layers are only in these targets.
    args.layersOverHaze = layerTargets ? m_layerHazeMode : 0u;
    args.layerFogThrough = layerTargets ? std::clamp(layerFogThrough(), 0.0f, 1.0f) : 0.0f;
    args.layerOwnHaze = std::clamp(layerOwnHaze(), 0.0f, 1.0f);
    // With no meter (a menu's character view, or metering off) there is nothing to go by: the eye returns to no lift, not to stay where it was.
    if (mode != 2 && !DxvkDarkSoulsExposure::wantsMeter()) {
      updateEye(0.0f, 1.0f, false);
    }
    args.eyeViewGain = eyeEnable() && mode != 2 ? m_eyeViewGain : 1.0f;
    if (mode != 2) {
      m_eyeLifted = args.eyeViewGain > 1.0f;
    }
    args.eyeLocal = std::clamp(eyeLocal(), 0.0f, 2.0f);
    args.eyeMaxGain = std::max(eyeMaxGain(), 1.0f);
    // HDR10 output: the scene keeps what the game's 8-bit target would clip (rtx_hdr_output.h).
    args.keepHeadroom = DxvkHdrOutput::renderHeadroom() && DxvkHdrOutput::ds1Headroom() ? 1u : 0u;
    // The sky as the composite maps it (RtxComposite), for the eye's sky test at silhouettes.
    const Resources::Resource skyMatte = static_cast<RtxContext*>(ctx.ptr())->getResourceManager().getSkyMatte(ctx);
    const float skyExposure = DxvkDarkSoulsExposure::drawnExposure();
    args.skyGamma = RtxOptions::skyForceHDR() ? 1u : 2u;
    args.skyUnexpose = skyExposure > 0.0f ? 1.0f / skyExposure : 1.0f;
    args.skyBrightness = ds1SkyBrightness();
    args.eyeSkyMatch = skyMatte.isValid() ? std::max(eyeSkyMatch(), 0.0f) : 0.0f;
    args.hazeEnable = hazeEnable() && hazeValid && mode != 2 ? 1u : 0u;
    const bool dof = dofEnable() && gameActive();
    args.dofFarBegin = dofFarBegin();
    args.dofFarEnd = dofFarEnd();
    args.dofFarStrength = dof ? dofFarStrength() : 0.0f;
    // Without camera jitter the raster does not flip, so there is nothing to stabilize.
    // Before the upscaler each pixel is its own view Z texel: nothing to stabilize, and the history is the upscaled pass's.
    const bool jittered = jitterOffset[0] != 0.0f || jitterOffset[1] != 0.0f;
    args.depthStabilizationTexels = jittered && mode != 1 ? std::max(depthStabilizationTexels(), 0.0f) : 0.0f;
    args.temporalBlend = std::clamp(depthTemporalBlend(), 0.01f, 1.0f);
    // The history is last frame's output of this pass; any gap (a frame without the game's image
    // pipeline, a resize), a camera cut or a history reset leaves it describing another image.
    args.historyValid = mode != 1 && !resetHistory && m_depthHistoryFrame != kInvalidFrameIndex && m_depthHistoryFrame + 1 == frameId ? 1u : 0u;
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

    // The eye's level of each part of the view, from the radiance the scene pass is about to overwrite. Nothing to do while the view takes no lift.
    if (args.eyeViewGain > 1.0f && mode != 2) {
      ScopedGpuProfileZone(ctx, "Dark Souls 1 Look: Eye");
      const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
      const float dt = float(std::clamp(now - m_eyeTime, 0.0, 0.5));
      m_eyeTime = now;
      PostFxDarkSoulsLookEyeArgs eyeArgs = {};
      eyeArgs.frameSize = { extent.width, extent.height };
      eyeArgs.outputSize = { 64, 36 };
      eyeArgs.viewZSize = { viewZExtent.width, viewZExtent.height };
      eyeArgs.exposure = args.exposure;
      eyeArgs.missLinearViewZ = missLinearViewZ;
      eyeArgs.riseBlend = 1.0f - std::exp(-dt / std::max(eyeBrightenSeconds(), 0.01f));
      eyeArgs.fallBlend = 1.0f - std::exp(-dt / std::max(eyeDarkenSeconds(), 0.01f));
      eyeArgs.historyValid = m_eyeFrame != 0xFFFFFFFFu && m_eyeFrame + 1 == frameId && !resetHistory ? 1u : 0u;
      m_eyeFrame = frameId;
      m_eyeIndex ^= 1;
      ctx->writeToBuffer(m_eyeConstants, 0, sizeof(eyeArgs), &eyeArgs);
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_eyeConstants);
      ctx->bindResourceView(POST_FX_DS_LOOK_EYE_FRAME, inOutColor.view, nullptr);
      ctx->bindResourceView(POST_FX_DS_LOOK_EYE_LINEAR_VIEW_Z, linearViewZ.view, nullptr);
      ctx->bindResourceView(POST_FX_DS_LOOK_EYE_HISTORY, m_eyeLevel[m_eyeIndex ^ 1].view, nullptr);
      ctx->bindResourceView(POST_FX_DS_LOOK_EYE_OUTPUT, m_eyeLevel[m_eyeIndex].view, nullptr);
      ctx->bindResourceBuffer(POST_FX_DS_LOOK_EYE_CONSTANTS, DxvkBufferSlice(m_eyeConstants, 0, m_eyeConstants->info().size));
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookEyeShader::getShader());
      ctx->dispatch(8, 5, 1);
    }

    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_INOUT, inOutColor.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_EYE, m_eyeLevel[m_eyeIndex].view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_SCENE_EYE, m_linearSampler);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_LINEAR_VIEW_Z, linearViewZ.view, nullptr);
    ctx->bindResourceBuffer(POST_FX_DS_LOOK_SCENE_HAZE, DxvkBufferSlice(m_hazeConstants, 0, m_hazeConstants->info().size));
    ctx->bindResourceBuffer(POST_FX_DS_LOOK_SCENE_CONSTANTS, DxvkBufferSlice(m_sceneConstants, 0, m_sceneConstants->info().size));
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_DOF_RATE, mode == 1 ? m_preLifted.view : m_dofRate.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_SURFACE_FLAGS, surfaceFlags.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_MOTION_VECTOR, motionVector.view, nullptr);
    // Mode 1 neither reads nor writes the history (it may not exist yet on the first frame): any view of its own stands in.
    const bool historyBound = mode != 1 && historyIn[0].isValid() && historyOut[0].isValid();
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_HISTORY_A_IN, historyBound ? historyIn[0].view : m_preLifted.view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_SCENE_HISTORY_A_IN, m_linearSampler);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_HISTORY_B_IN, historyBound ? historyIn[1].view : m_preLifted.view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_SCENE_HISTORY_B_IN, m_linearSampler);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_HISTORY_A_OUT, historyBound ? historyOut[0].view : m_preLifted.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_HISTORY_B_OUT, historyBound ? historyOut[1].view : m_preLifted.view, nullptr);
    // What the lift before the upscaler added (mode 2); otherwise unread, any view stands in.
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_PRE_LIFTED, m_preLifted.isValid() ? m_preLifted.view : additiveRadiance.view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_SCENE_PRE_LIFTED, m_linearSampler);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_HAZED_LAYERS, hazedLayers.isValid() ? hazedLayers.view : additiveRadiance.view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_SCENE_HAZED_LAYERS, m_linearSampler);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_HAZED_LAYER_DEPTH, hazedLayerDepth.isValid() ? hazedLayerDepth.view : additiveRadiance.view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_SCENE_HAZED_LAYER_DEPTH, m_linearSampler);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_HAZE_REGION, m_hazeRegion.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_ADDITIVE, additiveRadiance.view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_SCENE_ADDITIVE, m_linearSampler);
    // Without a sky this frame the test is off (args.eyeSkyMatch 0) and any view stands in.
    ctx->bindResourceView(POST_FX_DS_LOOK_SCENE_SKY_MATTE, skyMatte.isValid() ? skyMatte.view : additiveRadiance.view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_SCENE_SKY_MATTE, m_linearSampler);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookSceneShader::getShader());
    const VkExtent3D workgroups = util::computeBlockCount(extent, VkExtent3D { 16, 8, 1 });
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    if (mode == 1) {
      return;
    }
    m_depthHistoryIndex ^= 1;
    m_depthHistoryFrame = frameId;

    if (DxvkDarkSoulsExposure::wantsMeter()) {
      dispatchMeter(ctx, inOutColor, args.exposure);
    }
    // After the meter: the game's exposure does not follow a fog of our making.
    dispatchFogGates(ctx, inOutColor, linearViewZ, args.exposure, extent);
  }

  void DxvkDarkSoulsLook::dispatchFogGates(Rc<DxvkContext>& ctx, const Resources::Resource& color, const Resources::Resource& linearViewZ, float exposure, const VkExtent3D& extent) {
    m_fogGateMaskUsed = false;
    const Vector4 sentA[DS_FOG_GATE_MAX] = { gate0A(), gate1A(), gate2A(), gate3A() };
    const Vector4 sentB[DS_FOG_GATE_MAX] = { gate0B(), gate1B(), gate2B(), gate3B() };

    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const float dt = float(std::clamp(now - m_fogGateClock, 0.0, 0.05));
    m_fogGateClock = now;
    m_fogGateTime += dt;

    DsFogGateMarchArgs march = {};
    uint32_t count = 0;
    const Vector3 feet = DxvkDarkSoulsRipples::playerPosition();
    float player[3] = { feet.x, feet.y, feet.z };
    int nearest = -1;
    float nearestDistance = debugWalk() ? 1e9f : std::max(simRange(), 0.0f);
    const float level = debugLevel();
    for (uint32_t i = 0; i < DS_FOG_GATE_MAX; ++i) {
      const float gateLevel = level >= 0.0f && sentA[i].w > 0.0f ? level : sentA[i].w;
      if (gateLevel <= 0.0f || sentB[i].y <= 0.0f) {
        continue;
      }
      DsFogGate& gate = march.gates[count];
      gate.centreLevel = vec4 { sentA[i].x, sentA[i].y, sentA[i].z, std::min(gateLevel, 1.0f) };
      const float c = std::cos(sentB[i].x), s = std::sin(sentB[i].x);
      gate.axisUHalfWidth = vec4 { c, 0.0f, s, sentB[i].y };
      gate.axisWHalfThick = vec4 { -s, 0.0f, c, sentB[i].w };
      gate.halfHeightSim = vec4 { sentB[i].z, 0.0f, 0.0f, 0.0f };
      const float dx = sentA[i].x - player[0], dz = sentA[i].z - player[2];
      const float distance = std::sqrt(dx * dx + dz * dz);
      if (distance < nearestDistance) {
        nearestDistance = distance;
        nearest = int(count);
      }
      ++count;
    }
    if (count == 0) {
      m_fogGateSimGate = -1;
      return;
    }

    ScopedGpuProfileZone(ctx, "Dark Souls 1 Look: Fog Gates");
    if (!m_fogGateNoiseBaked) {
      m_fogGateNoiseBaked = true;
      ctx->bindResourceView(POST_FX_DS_FOG_GATE_NOISE_OUTPUT, m_fogGateNoise.view, nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsFogGateNoiseShader::getShader());
      ctx->dispatch(DS_FOG_GATE_NOISE_SIZE / 4, DS_FOG_GATE_NOISE_SIZE / 4, DS_FOG_GATE_NOISE_SIZE / 4);
    }

    // The nearest gate's fog, with the body in it.
    if (nearest >= 0) {
      DsFogGate& gate = march.gates[nearest];
      gate.halfHeightSim.y = 1.0f;
      const bool sameGate = m_fogGateSimGate >= 0 && m_fogGateSimKey[0] == gate.centreLevel.x && m_fogGateSimKey[1] == gate.centreLevel.y && m_fogGateSimKey[2] == gate.centreLevel.z;
      m_fogGateSimKey[0] = gate.centreLevel.x;
      m_fogGateSimKey[1] = gate.centreLevel.y;
      m_fogGateSimKey[2] = gate.centreLevel.z;
      if (debugWalk()) {
        m_fogGateWalk += dt * std::max(debugWalkSpeed(), 0.0f);
        // Back and forth, 4 m either side of the plane, with a pause out of it.
        const float along = std::abs(std::fmod(m_fogGateWalk, 16.0f) - 8.0f) - 4.0f;
        player[0] = gate.centreLevel.x + gate.axisWHalfThick.x * along;
        player[1] = gate.centreLevel.y - gate.halfHeightSim.x;
        player[2] = gate.centreLevel.z + gate.axisWHalfThick.z * along;
      }
      DsFogGateSimArgs sim = {};
      sim.gate = gate;
      sim.player = vec3 { player[0], player[1], player[2] };
      if (m_fogGateHavePlayer && dt > 0.0f) {
        sim.playerVelocity = vec3 { (player[0] - m_fogGatePlayer[0]) / dt, (player[1] - m_fogGatePlayer[1]) / dt, (player[2] - m_fogGatePlayer[2]) / dt };
        // A warp is no walk.
        const float speed = std::sqrt(sim.playerVelocity.x * sim.playerVelocity.x + sim.playerVelocity.y * sim.playerVelocity.y + sim.playerVelocity.z * sim.playerVelocity.z);
        if (speed > 20.0f) {
          sim.playerVelocity = vec3 { 0.0f, 0.0f, 0.0f };
        }
      }
      std::memcpy(m_fogGatePlayer, player, sizeof(player));
      m_fogGateHavePlayer = true;
      sim.dt = std::max(dt, 1e-3f);
      sim.recoverSeconds = recoverSeconds();
      sim.radius = bodyRadius();
      sim.height = bodyHeight();
      sim.historyValid = sameGate ? 1u : 0u;
      m_fogGateSimGate = nearest;
      m_fogGateSimIndex ^= 1;
      ctx->writeToBuffer(m_fogGateSimConstants, 0, sizeof(sim), &sim);
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_fogGateSimConstants);
      ctx->bindResourceView(POST_FX_DS_FOG_GATE_SIM_HISTORY, m_fogGateSim[m_fogGateSimIndex ^ 1].view, nullptr);
      ctx->bindResourceSampler(POST_FX_DS_FOG_GATE_SIM_HISTORY, m_linearSampler);
      ctx->bindResourceView(POST_FX_DS_FOG_GATE_SIM_OUTPUT, m_fogGateSim[m_fogGateSimIndex].view, nullptr);
      ctx->bindResourceBuffer(POST_FX_DS_FOG_GATE_SIM_CONSTANTS, DxvkBufferSlice(m_fogGateSimConstants, 0, m_fogGateSimConstants->info().size));
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsFogGateSimShader::getShader());
      ctx->dispatch(DS_FOG_GATE_SIM_X / 4, DS_FOG_GATE_SIM_Y / 4, DS_FOG_GATE_SIM_Z / 4);
    } else {
      m_fogGateSimGate = -1;
      m_fogGateHavePlayer = false;
    }

    auto& common = *ctx->getCommonObjects();
    auto& volumetrics = common.metaGlobalVolumetrics();
    march.volumeArgs = volumetrics.getVolumeArgs(common.getSceneManager().getCameraManager(), common.getSceneManager().getFogState(), false);
    DxvkDarkSoulsAmbient::fillShaderArgs(march.ambient);
    const Vector3 ownLight = glow();
    march.glow = vec3 { ownLight.x, ownLight.y, ownLight.z };
    march.density = density();
    march.gateCount = count;
    march.maxSteps = uint32_t(std::clamp(maxSteps(), 4, 128));
    march.exposure = exposure;
    march.time = float(std::fmod(m_fogGateTime, 3600.0));
    march.lightGain = lightGain();
    march.ambientGain = ambientGain();
    march.useCache = useCache() ? 1u : 0u;
    march.shapeLow = shapeLow();
    march.shapeGain = shapeGain();
    march.selfShadow = selfShadow();
    march.solid = solid();
    march.contact = contact();
    march.ambientSaturation = ambientSaturation();
    march.dofFarBegin = dofFarBegin();
    march.dofFarEnd = dofFarEnd();
    march.dofFarStrength = dofEnable() ? dofFarStrength() : 0.0f;
    m_fogGateMaskUsed = true;
    ctx->writeToBuffer(m_fogGateMarchConstants, 0, sizeof(march), &march);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_fogGateMarchConstants);
    ctx->bindResourceView(POST_FX_DS_FOG_GATE_MARCH_INOUT, color.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_FOG_GATE_MARCH_DOF_RATE, m_dofRate.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_FOG_GATE_MARCH_MASK, m_fogGateMask.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_FOG_GATE_MARCH_VIEW_Z, linearViewZ.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_FOG_GATE_MARCH_NOISE, m_fogGateNoise.view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_FOG_GATE_MARCH_NOISE, m_fogGateRepeatSampler);
    ctx->bindResourceView(POST_FX_DS_FOG_GATE_MARCH_SIM, m_fogGateSim[m_fogGateSimIndex].view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_FOG_GATE_MARCH_SIM, m_linearSampler);
    ctx->bindResourceView(POST_FX_DS_FOG_GATE_MARCH_RADIANCE_Y, volumetrics.getCurrentVolumeAccumulatedRadianceY().view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_FOG_GATE_MARCH_RADIANCE_Y, m_linearSampler);
    ctx->bindResourceView(POST_FX_DS_FOG_GATE_MARCH_RADIANCE_COCG, volumetrics.getCurrentVolumeAccumulatedRadianceCoCg().view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_FOG_GATE_MARCH_RADIANCE_COCG, m_linearSampler);
    ctx->bindResourceBuffer(POST_FX_DS_FOG_GATE_MARCH_HAZE, DxvkBufferSlice(m_hazeConstants, 0, m_hazeConstants->info().size));
    ctx->bindResourceBuffer(POST_FX_DS_FOG_GATE_MARCH_CONSTANTS, DxvkBufferSlice(m_fogGateMarchConstants, 0, m_fogGateMarchConstants->info().size));
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsFogGateMarchShader::getShader());
    const VkExtent3D workgroups = util::computeBlockCount(extent, VkExtent3D { 16, 8, 1 });
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
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

  void DxvkDarkSoulsLook::updateEye(float lum, float gameLightShare, bool haveShare) {
    // How much the view is short of, as a lift. The game's meter keeps a view at its key down to the floor of its luminance clamp, and an eye goes on
    // where it stops: floor / lum is what following it all the way would take. But never past the game's own light on these surfaces (the frame
    // against the game's unoccluded ambient on them): where path tracing keeps that light, a dark place is dark in the game too (Tomb of the Giants).
    float shortOf = 1.0f;
    if (haveShare && eyeEnable() && std::isfinite(lum) && lum > 0.0f && std::isfinite(gameLightShare) && gameLightShare > 0.0f) {
      shortOf = std::max(std::min(DxvkDarkSoulsExposure::meterFloor() / lum, 1.0f / gameLightShare), 1.0f);
    }
    const float target = std::min(std::pow(std::max(shortOf / std::max(eyeKnee(), 1.0f), 1.0f), std::clamp(eyeStrength(), 0.0f, 2.0f)), std::max(eyeMaxGain(), 1.0f));
    // As an eye: slowly opening up in the dark, quickly closing in the light.
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const float dt = float(std::clamp(now - m_eyeGainTime, 0.0, 0.5));
    m_eyeGainTime = now;
    const float seconds = std::max(target > m_eyeViewGain ? eyeDarkenSeconds() : eyeBrightenSeconds(), 0.01f);
    m_eyeViewGain += (target - m_eyeViewGain) * (1.0f - std::exp(-dt / seconds));
    if (m_eyeViewGain < 1.002f) {
      m_eyeViewGain = 1.0f;
    }
  }

  void DxvkDarkSoulsLook::dispatchAmbientMeter(Rc<DxvkContext>& ctx, const Resources::Resource& radiance,
                                               const Resources::Resource& albedo, const Resources::Resource& shadingNormal,
                                               const Resources::Resource& linearViewZ, float missLinearViewZ) {
    m_ambientMetered = false;
    if ((!DxvkDarkSoulsExposure::ambientClamp() && !eyeEnable()) || !albedo.isValid() || !shadingNormal.isValid()) {
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
      updateEye(float(std::exp(total / double(cells))), ambientScale, m_meterAmbient[slot]);
    }

    const VkExtent3D extent = scene.image->mipLevelExtent(0);
    PostFxDarkSoulsLookMeterArgs args = {};
    args.gridSize = { grid.width, grid.height };
    args.sceneSize = { extent.width, extent.height };
    args.exposure = std::max(exposure, 1e-4f);
    args.groupsX = grid.groupsX;
    args.useLift = m_eyeLifted ? 1u : 0u;
    ctx->writeToBuffer(m_meterConstants, 0, sizeof(args), &args);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_meterConstants);

    ctx->bindResourceView(POST_FX_DS_LOOK_METER_SCENE, scene.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_METER_LIFT, m_dofRate.view, nullptr);
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
                                         const vec4* taps, uint32_t tapCount, uint32_t gated) {
    const VkExtent3D in = input.image->mipLevelExtent(0);
    const VkExtent3D out = output.image->mipLevelExtent(0);
    PostFxDarkSoulsLookFilterArgs args = {};
    args.outputSize = { out.width, out.height };
    args.invInputSize = { 1.0f / float(in.width), 1.0f / float(in.height) };
    args.invOutputSize = { 1.0f / float(out.width), 1.0f / float(out.height) };
    args.tapCount = std::min(tapCount, uint32_t(DS_LOOK_MAX_TAPS));
    args.gated = gated;
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

    // What the composite's weights can reach, at the largest rate this frame's constants allow: weights are
    // sat(rate * scale + bias), and each level's share only grows with the rate. A level nobody takes is not computed.
    const Vector4 blendScale = dofBlendScale();
    const Vector4 blendBias = dofBlendBias();
    const float maxRate = std::max(std::max(dofFarStrength(), dofNearStrength()), 0.0f);
    const auto weightAtMost = [&](int i) { return saturate(maxRate * blendScale[i] + blendBias[i]); };
    constexpr float kNoShare = 1e-4f;
    const int levelsUsed = std::abs(weightAtMost(3) - weightAtMost(2)) > kNoShare ? 3
                         : std::abs(weightAtMost(2) - weightAtMost(1)) > kNoShare ? 2
                         : std::abs(weightAtMost(1) - weightAtMost(0)) > kNoShare ? 1 : 0;
    if (levelsUsed == 0) {
      return;
    }

    // The game's DoF level is twice its bright pass size (512x360 for 256x180): one of its texels
    // covers this many of the levels' pixels.
    const VkExtent3D levelExtent = m_dofLevel[0].image->mipLevelExtent(0);
    const float texelX = float(levelExtent.width) / float(2 * m_baseWidth);
    const float texelY = float(levelExtent.height) / float(2 * m_baseHeight);
    const bool reduced = levelExtent.width != extent.width;

    // The level source: the frame as it is now (passes may have run since the scene pass) with the
    // far rate in alpha, staged in the last level, which is only written after the first two.
    {
      ctx->bindResourceView(POST_FX_DS_LOOK_DOF_PREPARE_FRAME, inOutColor.view, nullptr);
      ctx->bindResourceView(POST_FX_DS_LOOK_DOF_PREPARE_RATE, m_dofRate.view, nullptr);
      ctx->bindResourceView(POST_FX_DS_LOOK_DOF_PREPARE_OUTPUT, m_dofLevel[2].view, nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookDofPrepareShader::getShader());
      const VkExtent3D workgroups = util::computeBlockCount(levelExtent, VkExtent3D { 16, 8, 1 });
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    // Levels, each from the one before: the increment is the Gaussian that takes the previous spread to the next.
    // At half resolution the 2x2 average and the composite's bilinear upsampling already spread it by a variance of
    // 1/12 + 1/6 of a level pixel squared, which comes off the first step.
    float previous = 0.0f;
    for (int level = 0; level < levelsUsed; ++level) {
      const float sigma = kDofLevelSigma[level];
      const float variance = sigma * sigma - previous * previous;
      const float taken = reduced && level == 0 ? 0.25f : 0.0f;
      const float stepX = std::sqrt(std::max(variance * texelX * texelX - taken, 0.0f));
      const float stepY = std::sqrt(std::max(variance * texelY * texelY - taken, 0.0f));
      dispatchDofGaussian(ctx, 2 * level, level == 0 ? m_dofLevel[2] : m_dofLevel[level - 1], m_dofLevel[level], stepX, stepY);
      previous = sigma;
    }

    // FRPG_Fil_Dof_NearRate + UnfocusNearRate3x3, with the game's footprint (one texel of its half-size level).
    const bool nearUsed = dofNearStrength() > 0.0f;
    if (nearUsed) {
      const VkExtent3D viewZExtent = linearViewZ.image->mipLevelExtent(0);
      PostFxDarkSoulsLookNearArgs args = {};
      args.outputSize = { levelExtent.width, levelExtent.height };
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
      const VkExtent3D workgroups = util::computeBlockCount(levelExtent, VkExtent3D { 16, 8, 1 });
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    // FRPG_Fil_Dof.
    {
      PostFxDarkSoulsLookDofArgs args = {};
      args.imageSize = { extent.width, extent.height };
      args.invImageSize = { 1.0f / float(extent.width), 1.0f / float(extent.height) };
      args.blendScale = toVec4(dofBlendScale());
      args.blendBias = toVec4(dofBlendBias());
      args.useNear = nearUsed ? 1u : 0u;
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

  void DxvkDarkSoulsLook::dispatchBright(Rc<DxvkContext>& ctx, const Rc<DxvkBuffer>& constants,
                                         const Resources::Resource& scene, const Resources::Resource& linearViewZ,
                                         const float jitterOffset[2], float missLinearViewZ,
                                         float thresholdNear, float thresholdFar) {
    const VkExtent3D extent = scene.image->mipLevelExtent(0);
    const VkExtent3D viewZExtent = linearViewZ.image->mipLevelExtent(0);
    PostFxDarkSoulsLookBrightArgs args = {};
    args.outputSize = { m_baseWidth, m_baseHeight };
    args.sceneSize = { extent.width, extent.height };
    args.depthJitterUv = { jitterOffset[0] / float(viewZExtent.width), jitterOffset[1] / float(viewZExtent.height) };
    args.missLinearViewZ = missLinearViewZ;
    args.thresholdNear = thresholdNear;
    args.thresholdFar = thresholdFar;
    args.depthBegin = depthBegin();
    args.depthEnd = depthEnd();
    args.useFogGateMask = m_fogGateMaskUsed ? 1u : 0u;
    ctx->writeToBuffer(constants, 0, sizeof(args), &args);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(constants);

    ctx->bindResourceView(POST_FX_DS_LOOK_BRIGHT_SCENE, scene.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_BRIGHT_FOG_GATE_MASK, m_fogGateMask.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_BRIGHT_LINEAR_VIEW_Z, linearViewZ.view, nullptr);
    ctx->bindResourceView(POST_FX_DS_LOOK_BRIGHT_OUTPUT, m_base[0].view, nullptr);
    ctx->bindResourceBuffer(POST_FX_DS_LOOK_BRIGHT_CONSTANTS, DxvkBufferSlice(constants, 0, constants->info().size));
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookBrightShader::getShader());
    const VkExtent3D workgroups = util::computeBlockCount(VkExtent3D { m_baseWidth, m_baseHeight, 1 }, VkExtent3D { 16, 8, 1 });
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkDarkSoulsLook::ensureShaftCubes(Rc<DxvkContext>& ctx) {
    if (m_shaftCube[0].isValid()) {
      return;
    }
    const VkExtent3D side { kShaftCubeSize, kShaftCubeSize, 1 };
    static const char* kNames[3] = { "DS1 look shaft source cube", "DS1 look shaft blur cube", "DS1 look shaft pass 1 cube" };
    for (int i = 0; i < 3; ++i) {
      m_shaftCube[i] = Resources::createImageResource(ctx, kNames[i], side, VK_FORMAT_R16G16B16A16_SFLOAT, 6,
                                                      VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
      DxvkImageViewCreateInfo viewInfo;
      viewInfo.type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
      viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
      viewInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT;
      viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.minLevel = 0;
      viewInfo.numLevels = 1;
      viewInfo.minLayer = 0;
      viewInfo.numLayers = 6;
      m_shaftCubeTarget[i] = m_device->createImageView(m_shaftCube[i].image, viewInfo);
    }
  }

  bool DxvkDarkSoulsLook::dispatchShafts(Rc<DxvkContext>& ctx, const Resources::Resource& linearViewZ, const RtCamera& camera,
                                         const float jitterOffset[2], float missLinearViewZ) {
    RtxContext* rtxCtx = static_cast<RtxContext*>(ctx.ptr());
    const auto& tlas = rtxCtx->getResourceManager().getTLAS(Tlas::Opaque).accelStructure;
    const Vector3 sun = shaftDirection();
    const float sunLength = std::sqrt(sun.x * sun.x + sun.y * sun.y + sun.z * sun.z);
    if (!rtxCtx->ds1ShaftSourceReady() || tlas.ptr() == nullptr || !(sunLength > 0.0f)) {
      return false;
    }
    ScopedGpuProfileZone(ctx, "Dark Souls 1 Look: Light Shafts");
    ensureShaftCubes(ctx);

    PostFxDarkSoulsLookShaftArgs args = {};
    args.camera = camera.getShaderConstants();
    args.cubeSize = kShaftCubeSize;
    args.outputSize = { m_baseWidth, m_baseHeight };
    args.sunDirection = { sun.x / sunLength, sun.y / sunLength, sun.z / sunLength };
    // The game's buffer spans the screen's width: a texel is 2 / (P00 * width) of tangent at the centre.
    const double p00 = camera.getViewToProjection()[0][0];
    args.texelAngle = float(std::atan(2.0 / (std::abs(p00) * double(std::max(m_baseWidth, 1u)))));
    args.threshold = std::min(shaftThreshold(), 0.999f);
    // The sky raster's decode, as the composite decodes primary misses (CompositePass::dispatch).
    const float drawn = DxvkDarkSoulsExposure::drawnExposure();
    args.skyBrightness = ds1SkyBrightness();
    args.exposure = sceneExposure();
    args.skyUnexpose = drawn > 0.0f ? 1.0f / drawn : 1.0f;
    args.skyGamma = RtxOptions::skyForceHDR() ? 1u : 2u;
    const VkExtent3D viewZExtent = linearViewZ.image->mipLevelExtent(0);
    args.depthJitterUv = { jitterOffset[0] / float(viewZExtent.width), jitterOffset[1] / float(viewZExtent.height) };
    args.missLinearViewZ = missLinearViewZ;

    const Resources::Resource source = rtxCtx->getResourceManager().getDs1ShaftSource(ctx);
    ctx->bindAccelerationStructure(POST_FX_DS_LOOK_SHAFTS_TLAS, tlas);
    ctx->bindResourceView(POST_FX_DS_LOOK_SHAFTS_SOURCE, source.view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_SHAFTS_SOURCE, m_linearSampler);
    ctx->bindResourceView(POST_FX_DS_LOOK_SHAFTS_LINEAR_VIEW_Z, linearViewZ.view, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookShaftsShader::getShader());

    // Source -> cube 0, blur -> cube 1, pass 1 -> cube 2, pass 2 -> m_base[1]. Unused slots still need a view.
    const Vector3 passes[4] = { Vector3(0.0f), Vector3(0.0f), shaftPass1(), shaftPass2() };
    for (uint32_t mode = 0; mode < 4; ++mode) {
      args.mode = mode;
      args.maxLength = passes[mode].x;
      args.weight = passes[mode].y;
      args.decay = passes[mode].z;
      ctx->writeToBuffer(m_shaftConstants[mode], 0, sizeof(args), &args);
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(m_shaftConstants[mode]);
      ctx->bindResourceBuffer(POST_FX_DS_LOOK_SHAFTS_CONSTANTS,
                              DxvkBufferSlice(m_shaftConstants[mode], 0, m_shaftConstants[mode]->info().size));
      ctx->bindResourceView(POST_FX_DS_LOOK_SHAFTS_INPUT, mode == 0 ? source.view : m_shaftCube[mode - 1].view, nullptr);
      ctx->bindResourceSampler(POST_FX_DS_LOOK_SHAFTS_INPUT, m_linearSampler);
      ctx->bindResourceView(POST_FX_DS_LOOK_SHAFTS_OUTPUT_CUBE, m_shaftCubeTarget[std::min(mode, 2u)], nullptr);
      ctx->bindResourceView(POST_FX_DS_LOOK_SHAFTS_OUTPUT, m_base[1].view, nullptr);
      const VkExtent3D workgroups = mode == 3
        ? util::computeBlockCount(VkExtent3D { m_baseWidth, m_baseHeight, 1 }, VkExtent3D { 16, 8, 1 })
        : util::computeBlockCount(VkExtent3D { kShaftCubeSize, kShaftCubeSize, 6 }, VkExtent3D { 16, 8, 1 });
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
    return true;
  }

  void DxvkDarkSoulsLook::dispatchComposite(
    Rc<DxvkContext> ctx,
    const Resources::Resource& inOutColor,
    const Resources::Resource& linearViewZ,
    const RtCamera& camera,
    const float jitterOffset[2],
    float missLinearViewZ) {
    ScopedGpuProfileZone(ctx, "Dark Souls 1 Look: Bloom and ColAdj");
    ensureResources(ctx, std::max(bloomWidth(), 2), std::max(bloomHeight(), 2));

    const VkExtent3D extent = inOutColor.image->mipLevelExtent(0);
    const bool bloom = bloomEnable() && gameActive();

    if (bloom) {
      dispatchBright(ctx, m_brightConstants, inOutColor, linearViewZ, jitterOffset, missLinearViewZ, thresholdNear(), thresholdFar());

      vec4 vertical[15], horizontal[15];
      bloomTaps(vertical, true);
      bloomTaps(horizontal, false);
      // The game's order: GaussBlur5x5, downsample, GaussBlur5x5, Bloom vertical, Bloom horizontal.
      dispatchFilter(ctx, m_filterConstants[0], m_base[0], m_base[1], kGauss5x5, 13);
      dispatchFilter(ctx, m_filterConstants[1], m_base[1], m_half[0], kDownsample2x, 1);
      dispatchFilter(ctx, m_filterConstants[2], m_half[0], m_half[1], kGauss5x5, 13);
      dispatchFilter(ctx, m_filterConstants[3], m_half[1], m_half[0], vertical, 15, 2);
      dispatchFilter(ctx, m_filterConstants[4], m_half[0], m_half[1], horizontal, 15, 2);
    }

    // The game's light shaft chain, the result in m_base[1] (the bloom is done with the bright pass buffers).
    const bool shafts = shaftEnable() && shaftActive() && gameActive() &&
                        dispatchShafts(ctx, linearViewZ, camera, jitterOffset, missLinearViewZ);

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
      // The grade's black offset takes away the floor the game's ambient gives its image. A view the eye has to lift has
      // no such floor (the path tracer's light does not reach there), and a large offset eats what it has: Lost Izalith
      // lost its walls to -0.21. So past the limit the offset eases with the lift, all channels by the same factor so the
      // grade keeps its hue. A view with no lift, or a grade within the limit (the Depths' -0.075), is left as the game's.
      const float limit = std::max(gradeOffsetLimit(), 0.0f);
      const float largest = std::max({ -args.gradeRowR.w, -args.gradeRowG.w, -args.gradeRowB.w });
      if (eyeEnable() && largest > limit) {
        const float eased = std::clamp((m_eyeViewGain - 1.0f) / std::max(gradeOffsetLiftFull() - 1.0f, 0.01f), 0.0f, 1.0f);
        const float scale = 1.0f + (limit / largest - 1.0f) * eased;
        for (vec4* row : { &args.gradeRowR, &args.gradeRowG, &args.gradeRowB }) {
          if (row->w < 0.0f) {
            row->w *= scale;
          }
        }
      }
    } else {
      // No ColAdj seen yet: the scene's 1.3 headroom undone, no grade.
      args.sceneScale = 1.3f;
      args.gradeRowR = vec4 { 1, 0, 0, 0 };
      args.gradeRowG = vec4 { 0, 1, 0, 0 };
      args.gradeRowB = vec4 { 0, 0, 1, 0 };
    }
    args.shaftEnable = shafts ? 1u : 0u;
    args.shaftWeight = shaftWeight() * std::max(shaftStrength(), 0.0f);
    args.blackToe = std::clamp(blackToe(), 0.0f, 1.0f);
    // Image adjustments (rtx.ds1Look.adjust* and the strengths): exactly the look as it is at their defaults.
    args.bloomRateNear *= std::max(bloomStrength(), 0.0f);
    args.bloomRateFar *= std::max(bloomStrength(), 0.0f);
    if (blackOffsetScale() != 1.0f) {
      for (vec4* row : { &args.gradeRowR, &args.gradeRowG, &args.gradeRowB }) {
        if (row->w < 0.0f) {
          row->w *= std::max(blackOffsetScale(), 0.0f);
        }
      }
    }
    args.adjustExposure = exp2f(std::clamp(adjustExposure(), -4.0f, 4.0f) / 2.2f);
    args.adjustGain = std::max(adjustGain(), 0.0f);
    args.adjustGamma = std::max(adjustGamma(), 0.01f);
    args.adjustLift = std::clamp(adjustLift(), 0.0f, 0.5f);
    args.adjustContrast = std::max(adjustContrast(), 0.0f);
    args.adjustSaturation = std::max(adjustSaturation(), 0.0f);
    args.adjustEnable = (adjustGain() != 1.0f || adjustGamma() != 1.0f || adjustLift() != 0.0f || adjustContrast() != 1.0f ||
                         adjustSaturation() != 1.0f) ? 1u : 0u;
    // HDR10 output: ColAdj's clips become a roll-off from the knee towards the headroom (rtx_hdr_output.h).
    args.keepHeadroom = DxvkHdrOutput::renderHeadroom() && DxvkHdrOutput::ds1Headroom() ? 1u : 0u;
    args.hdrLimitGamma = std::pow(DxvkHdrOutput::headroom(), 1.0f / 2.2f);
    args.hdrPerChannel = std::clamp(DxvkHdrOutput::rollOffPerChannel(), 0.0f, 1.0f);
    args.hdrKneeGamma = std::min(DxvkHdrOutput::kneeGamma(), args.hdrLimitGamma - 0.05f);
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
    ctx->bindResourceView(POST_FX_DS_LOOK_COMPOSITE_SHAFTS, m_base[1].view, nullptr);
    ctx->bindResourceSampler(POST_FX_DS_LOOK_COMPOSITE_SHAFTS, m_linearSampler);
    // Without a blend this frame the slot still needs a view; the shader does not read it.
    ctx->bindResourceView(POST_FX_DS_LOOK_COMPOSITE_STOCK, args.intensity < 1.0f ? m_blend[1].view : inOutColor.view, nullptr);
    ctx->bindResourceBuffer(POST_FX_DS_LOOK_COMPOSITE_CONSTANTS, DxvkBufferSlice(m_compositeConstants, 0, m_compositeConstants->info().size));
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, PostFxDarkSoulsLookCompositeShader::getShader());
    const VkExtent3D workgroups = util::computeBlockCount(extent, VkExtent3D { 16, 8, 1 });
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void DxvkDarkSoulsLook::showImguiSettings() {
    if (!ImGui::CollapsingHeader("Dark Souls image pipeline")) {
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
    RemixGui::Checkbox("Full Resolution Depth of Field", &dofFullResObject());
    RemixGui::Checkbox("Game Light Shafts", &shaftEnableObject());
    RemixGui::Checkbox("Eye Adaptation (dark parts of the view come up)", &eyeEnableObject());
    ImGui::BeginDisabled(!eyeEnable());
    ImGui::Text("Eye: view's lift %.2f", m_eyeViewGain);
    RemixGui::DragFloat("Eye: Largest Lift", &eyeMaxGainObject(), 0.05f, 1.0f, 8.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Eye: Knee (no lift until this far short)", &eyeKneeObject(), 0.01f, 1.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Eye: Strength", &eyeStrengthObject(), 0.01f, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Eye: Dark Parts Take More", &eyeLocalObject(), 0.01f, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Eye: Seconds to a Brighter View", &eyeBrightenSecondsObject(), 0.01f, 0.01f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Eye: Seconds to a Darker View", &eyeDarkenSecondsObject(), 0.05f, 0.01f, 20.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    RemixGui::DragFloat("Fog / DoF Depth Stabilization (texels)", &depthStabilizationTexelsObject(), 0.05f, 0.0f, 4.0f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Fog / DoF Temporal Blend (1 = no history)", &depthTemporalBlendObject(), 0.01f, 0.01f, 1.0f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
    // Image adjustments for players, on top of the game's look (which replaces Remix's tonemapper while it runs).
    if (ImGui::TreeNodeEx("Image Adjustments", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::TextDisabled("On top of the game's look. The defaults change nothing.");
      constexpr ImGuiSliderFlags clamp = ImGuiSliderFlags_AlwaysClamp;
      RemixGui::SliderFloat("Exposure (stops)", &adjustExposureObject(), -2.0f, 2.0f, "%.2f", clamp);
      RemixGui::SliderFloat("Brightness", &adjustGainObject(), 0.5f, 1.5f, "%.2f", clamp);
      RemixGui::SliderFloat("Midtones (gamma)", &adjustGammaObject(), 0.5f, 2.0f, "%.2f", clamp);
      RemixGui::SliderFloat("Shadow Lift", &adjustLiftObject(), 0.0f, 0.2f, "%.3f", clamp);
      RemixGui::SliderFloat("Contrast", &adjustContrastObject(), 0.5f, 1.5f, "%.2f", clamp);
      RemixGui::SliderFloat("Saturation", &adjustSaturationObject(), 0.0f, 2.0f, "%.2f", clamp);
      RemixGui::SliderFloat("Black Crush Softening (toe)", &blackToeObject(), 0.0f, 1.0f, "%.2f", clamp);
      RemixGui::SliderFloat("Game Grade's Black Offset", &blackOffsetScaleObject(), 0.0f, 1.5f, "%.2f", clamp);
      RemixGui::SliderFloat("Bloom Strength", &bloomStrengthObject(), 0.0f, 3.0f, "%.2f", clamp);
      RemixGui::SliderFloat("Light Shaft Strength", &shaftStrengthObject(), 0.0f, 3.0f, "%.2f", clamp);
      if (ImGui::Button("Reset Adjustments")) {
        adjustExposure.resetToDefault();
        adjustGain.resetToDefault();
        adjustGamma.resetToDefault();
        adjustLift.resetToDefault();
        adjustContrast.resetToDefault();
        adjustSaturation.resetToDefault();
        blackToe.resetToDefault();
        blackOffsetScale.resetToDefault();
        bloomStrength.resetToDefault();
        shaftStrength.resetToDefault();
      }
      ImGui::TreePop();
    }
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
      if (shaftActive()) {
        const Vector3 d = shaftDirection();
        const Vector3 p1 = shaftPass1(), p2 = shaftPass2();
        ImGui::Text("shaft sun %.3f, %.3f, %.3f", d.x, d.y, d.z);
        ImGui::Text("threshold %.3f  weight %.2f  passes (%.0f, %.3f, %.3f) (%.0f, %.3f, %.3f)", shaftThreshold(), shaftWeight(),
                    p1.x, p1.y, p1.z, p2.x, p2.y, p2.z);
      } else {
        ImGui::TextDisabled("The game runs no light shaft filter here.");
      }
      ImGui::TreePop();
    }
    ImGui::Unindent();
  }

} // namespace dxvk
