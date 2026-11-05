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
  class RtxContext;
  class RtCamera;

  // Dark Souls 1 look: the game's own image pipeline run on Remix's linear radiance, in
  // place of Remix's tonemapper, bloom, haze pass and grading (post_fx_dark_souls_look.h):
  //
  //   scene pass       radiance^(1/2.2) * c135.x / c135.y, the game's fog and scattering,
  //                    saturated: the game's 8-bit scene value (dispatchScene, where the
  //                    tonemapper would run). The DS1 depth of field then runs on it.
  //   bloom            BrightPassFilter, GaussBlur5x5, 2x downsample, GaussBlur5x5 and the
  //                    15-tap Bloom pair, with the game's kernels (dispatchComposite).
  //   light shafts     BrightPassFilter, GaussBlur5x5 and two LightShaft radial blurs towards
  //                    the shaft sun, where the game runs them, on view directions (dispatchComposite).
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

    // rtx.ds1Look.intensity clamped to 0..1.
    static float intensityClamped() { return std::clamp(intensity(), 0.0f, 1.0f); }

    // Scratch frames for intensity below 1 (RtxContext): 0 holds the radiance while Remix's own bloom and
    // tonemapper run on the frame, 1 keeps their result for the composite to blend with.
    const Resources::Resource& blendImage(Rc<DxvkContext> ctx, const Resources::Resource& like, uint32_t index);

    void dispatchScene(
      Rc<DxvkContext> ctx,
      const Resources::Resource& inOutColor,
      const Resources::Resource& linearViewZ,
      const Resources::Resource& surfaceFlags,
      const Resources::Resource& motionVector,
      const Resources::Resource& albedo,
      const Resources::Resource& shadingNormal,
      const Resources::Resource& additiveRadiance,
      const DxvkDarkSoulsHaze& haze,
      const Camera& camera,
      const float jitterOffset[2],
      float missLinearViewZ,
      bool resetHistory);

    // FRPG_Fil_Dof and its chain, after the scene pass (which writes the far rate into alpha).
    void dispatchDof(
      Rc<DxvkContext> ctx,
      const Resources::Resource& inOutColor,
      const Resources::Resource& linearViewZ,
      const float jitterOffset[2],
      float missLinearViewZ);

    // Bloom, light shafts and ColAdj. The camera places the shaft sun on screen.
    void dispatchComposite(
      Rc<DxvkContext> ctx,
      const Resources::Resource& inOutColor,
      const Resources::Resource& linearViewZ,
      const RtCamera& camera,
      const float jitterOffset[2],
      float missLinearViewZ);

    void showImguiSettings();

    RTX_OPTION("rtx.ds1Look", bool, enable, true,
               "Run Dark Souls' own image pipeline on the path-traced radiance (its exposure, fog and scattering, 8-bit "
               "scene clip, bloom, depth of field and ColAdj grade, with the values the DS1 proxy reads from the game each "
               "frame) instead of Remix's auto exposure, tonemapper and bloom. Off = stock Remix image processing (the "
               "Post-Processing settings), with no DS1 treatment.");
    RTX_OPTION("rtx.ds1Look", float, intensity, 1.0f,
               "How much of the game's image pipeline is laid over Remix's own image: 1 = the game's look (its exposure, "
               "fog, bloom, DoF and grade), 0 = Remix's own tonemapper, auto exposure and bloom. In between, the game's "
               "scene is blended over Remix's tonemapped frame in display space and the grade eased toward identity.");
    RTX_OPTION("rtx.ds1Look", bool, hazeEnable, true, "Apply the game's fog and sky scattering in the scene pass.");
    RTX_OPTION("rtx.ds1Look", bool, bloomEnable, true, "Apply the game's bloom in the composite.");
    RTX_OPTION("rtx.ds1Look", bool, dofEnable, true, "Apply the game's depth of field (FRPG_Fil_Dof) after the scene pass.");
    RTX_OPTION("rtx.ds1Look", bool, eyeEnable, true,
               "Adapt as an eye does where the game's exposure stops: below the meter's floor (its luminance clamp) the view comes up, up to what the game's "
               "own light gives on the same surfaces, so dark interiors can be seen without lights being added and places dark in the game stay dark. "
               "What is bright stays. The game's meter does not see the lift.");
    RTX_OPTION("rtx.ds1Look", float, eyeMaxGain, 3.0f, "The most a dark part of the view is lifted, on gamma values. How far an eye adapts in the dark.");
    RTX_OPTION("rtx.ds1Look", float, eyeKnee, 1.25f, "How far below the meter's floor a view may lie with no lift at all: places the game keeps dim stay as they are (measured: views up to about this already match the game).");
    RTX_OPTION("rtx.ds1Look", float, eyeStrength, 0.85f, "How fully the view's lift follows what it is short of past the knee: 1 all the way, less stays darker, as dark rooms look to an eye.");
    RTX_OPTION("rtx.ds1Look", float, eyeLocal, 0.5f, "How much more the view's dark parts come up than its bright ones.");
    RTX_OPTION("rtx.ds1Look", float, gradeOffsetLimit, 0.08f, "While the eye lifts the view, the game's grade takes off no more than this for black, gamma, on its largest channel; the others keep their proportion. Lost Izalith's takes 0.21. 1 keeps the game's in full.");
    RTX_OPTION("rtx.ds1Look", float, gradeOffsetLiftFull, 2.0f, "The eye's lift at which that limit holds in full, from none at no lift.");
    RTX_OPTION("rtx.ds1Look", float, eyeBrightenSeconds, 0.3f, "How fast the eye adapts to a part of the view getting brighter.");
    RTX_OPTION("rtx.ds1Look", float, eyeDarkenSeconds, 2.0f, "How slowly the eye adapts to a part of the view getting darker.");
    RTX_OPTION("rtx.ds1Look", bool, dofFullRes, false,
               "Compute the DoF's blur levels and near rate at full resolution. Off: at half, which costs about a quarter of the pixels with half the "
               "blur taps and looks nearly the same. The blend onto the frame is at full resolution either way.");
    RTX_OPTION("rtx.ds1Look", float, depthStabilizationTexels, 1.0f,
               "The fog, scattering and DoF rates read view Z, which is this frame's jittered render-resolution raster, "
               "while the colour they apply to is the upscaler's resolved frame: at silhouettes, worst against the sky, "
               "the surface a pixel reads flips between frames and those terms crawl. They are averaged over the centre "
               "and a 3x3 of taps this many view Z texels apart, then resolved over time (depthTemporalBlend). "
               "0 = the centre alone, no history (the old behaviour).");
    RTX_OPTION("rtx.ds1Look", float, depthTemporalBlend, 0.1f,
               "Weight of this frame's fog, scattering and far DoF terms against their history, reprojected through the "
               "motion vectors and clamped to the range of this frame's taps: averaged over the camera jitter sequence, "
               "their coverage of a silhouette matches the upscaler's resolved colour. 1 = no history.");
    RTX_OPTION("rtx.ds1Look", float, hazeRegionBlendRadius, 2.0f,
               "Each surface takes the fog and scattering of its region's DrawParams, as in the game, but blended with "
               "the regions of the surfaces within this many world units of it, by their distance in 3D. The game pairs "
               "each region's fog with that region's ambient light, so where two regions meet it shows no seam; Remix "
               "lights both with one continuous light, and their fog alone met in a hard line (Firelink's passage). "
               "A seam becomes a ramp twice this wide; surfaces apart in depth never mix. 0 = each surface's own set.");
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

    // Light shafts (FRPG_Fil_LightShaft): the game's screen-space glow around its shaft sun, from the bright
    // parts of the frame (post_fx_dark_souls_look.h). Its sharp shafts are the m10_bg_shaft planes in the sky.
    RTX_OPTION("rtx.ds1Look", bool, shaftEnable, true,
               "Add the game's light shaft filter: a bright pass of the frame blurred radially towards the game's shaft "
               "sun, in the areas where the game runs it.");
    // Game state, set by the proxy from the LightShaft passes. Never saved.
    RTX_OPTION_FLAG("rtx.ds1Look", bool, shaftActive, false, RtxOptionFlags::NoSave,
                    "Set by the DS1 proxy while the game runs its light shaft filter (not every area has shafts).");
    RTX_OPTION_FLAG("rtx.ds1Look", Vector3, shaftDirection, Vector3(0.0f, 1.0f, 0.0f), RtxOptionFlags::NoSave,
                    "Towards the game's shaft sun (recovered from LightShaft PS c69; not the lighting sun).");
    RTX_OPTION_FLAG("rtx.ds1Look", float, shaftThreshold, 1.0f, RtxOptionFlags::NoSave,
                    "The shaft bright pass threshold (PS c71.x of the game's shaft BrightPassFilter).");
    RTX_OPTION_FLAG("rtx.ds1Look", float, shaftWeight, 1.0f, RtxOptionFlags::NoSave, "ColAdj's light shaft weight (PS c56.y).");
    RTX_OPTION_FLAG("rtx.ds1Look", Vector3, shaftPass1, Vector3(100.0f, 0.04f, 0.434f), RtxOptionFlags::NoSave,
                    "The first LightShaft pass's PS c70: reach in texels, weight, decay per tap.");
    RTX_OPTION_FLAG("rtx.ds1Look", Vector3, shaftPass2, Vector3(10.0f, 1.0f, 0.92f), RtxOptionFlags::NoSave,
                    "The second LightShaft pass's PS c70: reach in texels, weight, decay per tap.");

  private:
    void ensureResources(Rc<DxvkContext>& ctx, uint32_t width, uint32_t height);
    void ensureDofResources(Rc<DxvkContext>& ctx, const VkExtent3D& extent);
    // One separable Gaussian pass pair (horizontal then vertical) of the given spread in pixels,
    // gated by the DoF rate in alpha like the game's Unfocus3x3.
    void dispatchDofGaussian(Rc<DxvkContext>& ctx, int constantsIndex, const Resources::Resource& input,
                             const Resources::Resource& output, float sigmaX, float sigmaY);
    void dispatchFilter(Rc<DxvkContext>& ctx, const Rc<DxvkBuffer>& constants, const Resources::Resource& input,
                        const Resources::Resource& output, const vec4* taps, uint32_t tapCount, bool gated = false);
    // BrightPassFilter: the scene into m_base[0], thresholded by lerp(near, far, depth rate).
    void dispatchBright(Rc<DxvkContext>& ctx, const Rc<DxvkBuffer>& constants, const Resources::Resource& scene,
                        const Resources::Resource& linearViewZ, const float jitterOffset[2], float missLinearViewZ,
                        float thresholdNear, float thresholdFar);
    // The light shaft chain on view directions into m_base[1] (post_fx_dark_souls_look_shafts.comp.slang);
    // false when there is nothing to run it on this frame.
    bool dispatchShafts(Rc<DxvkContext>& ctx, const Resources::Resource& linearViewZ, const RtCamera& camera,
                        const float jitterOffset[2], float missLinearViewZ);
    void ensureShaftCubes(Rc<DxvkContext>& ctx);
    struct MeterGrid {
      uint32_t width, height, groupsX, groupsY;
      uint32_t groups() const { return groupsX * groupsY; }
    };
    static MeterGrid meterGrid();
    void ensureMeterResources(uint32_t groups);
    // The game's luminance meter on the scene pass output; adapts DxvkDarkSoulsExposure with the
    // value read back kMeterSlots - 1 frames later.
    void dispatchMeter(Rc<DxvkContext>& ctx, const Resources::Resource& scene, float exposure);
    // The ambient meter (post_fx_dark_souls_look.h) on the radiance before the scene pass; its sums
    // travel with the next dispatchMeter's.
    void dispatchAmbientMeter(Rc<DxvkContext>& ctx, const Resources::Resource& radiance, const Resources::Resource& albedo,
                              const Resources::Resource& shadingNormal, const Resources::Resource& linearViewZ,
                              float missLinearViewZ);
    Rc<DxvkBuffer> makeConstants(size_t size, const char* name);

    DxvkDevice* m_device;
    Rc<DxvkSampler> m_linearSampler;

    Rc<DxvkBuffer> m_sceneConstants;
    Rc<DxvkBuffer> m_hazeConstants;
    Rc<DxvkBuffer> m_regionConstants;
    // The haze region blend per view Z texel (dispatchScene's region pass).
    Resources::Resource m_hazeRegion;
    // The eye's local adaptation level, this frame's and last frame's.
    Resources::Resource m_eyeLevel[2];
    int m_eyeIndex = 0;
    uint32_t m_eyeFrame = 0xFFFFFFFFu;
    double m_eyeTime = 0.0;
    float m_eyeViewGain = 1.0f;
    bool m_eyeLifted = false;
    double m_eyeGainTime = 0.0;
    void updateEye(float lum, float gameLightShare, bool haveShare);
    Rc<DxvkBuffer> m_eyeConstants;
    Rc<DxvkBuffer> m_brightConstants;
    Rc<DxvkBuffer> m_filterConstants[6];
    Rc<DxvkBuffer> m_compositeConstants;
    Rc<DxvkBuffer> m_nearConstants;
    Rc<DxvkBuffer> m_dofFilterConstants[6];
    Rc<DxvkBuffer> m_dofConstants;
    Rc<DxvkBuffer> m_shaftConstants[4];
    // The light shaft chain's cubes: bright source, blurred, first pass; each with a 2D array view to write.
    Resources::Resource m_shaftCube[3];
    Rc<DxvkImageView> m_shaftCubeTarget[3];

    // intensity below 1: the radiance copy and Remix's own tonemapped frame (blendImage).
    Resources::Resource m_blend[2];

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
    bool m_dofIsFullRes = false;
    // The scene pass's depth-derived terms over time (ping-pong, [A, B] each, see dispatchScene).
    Resources::Resource m_depthHistory[2][2];
    uint32_t m_depthHistoryIndex = 0;
    uint32_t m_depthHistoryFrame = kInvalidFrameIndex;
    uint32_t m_baseWidth = 0;
    uint32_t m_baseHeight = 0;

    // Meter: per-group ln sums, and a ring of host copies read kMeterSlots - 1 frames later
    // (long finished on the GPU), as DxvkAutoExposure reads its exposure back.
    // Per group: the game meter's ln sum, then the ambient meter's three sums.
    static constexpr uint32_t kMeterSlots = 4;
    static constexpr uint32_t kMeterFloatsPerGroup = 4;
    Rc<DxvkBuffer> m_meterConstants;
    Rc<DxvkBuffer> m_ambientMeterConstants;
    Rc<DxvkBuffer> m_meterPartials;
    Rc<DxvkBuffer> m_meterReadback;
    uint32_t m_meterGroups = 0;                // groups per slot
    uint32_t m_meterCells[kMeterSlots] = {};   // grid cells each slot's sums cover, 0 = empty
    bool m_meterAmbient[kMeterSlots] = {};     // the slot holds ambient meter sums
    bool m_ambientMetered = false;             // this frame's ambient meter ran
    uint32_t m_meterFrame = 0;
  };

} // namespace dxvk
