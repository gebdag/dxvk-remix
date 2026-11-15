// The Dark Souls menu: every DS1-specific runtime option in
// one place, drawn as the developer menu's Dark Souls tab (Alt+X >
// Developer Settings Menu > Dark Souls), Nier-style: one collapsing header per
// feature, closed by default but for the image pipeline. Authoring and debug tools show only in dev mode (DxvkDs1Dev).
// The widgets are the standard RemixGui ones bound
// to the options themselves, so tooltips come from the option descriptions and
// values save to rtx.conf like any other setting.
//
// Settings that live in the proxy (game lights, sun, player light, sprite
// switches) stay in the proxy's F4 menu.

#include "rtx_ds1_menu.h"
#include <cstdio>
#include "rtx_option_layer.h"

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
#include "rtx_dark_souls_motion_blur.h"
#include "rtx_dark_souls_skin.h"
#include "rtx_dark_souls_effect_particles.h"
#include "rtx_ds1_emissive.h"
#include "rtx_hdr_output.h"
#include "../dxvk_context.h"
#include "../../d3d9/d3d9_rtx.h"
#include "../imgui/imgui.h"
#include "../../util/util_env.h"

namespace dxvk {

  void DxvkDs1SettingsCheck::update() {
    static int seen = 0;
    const int value = logUnsavedSettings();
    if (value == seen) {
      return;
    }
    seen = value;
    const std::pair<const char*, RtxOptionLayer*> layers[] = {
      { "user", const_cast<RtxOptionLayer*>(RtxOptionLayer::getUserLayer()) },
      { "rtx", RtxOptionLayer::getRtxConfLayer() },
    };
    for (const auto& [name, layer] : layers) {
      if (!layer) {
        continue;
      }
      const std::string path = std::string("rtx_comp/unsaved_") + name + ".conf";
      std::remove(path.c_str());
      if (layer->hasUnsavedChanges()) {
        layer->exportUnsavedChanges(path);
        Logger::info(str::format("[DS1 settings] ", layer->getName(), ": unsaved changes written to ", path));
      } else {
        Logger::info(str::format("[DS1 settings] ", layer->getName(), ": no unsaved changes"));
      }
    }
  }

  namespace {
    // Sections start closed: the menu is a place to look something up, not a page of everything.
    constexpr ImGuiTreeNodeFlags kOpen = ImGuiTreeNodeFlags_None;
    constexpr ImGuiSliderFlags kClamp = ImGuiSliderFlags_AlwaysClamp;

    // A line that opens `url` in the browser when clicked.
    void link(const char* url) {
      ImGui::TextColored(ImVec4(0.45f, 0.7f, 1.0f, 1.0f), "%s", url);
      if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("Open in the browser");
      }
      if (ImGui::IsItemClicked()) {
        env::openUrlInBrowser(url);
      }
    }

    void showAbout() {
      if (!ImGui::CollapsingHeader("About", kOpen)) {
        return;
      }
      ImGui::Indent();
      ImGui::TextUnformatted("Dark Souls RTX created by gabdeg");
      link("https://github.com/gebdag/");
      ImGui::Spacing();
      ImGui::TextUnformatted("Credits to:");
      ImGui::Spacing();
      ImGui::TextUnformatted("NVIDIA for RTX Remix");
      ImGui::TextUnformatted("NV_E-man for Vibe Reverse Engineering Toolset");
      ImGui::TextUnformatted("#xoxor4d for remix-comp-proxy & general inspiration");
      ImGui::TextUnformatted("NightRaven for PBRFusion 4");
      ImGui::TextUnformatted("Entity for play testing");
      ImGui::TextUnformatted("and the RTX Remix Discord community!");
      ImGui::Unindent();
    }

    void showParticles() {
      if (!DxvkDs1MenuView::advanced() || !ImGui::CollapsingHeader("Particles and effects", kOpen)) {
        return;
      }
      ImGui::Indent();
      // The same marker as in Proxy draw markers: a dev switch.
      if (DxvkDs1Dev::enable()) {
        RemixGui::Checkbox("Unlit Sprites (proxy marker)", &RtxOptions::fixedFunctionUnlitMarkerEnabledObject());
      }
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
      RemixGui::Checkbox("Water Displacement", &DxvkDarkSoulsRipples::displaceViewObject());
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Off: the water no longer shifts what is seen on and through it (ripple parallax, refraction smear,\n"
                          "bending by depth) and skips that work. Waves and ripples still shape its reflections.");
      }
      RemixGui::Checkbox("Ripples Around the Player", &DxvkDarkSoulsRipples::enableObject());
      if (!DxvkDs1MenuView::advanced()) {
        ImGui::Unindent();
        return;
      }
      ImGui::Separator();
      RemixGui::Checkbox("Animated Water", &TranslucentMaterialOptions::animatedWaterEnableObject());
      RemixGui::SliderFloat("Primary Normal LoD Bias", &TranslucentMaterialOptions::animatedWaterPrimaryNormalLodBiasObject(), 0.0f, 16.0f, "%.2f", kClamp);
      RemixGui::SliderFloat("Secondary Normal LoD Bias", &TranslucentMaterialOptions::animatedWaterSecondaryNormalLodBiasObject(), 0.0f, 16.0f, "%.2f", kClamp);
      RemixGui::SliderFloat2("Primary Normal Motion", &TranslucentMaterialOptions::animatedWaterPrimaryNormalMotionObject(), -0.5f, 0.5f, "%.3f", kClamp);
      RemixGui::SliderFloat2("Secondary Normal Motion", &TranslucentMaterialOptions::animatedWaterSecondaryNormalMotionObject(), -0.5f, 0.5f, "%.3f", kClamp);
      ImGui::Separator();
      RemixGui::SliderFloat("Ripple Gravity Scale", &DxvkDarkSoulsRipples::gravityScaleObject(), 0.1f, 3.0f, "%.2f", kClamp);
      RemixGui::SliderFloat("Ripple Damping (1/s)", &DxvkDarkSoulsRipples::dampingObject(), 0.0f, 5.0f, "%.2f", kClamp);
      RemixGui::DragFloat("Ripple Viscosity (m2/s)", &DxvkDarkSoulsRipples::viscosityObject(), 1.0e-5f, 0.0f, 2.0e-3f, "%.5f", kClamp);
      RemixGui::SliderFloat("Ripple Displacement (m)", &DxvkDarkSoulsRipples::displacementObject(), 0.0f, 0.05f, "%.4f", kClamp);
      RemixGui::Checkbox("Ripples From Enemies", &DxvkDarkSoulsRipples::enemiesObject());
      RemixGui::SliderFloat("Enemy Body Reach (m)", &DxvkDarkSoulsRipples::enemyReachObject(), 0.3f, 5.0f, "%.2f", kClamp);
      RemixGui::Checkbox("Ripples From Waterfalls", &DxvkDarkSoulsRipples::waterfallsObject());
      RemixGui::DragFloat("Waterfall Strength (m)", &DxvkDarkSoulsRipples::fallStrengthObject(), 1.0e-5f, 0.0f, 0.01f, "%.5f", kClamp);
      RemixGui::SliderFloat("Ripple Collision Area (m), Enemies Off", &DxvkDarkSoulsRipples::collisionSizeObject(), 0.5f, 7.0f, "%.1f", kClamp);
      RemixGui::SliderFloat("Ripple Normal Scale", &DxvkDarkSoulsRipples::normalScaleObject(), 0.0f, 5.0f, "%.2f", kClamp);
      RemixGui::SliderFloat("Ripple Parallax Scale", &DxvkDarkSoulsRipples::parallaxScaleObject(), 0.0f, 4.0f, "%.2f", kClamp);
      RemixGui::SliderFloat("Ripple Parallax Reach (m)", &DxvkDarkSoulsRipples::parallaxBoundObject(), 0.01f, 0.3f, "%.3f", kClamp);
      RemixGui::DragInt("Ripple Parallax Steps (max)", &DxvkDarkSoulsRipples::parallaxMaxStepsObject(), 1.0f, 8, 128, "%d", kClamp);
      RemixGui::SliderFloat("Ripple Cell Size (m)", &DxvkDarkSoulsRipples::cellSizeObject(), 0.01f, 0.05f, "%.3f", kClamp);
      RemixGui::DragInt("Ripple Window (cells)", &DxvkDarkSoulsRipples::gridSizeObject(), 16.0f, 256, 4096, "%d", kClamp);
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
      if (!DxvkDs1MenuView::advanced()) {
        ImGui::Unindent();
        return;
      }
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
        if (DxvkDs1Dev::enable()) {
          RemixGui::Checkbox("Soak All Over (test)", &DxvkDarkSoulsWet::soakObject());
        }
        ImGui::TreePop();
      }
      ImGui::Unindent();
    }

    void showMaterials() {
      if (!DxvkDs1MenuView::advanced() || !ImGui::CollapsingHeader("Materials", kOpen)) {
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
      // Players get only the HUD switch; the markers themselves break rendering when off.
      if (!DxvkDs1Dev::enable()) {
        if (ImGui::CollapsingHeader("Game UI", kOpen)) {
          ImGui::Indent();
          RemixGui::Checkbox("Show Game UI (HUD and menus)", &RtxOptions::showGameUiObject());
          ImGui::Unindent();
        }
        return;
      }
      if (!ImGui::CollapsingHeader("Proxy draw markers", kOpen)) {
        return;
      }
      ImGui::Indent();
      ImGui::TextDisabled("Draws the DS1 proxy tags; off = Remix treats them as ordinary draws.");
      RemixGui::Checkbox("Decals (blended layers)", &RtxOptions::fixedFunctionDecalMarkerEnabledObject());
      RemixGui::Checkbox("Unlit (sprites)", &RtxOptions::fixedFunctionUnlitMarkerEnabledObject());
      RemixGui::Checkbox("Sky", &RtxOptions::skyMarkerEnabledObject());
      RemixGui::Checkbox("UI (classify only)", &RtxOptions::uiMarkerEnabledObject());
      RemixGui::Checkbox("Show Game UI (HUD and menus)", &RtxOptions::showGameUiObject());
      ImGui::Unindent();

      if (!ImGui::CollapsingHeader("Shader draws", kOpen)) {
        return;
      }
      ImGui::Indent();
      ImGui::TextDisabled("Draws that still run the game's vertex shader reach the path tracer through vertex capture;");
      ImGui::TextDisabled("the proxy's converted world draws are fixed function.");
      RemixGui::Checkbox("Only Shader Draws (drop the fixed-function ones)", &DxvkDs1DrawFilter::onlyShaderDrawsObject());
      RemixGui::Checkbox("Capture Vertices from Shader (off = drop the shader draws, the UI too)", &D3D9Rtx::useVertexCaptureObject());
      ImGui::Text("Path traced last frame: %u shader draws (%u prims), %u fixed-function draws (%u prims)",
                  DxvkDs1DrawFilter::shaderDrawsLastFrame.load(), DxvkDs1DrawFilter::shaderPrimsLastFrame.load(),
                  DxvkDs1DrawFilter::fixedDrawsLastFrame.load(), DxvkDs1DrawFilter::fixedPrimsLastFrame.load());
      if (ImGui::Button("Log the next frame's shader draws")) {
        DxvkDs1DrawFilter::logShaderDrawsObject().setDeferred(DxvkDs1DrawFilter::logShaderDraws() + 1);
      }
      ImGui::SameLine();
      ImGui::TextDisabled("[DS1 shader draw] lines in remix-dxvk.log: shader, size, target, texture, states");
      ImGui::Unindent();
    }
  }

  void showDs1Menu(const Rc<DxvkContext>& ctx) {
    ImGui::PushItemWidth(240.0f);

    // A player's switches, or everything (DxvkDs1MenuView).
    RemixGui::Checkbox("Advanced", &DxvkDs1MenuView::advancedObject());
    ImGui::Separator();

    // The DS1 post passes draw their own headers.
    DxvkPostFx& postFx = ctx->getCommonObjects()->metaPostFx();
    postFx.darkSoulsLook().showImguiSettings();
    if (DxvkDs1MenuView::advanced()) {
      DxvkDarkSoulsExposure::showImguiSettings();
      DxvkDarkSoulsAmbient::showImguiSettings();
      postFx.darkSoulsHaze().showImguiSettings();
    }

    showParticles();
    // How strongly single textures glow (rtx_ds1_emissive.h): an advanced tool that writes emissive_scales.txt.
    if (DxvkDs1MenuView::advanced()) {
      DxvkDs1Emissive::showImguiSettings(ctx);
    }
    DxvkDarkSoulsSkin::showImguiSettings();
    DxvkDarkSoulsEffectParticles::showImguiSettings();
    if (DxvkDs1MenuView::advanced()) {
      DxvkDarkSoulsSwamp::showImguiSettings();
    }

    if (DxvkDs1MenuView::advanced() && ImGui::CollapsingHeader("Tonemapper", kOpen)) {
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
    DxvkDarkSoulsMotionBlur::showImguiSettings();

    showWater();
    showBody();
    showMaterials();
    showMarkers();
    // Every light Remix renders and where it comes from (rtx_light_manager_gui.cpp).
    if (DxvkDs1MenuView::advanced()) {
      ctx->getCommonObjects()->getSceneManager().getLightManager().showDs1LightList();
    }
    showAbout();

    ImGui::PopItemWidth();
  }

} // namespace dxvk
