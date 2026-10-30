// The Dark Souls menu: every DS1-specific runtime option in
// one place, drawn as the developer menu's Dark Souls tab (Alt+X >
// Developer Settings Menu > Dark Souls), Nier-style: one collapsing header per
// feature, all open by default. The widgets are the standard RemixGui ones bound
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
#include "rtx_dark_souls_specular.h"
#include "../dxvk_context.h"
#include "../imgui/imgui.h"

namespace dxvk {

  namespace {
    constexpr ImGuiTreeNodeFlags kOpen = ImGuiTreeNodeFlags_DefaultOpen;
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

    showWater();
    showMaterials();
    showMarkers();

    ImGui::PopItemWidth();
  }

} // namespace dxvk
