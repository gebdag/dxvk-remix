#pragma once

#include "dxvk_include.h"
#include "dxvk_context.h"
#include "rtx_resources.h"
#include "rtx_options.h"
#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"

namespace dxvk {

  class DxvkDevice;
  class DxvkSampler;
  class DxvkDarkSoulsHaze;

  // Dark Souls 1 look: the game's own image pipeline run on Remix's linear radiance, in
  // place of Remix's tonemapper, bloom, haze pass and grading (post_fx_dark_souls_look.h):
  //
  //   scene pass       radiance^(1/2.2) * c135.x / c135.y, the game's fog and scattering,
  //                    saturated: the game's 8-bit scene value (dispatchScene, where the
  //                    tonemapper would run). The DS1 depth of field then runs on it.
  //   bloom            BrightPassFilter, GaussBlur5x5, 2x downsample, GaussBlur5x5 and the
  //                    15-tap Bloom pair, with the game's kernels (dispatchComposite).
  //   composite        FRPG_Fil_HDR_ColAdj: sat(scene * c56.z) + bloom * rate, the colour
  //                    matrix c62..c64, saturated.
  //
  // Every value that changes with the area comes from the game, sent by the DS1 proxy
  // (SetConfigVariable on the NoSave options below, and rtx.ds1Exposure / rtx.ds1Haze).
  class DxvkDarkSoulsLook {
  public:
    explicit DxvkDarkSoulsLook(DxvkDevice* device);
    ~DxvkDarkSoulsLook() = default;

    // True when the game's pipeline replaces Remix's tonemapper, bloom, haze and grading.
    static bool isActive();

    void dispatchScene(
      Rc<DxvkContext> ctx,
      const Resources::Resource& inOutColor,
      const Resources::Resource& linearViewZ,
      const DxvkDarkSoulsHaze& haze,
      const Camera& camera,
      const float jitterOffset[2],
      float missLinearViewZ);

    // FRPG_Fil_Dof and its chain, after the scene pass (which writes the far rate into alpha).
    void dispatchDof(
      Rc<DxvkContext> ctx,
      const Resources::Resource& inOutColor,
      const Resources::Resource& linearViewZ,
      const float jitterOffset[2],
      float missLinearViewZ);

    void dispatchComposite(
      Rc<DxvkContext> ctx,
      const Resources::Resource& inOutColor,
      const Resources::Resource& linearViewZ,
      const float jitterOffset[2],
      float missLinearViewZ);

    void showImguiSettings();

    RTX_OPTION("rtx.ds1Look", bool, enable, true,
               "Run Dark Souls' own image pipeline on the path-traced radiance (its exposure, fog and scattering, 8-bit "
               "scene clip, bloom and ColAdj grade, with the values the DS1 proxy reads from the game each frame) instead "
               "of Remix's tonemapper, bloom, DS1 haze pass and DS1 grading.");
    RTX_OPTION("rtx.ds1Look", bool, hazeEnable, true, "Apply the game's fog and sky scattering in the scene pass.");
    RTX_OPTION("rtx.ds1Look", bool, bloomEnable, true, "Apply the game's bloom in the composite.");
    RTX_OPTION("rtx.ds1Look", bool, dofEnable, true, "Apply the game's depth of field (FRPG_Fil_Dof) after the scene pass.");
    RTX_OPTION("rtx.ds1Look", bool, gammaAlbedo, true,
               "Albedo = the texture's stored value^2.2 instead of its sRGB decode, so the path tracer's linear product "
               "albedo * light is the game's gamma-space product raised to 2.2 (output is encoded with x^(1/2.2)).");
    RTX_OPTION("rtx.ds1Look", float, bounceAlbedoGamma, 2.2f,
               "With gammaAlbedo: the power the stored value is raised to on the surfaces indirect light bounces off. "
               "2.2 = the same albedo as surfaces seen directly; lower carries more light into enclosed spaces, where "
               "the game's baked ambient has no path-traced counterpart. Surfaces seen directly are unaffected.");

    // Game state, set by the proxy from the ColAdj draw. Never saved.
    RTX_OPTION_FLAG("rtx.ds1Look", bool, gameActive, false, RtxOptionFlags::NoSave,
                    "Set by the DS1 proxy once it has read the game's ColAdj values.");
    RTX_OPTION_FLAG("rtx.ds1Look", float, sceneScale, 1.3f, RtxOptionFlags::NoSave, "ColAdj scene scale (PS c56.z).");
    RTX_OPTION_FLAG("rtx.ds1Look", float, bloomRateNear, 0.0f, RtxOptionFlags::NoSave, "Bloom rate near (PS c56.x).");
    RTX_OPTION_FLAG("rtx.ds1Look", float, bloomRateFar, 0.0f, RtxOptionFlags::NoSave, "Bloom rate far (PS c71.y).");
    RTX_OPTION_FLAG("rtx.ds1Look", float, thresholdNear, 1.0f, RtxOptionFlags::NoSave, "Bright pass threshold near (PS c11.x).");
    RTX_OPTION_FLAG("rtx.ds1Look", float, thresholdFar, 1.0f, RtxOptionFlags::NoSave, "Bright pass threshold far (PS c71.x).");
    RTX_OPTION_FLAG("rtx.ds1Look", float, depthBegin, 0.0f, RtxOptionFlags::NoSave, "Bloom depth rate begins (PS c71.z).");
    RTX_OPTION_FLAG("rtx.ds1Look", float, depthEnd, 1.0f, RtxOptionFlags::NoSave, "Bloom depth rate ends (PS c71.w).");
    RTX_OPTION_FLAG("rtx.ds1Look", Vector4, gradeRowR, Vector4(1.0f, 0.0f, 0.0f, 0.0f), RtxOptionFlags::NoSave, "ColAdj matrix row R (PS c62).");
    RTX_OPTION_FLAG("rtx.ds1Look", Vector4, gradeRowG, Vector4(0.0f, 1.0f, 0.0f, 0.0f), RtxOptionFlags::NoSave, "ColAdj matrix row G (PS c63).");
    RTX_OPTION_FLAG("rtx.ds1Look", Vector4, gradeRowB, Vector4(0.0f, 0.0f, 1.0f, 0.0f), RtxOptionFlags::NoSave, "ColAdj matrix row B (PS c64).");
    RTX_OPTION_FLAG("rtx.ds1Look", float, dofFarBegin, 10.0f, RtxOptionFlags::NoSave, "Far DoF begins (PS c9.x).");
    RTX_OPTION_FLAG("rtx.ds1Look", float, dofFarEnd, 100.0f, RtxOptionFlags::NoSave, "Far DoF full (PS c9.y).");
    RTX_OPTION_FLAG("rtx.ds1Look", float, dofFarStrength, 0.0f, RtxOptionFlags::NoSave, "Far DoF rate (PS c9.z).");
    RTX_OPTION_FLAG("rtx.ds1Look", float, dofNearBegin, 0.0f, RtxOptionFlags::NoSave, "Near DoF begins (PS c10.x).");
    RTX_OPTION_FLAG("rtx.ds1Look", float, dofNearEnd, 1.0f, RtxOptionFlags::NoSave, "Near DoF ends (PS c10.y).");
    RTX_OPTION_FLAG("rtx.ds1Look", float, dofNearStrength, 0.0f, RtxOptionFlags::NoSave, "Near DoF rate (PS c10.z).");
    RTX_OPTION_FLAG("rtx.ds1Look", Vector4, dofBlendScale, Vector4(-6.0f, -3.0f, -2.0f, 0.0f), RtxOptionFlags::NoSave,
                    "FRPG_Fil_Dof blend weights = sat(rate * this + dofBlendBias) (PS c66).");
    RTX_OPTION_FLAG("rtx.ds1Look", Vector4, dofBlendBias, Vector4(1.0f, 1.5f, 2.0f, 1.0f), RtxOptionFlags::NoSave,
                    "FRPG_Fil_Dof blend weight bias (PS c67).");
    RTX_OPTION_FLAG("rtx.ds1Look", int, bloomWidth, 256, RtxOptionFlags::NoSave,
                    "Width of the game's bright pass buffer (twice its final bloom buffer).");
    RTX_OPTION_FLAG("rtx.ds1Look", int, bloomHeight, 180, RtxOptionFlags::NoSave,
                    "Height of the game's bright pass buffer (twice its final bloom buffer).");

  private:
    void ensureResources(Rc<DxvkContext>& ctx, uint32_t width, uint32_t height);
    void ensureDofResources(Rc<DxvkContext>& ctx, const VkExtent3D& extent);
    // One separable Gaussian pass pair (horizontal then vertical) of the given spread in pixels,
    // gated by the DoF rate in alpha like the game's Unfocus3x3.
    void dispatchDofGaussian(Rc<DxvkContext>& ctx, int constantsIndex, const Resources::Resource& input,
                             const Resources::Resource& output, float sigmaX, float sigmaY);
    void dispatchFilter(Rc<DxvkContext>& ctx, const Rc<DxvkBuffer>& constants, const Resources::Resource& input,
                        const Resources::Resource& output, const vec4* taps, uint32_t tapCount, bool gated = false);
    // The game's luminance meter on the scene pass output; adapts DxvkDarkSoulsExposure with the
    // value read back kMeterSlots - 1 frames later.
    void dispatchMeter(Rc<DxvkContext>& ctx, const Resources::Resource& scene, float exposure);
    Rc<DxvkBuffer> makeConstants(size_t size, const char* name);

    DxvkDevice* m_device;
    Rc<DxvkSampler> m_linearSampler;

    Rc<DxvkBuffer> m_sceneConstants;
    Rc<DxvkBuffer> m_hazeConstants;
    Rc<DxvkBuffer> m_brightConstants;
    Rc<DxvkBuffer> m_filterConstants[5];
    Rc<DxvkBuffer> m_compositeConstants;
    Rc<DxvkBuffer> m_nearConstants;
    Rc<DxvkBuffer> m_dofFilterConstants[6];
    Rc<DxvkBuffer> m_dofConstants;

    // Bright pass size (the game's 256x180 for a 512x360 DoF buffer) and half that.
    Resources::Resource m_base[2];
    Resources::Resource m_half[2];
    // DoF: three full-resolution blur levels, a scratch target for the separable passes, the near rate.
    Resources::Resource m_dofLevel[3];
    Resources::Resource m_dofScratch;
    Resources::Resource m_dofNear;
    // The far DoF rate, written by the scene pass (the frame's alpha belongs to whatever runs after it).
    Resources::Resource m_dofRate;
    VkExtent3D m_dofExtent = { 0, 0, 0 };
    uint32_t m_baseWidth = 0;
    uint32_t m_baseHeight = 0;

    // Meter: per-group ln sums, and a ring of host copies read kMeterSlots - 1 frames later
    // (long finished on the GPU), as DxvkAutoExposure reads its exposure back.
    static constexpr uint32_t kMeterSlots = 4;
    Rc<DxvkBuffer> m_meterConstants;
    Rc<DxvkBuffer> m_meterPartials;
    Rc<DxvkBuffer> m_meterReadback;
    uint32_t m_meterGroups = 0;                // partials per slot
    uint32_t m_meterCells[kMeterSlots] = {};   // grid cells each slot's sums cover, 0 = empty
    uint32_t m_meterFrame = 0;
  };

} // namespace dxvk
