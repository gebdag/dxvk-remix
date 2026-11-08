// The Dark Souls menu: every DS1-specific runtime option in
// one place, drawn as the developer menu's Dark Souls tab (Alt+X >
// Developer Settings Menu > Dark Souls), Nier-style: one collapsing header per
// feature, all closed by default. The widgets are the standard RemixGui ones bound
// to the options themselves, so tooltips come from the option descriptions and
// values save to rtx.conf like any other setting.
//
// Settings that live in the proxy (game lights, sun, player light, sprite
// switches) stay in the proxy's F4 menu.

#include "rtx_ds1_menu.h"

#include "rtx_imgui.h"
#include "rtx_options.h"
#include "rtx_postFx.h"
#include "rtx_tone_mapping.h"
#include "rtx_local_tone_mapping.h"
#include "rtx_dark_souls_look.h"
#include "rtx_dark_souls_exposure.h"
#include "rtx_dark_souls_ambient.h"
#include "rtx_dark_souls_swamp.h"
#include "rtx_dark_souls_ripples.h"
#include "rtx_dark_souls_specular.h"
#include "rtx_dark_souls_effect_particles.h"
#include "rtx_hdr_output.h"
#include "../dxvk_context.h"
#include "../imgui/imgui.h"

namespace dxvk {

  namespace {
    // Sections start closed: the menu is a place to look something up, not a page of everything.
    constexpr ImGuiTreeNodeFlags kOpen = ImGuiTreeNodeFlags_None;
    constexpr ImGuiSliderFlags kClamp = ImGuiSliderFlags_AlwaysClamp;

    void showParticles() {
      if (!ImGui::CollapsingHeader("Particles and effects", kOpen)) {
        return;
      }
      ImGui::Indent();
      RemixGui::Checkbox("Unlit Sprites (proxy marker)", &RtxOptions::fixedFunctionUnlitMarkerEnabledObject());
      RemixGui::DragFloat("Additive Sprite Intensity", &RtxOptions::emissiveBlendOverrideEmissiveIntensityObject(), 0.01f, 0.0f, 20.0f, "%.2f", kClamp);
      RemixGui::DragFloat("Blended Sprite Intensity", &RtxOptions::unlitBlendedEmissiveIntensityObject(), 0.01f, 0.0f, 20.0f, "%.2f", kClamp);
      RemixGui::SliderFloat("Soft Particle Fade", &RtxOptions::particleSoftnessFactorObject(), 0.0f, 0.5f);
      ImGui::Unindent();
    }

    void showWater() {
      if (!ImGui::CollapsingHeader("Water", kOpen)) {
        return;
      }
      ImGui::Indent();
      RemixGui::Checkbox("Animated Water", &TranslucentMaterialOptions::animatedWaterEnableObject());
      RemixGui::SliderFloat("Primary Normal LoD Bias", &TranslucentMaterialOptions::animatedWaterPrimaryNormalLodBiasObject(), 0.0f, 16.0f, "%.2f", kClamp);
      RemixGui::SliderFloat("Secondary Normal LoD Bias", &TranslucentMaterialOptions::animatedWaterSecondaryNormalLodBiasObject(), 0.0f, 16.0f, "%.2f", kClamp);
      RemixGui::SliderFloat2("Primary Normal Motion", &TranslucentMaterialOptions::animatedWaterPrimaryNormalMotionObject(), -0.5f, 0.5f, "%.3f", kClamp);
      RemixGui::SliderFloat2("Secondary Normal Motion", &TranslucentMaterialOptions::animatedWaterSecondaryNormalMotionObject(), -0.5f, 0.5f, "%.3f", kClamp);
      ImGui::Separator();
      RemixGui::Checkbox("Ripples Around the Player", &DxvkDarkSoulsRipples::enableObject());
      RemixGui::SliderFloat("Ripple Gravity Scale", &DxvkDarkSoulsRipples::gravityScaleObject(), 0.1f, 3.0f, "%.2f", kClamp);
      RemixGui::SliderFloat("Ripple Damping (1/s)", &DxvkDarkSoulsRipples::dampingObject(), 0.0f, 5.0f, "%.2f", kClamp);
      RemixGui::DragFloat("Ripple Viscosity (m2/s)", &DxvkDarkSoulsRipples::viscosityObject(), 1.0e-5f, 0.0f, 2.0e-3f, "%.5f", kClamp);
      RemixGui::SliderFloat("Ripple Displacement (m)", &DxvkDarkSoulsRipples::displacementObject(), 0.0f, 0.05f, "%.4f", kClamp);
      RemixGui::SliderFloat("Ripple Collision Area (m)", &DxvkDarkSoulsRipples::collisionSizeObject(), 0.5f, 7.0f, "%.1f", kClamp);
      RemixGui::SliderFloat("Ripple Normal Scale", &DxvkDarkSoulsRipples::normalScaleObject(), 0.0f, 5.0f, "%.2f", kClamp);
      RemixGui::SliderFloat("Ripple Cell Size (m)", &DxvkDarkSoulsRipples::cellSizeObject(), 0.01f, 0.05f, "%.3f", kClamp);
      ImGui::Unindent();
    }

    // What water, mud and lava leave on the player's body. All of it clears on resting at a bonfire and on a warp.
    void showBody() {
      if (!ImGui::CollapsingHeader("Water, mud and lava on the player", kOpen)) {
        return;
      }
      ImGui::Indent();
      RemixGui::Checkbox("On", &DxvkDarkSoulsWet::enableObject());
      ImGui::TextDisabled("Clears when resting at a bonfire and on a warp.");
      if (ImGui::TreeNode("Water")) {
        RemixGui::SliderFloat("Drying Time (s)", &DxvkDarkSoulsWet::dryTimeObject(), 1.0f, 300.0f, "%.0f", kClamp);
        RemixGui::SliderFloat("Droplet Size (0 = none)", &DxvkDarkSoulsWet::dropletScaleObject(), 0.0f, 3.0f, "%.2f", kClamp);
        RemixGui::Checkbox("Drips", &DxvkDarkSoulsWet::dripsObject());
        RemixGui::SliderFloat("Drip Time (s)", &DxvkDarkSoulsWet::dripTimeObject(), 0.5f, 30.0f, "%.1f", kClamp);
        ImGui::TreePop();
      }
      if (ImGui::TreeNode("Mud")) {
        RemixGui::SliderFloat("Time on the Body (s)", &DxvkDarkSoulsWet::mudDryTimeObject(), 10.0f, 600.0f, "%.0f", kClamp);
        RemixGui::SliderFloat("Hardens Within (s)", &DxvkDarkSoulsWet::mudHardenTimeObject(), 1.0f, 120.0f, "%.0f", kClamp);
        RemixGui::SliderFloat("Fades Over the Last (s)", &DxvkDarkSoulsWet::mudFadeTimeObject(), 1.0f, 120.0f, "%.0f", kClamp);
        ImGui::TreePop();
      }
      if (ImGui::TreeNode("Lava")) {
        RemixGui::SliderFloat("Time on the Body (s)", &DxvkDarkSoulsWet::lavaTimeObject(), 10.0f, 600.0f, "%.0f", kClamp);
        RemixGui::SliderFloat("Glows For (s)", &DxvkDarkSoulsWet::lavaMoltenTimeObject(), 1.0f, 60.0f, "%.0f", kClamp);
        RemixGui::SliderFloat("Fades Over the Last (s)", &DxvkDarkSoulsWet::lavaFadeTimeObject(), 1.0f, 120.0f, "%.0f", kClamp);
        RemixGui::SliderFloat("Glow Strength", &DxvkDarkSoulsWet::lavaGlowObject(), 0.0f, 20.0f, "%.1f", kClamp);
        ImGui::TreePop();
      }
      if (ImGui::TreeNode("More")) {
        RemixGui::SliderFloat("Water Reaches Above Its Surface (m)", &DxvkDarkSoulsWet::contactMarginObject(), 0.0f, 0.3f, "%.2f", kClamp);
        RemixGui::SliderFloat("Lava Reaches Above Its Surface (m)", &DxvkDarkSoulsWet::lavaContactObject(), 0.0f, 0.3f, "%.2f", kClamp);
        RemixGui::SliderFloat("Wet Roughness", &DxvkDarkSoulsWet::roughnessObject(), 0.05f, 1.0f, "%.2f", kClamp);
        RemixGui::SliderFloat("Wet Specular Scale", &DxvkDarkSoulsWet::specularScaleObject(), 1.0f, 3.0f, "%.2f", kClamp);
        RemixGui::SliderFloat("Wet Porous Albedo Scale", &DxvkDarkSoulsWet::porousFactorObject(), 0.1f, 1.0f, "%.2f", kClamp);
        RemixGui::SliderFloat("Drip Rate", &DxvkDarkSoulsWet::dripRateObject(), 0.0f, 1000.0f, "%.0f", kClamp);
        RemixGui::SliderFloat("Drip Size (cm)", &DxvkDarkSoulsWet::dripSizeObject(), 0.1f, 5.0f, "%.2f", kClamp);
        RemixGui::Checkbox("Soak All Over (test)", &DxvkDarkSoulsWet::soakObject());
        ImGui::TreePop();
      }
      ImGui::Unindent();
    }

    void showMaterials() {
      if (!ImGui::CollapsingHeader("Materials", kOpen)) {
        return;
      }
      ImGui::Indent();
      RemixGui::SliderFloat("Legacy Specular Level", &LegacyMaterialDefaults::specularLevelObject(), 0.0f, 1.0f, "%.3f", kClamp);
      RemixGui::SliderFloat("Legacy Fresnel Grazing (f90)", &LegacyMaterialDefaults::fresnelF90Object(), 0.0f, 1.0f, "%.3f", kClamp);
      RemixGui::Checkbox("Texture Factor Blending (per-draw tint and fade)", &RtxOptions::enableMultiStageTextureFactorBlendingObject());
      RemixGui::Checkbox("Reflections at the Game's Environment Level", &DxvkDarkSoulsSpecular::envReflectionWeightObject());
      ImGui::Unindent();
    }

    void showMarkers() {
      if (!ImGui::CollapsingHeader("Proxy draw markers", kOpen)) {
        return;
      }
      ImGui::Indent();
      ImGui::TextDisabled("Draws the DS1 proxy tags; off = Remix treats them as ordinary draws.");
      RemixGui::Checkbox("Decals (blended layers)", &RtxOptions::fixedFunctionDecalMarkerEnabledObject());
      RemixGui::Checkbox("Unlit (sprites)", &RtxOptions::fixedFunctionUnlitMarkerEnabledObject());
      RemixGui::Checkbox("Sky", &RtxOptions::skyMarkerEnabledObject());
      RemixGui::Checkbox("UI", &RtxOptions::uiMarkerEnabledObject());
      ImGui::Unindent();
    }
  }

  void showDs1Menu(const Rc<DxvkContext>& ctx) {
    ImGui::PushItemWidth(240.0f);

    // The DS1 post passes draw their own headers.
    DxvkPostFx& postFx = ctx->getCommonObjects()->metaPostFx();
    postFx.darkSoulsLook().showImguiSettings();
    DxvkDarkSoulsExposure::showImguiSettings();
    DxvkDarkSoulsAmbient::showImguiSettings();
    postFx.darkSoulsHaze().showImguiSettings();

    showParticles();
    DxvkDarkSoulsEffectParticles::showImguiSettings();
    DxvkDarkSoulsSwamp::showImguiSettings();

    if (ImGui::CollapsingHeader("Tonemapper", kOpen)) {
      ImGui::Indent();
      if (DxvkDarkSoulsLook::isActive() && DxvkDarkSoulsLook::intensityClamped() >= 1.0f) {
        ImGui::TextWrapped("Not in use: the game's image pipeline above replaces Remix's tonemapper, auto exposure "
                           "and bloom. Turn off Game Image Pipeline for Remix's own look and these settings.");
      }
      // Remix's own tonemapper, as in Post-Processing > Tonemapping.
      RemixGui::Combo("Tonemapping Mode", &RtxOptions::tonemappingModeObject(), "Global\0Local\0");
      if (RtxOptions::tonemappingMode() == TonemappingMode::Global) {
        ctx->getCommonObjects()->metaToneMapping().showImguiSettings();
      } else {
        ctx->getCommonObjects()->metaLocalToneMapping().showImguiSettings();
      }
      ImGui::Unindent();
    }

    // HDR10 output (rtx_hdr_output.h): the swap chain, back buffer and the frame's headroom.
    DxvkHdrOutput::showImguiSettings();

    showWater();
    showBody();
    showMaterials();
    showMarkers();
    // Every light Remix renders and where it comes from (rtx_light_manager_gui.cpp).
    ctx->getCommonObjects()->getSceneManager().getLightManager().showDs1LightList();

    ImGui::PopItemWidth();
  }

} // namespace dxvk
