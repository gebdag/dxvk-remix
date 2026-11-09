#include "rtx_dark_souls_swamp.h"
#include "rtx_imgui.h"
#include "rtx_context.h"
#include "rtx_scene_manager.h"
#include "../imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace dxvk {

  namespace {
    // Up to `count` numbers separated by commas, semicolons or spaces.
    template<size_t N>
    size_t parseNumbers(const std::string& text, float (&out)[N]) {
      size_t n = 0;
      const char* p = text.c_str();
      while (*p && n < N) {
        while (*p == ',' || *p == ';' || *p == ' ') {
          ++p;
        }
        char* next = nullptr;
        const float v = std::strtof(p, &next);
        if (next == p) {
          break;
        }
        out[n++] = v;
        p = next;
      }
      return n;
    }

    // The snow's footprint textures, from the proxy's hidden draws, and their bindless indices this frame.
    TextureRef s_stampTexture[2];
    uint32_t s_stampIndex[2] = { BINDING_INDEX_INVALID, BINDING_INDEX_INVALID };

    // The rings come as one string per frame; parsed when it changes.
    std::string s_ringText;
    uint32_t s_ringCount = 0;
    vec4 s_rings[ds1SwampMaxRings * 2];

    void parseRings(const std::string& text) {
      s_ringText = text;
      s_ringCount = 0;
      float values[ds1SwampMaxRings * 8];
      const size_t n = parseNumbers(text, values);
      for (size_t i = 0; i + 7 < n && s_ringCount < ds1SwampMaxRings; i += 8) {
        s_rings[s_ringCount * 2] = vec4 { values[i], values[i + 1], values[i + 2], values[i + 6] };
        s_rings[s_ringCount * 2 + 1] = vec4 { values[i + 3], values[i + 4], values[i + 5], values[i + 7] };
        ++s_ringCount;
      }
    }
  }

  void DxvkDarkSoulsSwamp::setStampTexture(uint32_t slot, const TextureRef& texture) {
    if (slot < 2) {
      s_stampTexture[slot] = texture;
    }
  }

  void DxvkDarkSoulsSwamp::prepare(DxvkContext* ctx) {
    SceneManager& scene = ctx->getCommonObjects()->getSceneManager();
    for (uint32_t i = 0; i < 2; ++i) {
      s_stampIndex[i] = BINDING_INDEX_INVALID;
      if (s_stampTexture[i].isValid()) {
        scene.trackTexture(s_stampTexture[i], s_stampIndex[i], true, false);
      }
    }
  }

  void DxvkDarkSoulsSwamp::fillShaderArgs(Ds1SwampArgs& args) {
    args = {};
    if (!enable()) {
      return;
    }
    const Vector3 deep = deepColour(), crust = crustColour(), reflect = reflectColour(), env = envColour();
    const Vector3 layers = layerScale(), weights = layerWeight(), deepRamp = ramps(), reflectRamp = reflectRamps(), uv = uvScales();
    args.deepColour = vec3 { deep.x, deep.y, deep.z };
    args.deepScale = deepRamp.x;
    args.deepOffset = deepRamp.y;
    args.crustColour = vec3 { crust.x, crust.y, crust.z };
    const Vector3 envWeight = envScale() >= 0.0f ? Vector3(envScale()) : env;
    args.reflectColour = vec3 { envWeight.x * reflect.x, envWeight.y * reflect.y, envWeight.z * reflect.z };
    args.reflectScale = reflectRamp.x;
    args.reflectOffset = reflectRamp.y;
    args.slopeClamp = reflectRamp.z;
    args.layerScale = vec3 { layers.x, layers.y, layers.z };
    args.layerWeight = vec3 { weights.x / 3.0f, weights.y / 3.0f, weights.z / 3.0f };
    args.colourUvScale = uv.x;
    args.detailUvScale = uv.y;
    args.detailStrength = uv.z;
    args.spanPerDepth = spanPerDepth();
    args.invFadeDistance = fadeDistance() > 0.0f ? 1.0f / fadeDistance() : 0.0f;
    const float metres = meshMetresPerUv() > 0.0f ? meshMetresPerUv() : metresPerUv();
    args.uvPerMetre = metres > 0.0f ? 1.0f / metres : 0.0f;
    const Vector3 offsetU = layerOffsetU(), offsetV = layerOffsetV();
    args.layerOffsetU = vec3 { offsetU.x, offsetU.y, offsetU.z };
    args.layerOffsetV = vec3 { offsetV.x, offsetV.y, offsetV.z };
    args.heightScale = heightScale();
    args.wade = wade() ? 1u : 0u;
    args.heightInAlpha = heightInAlpha() ? 1u : 0u;
    if (roughness() >= 0.0f) {
      args.roughness = std::min(roughness(), 1.0f);
    } else {
      // The game's highlight is c101 * mask * light at its peak, its environment reflection
      // c87 * c101 * mask: a GGX lobe with F0 = (c87 * c101 * mask)^2.2 peaks at F0 / (4 pi alpha^2),
      // which is the game's (c101 * mask)^2.2 at alpha^2 = c87^2.2 / (4 pi). Perceptual = sqrt(alpha).
      const float c87 = std::max({ env.x, env.y, env.z, 1e-3f });
      const float alpha = std::sqrt(std::pow(c87, 2.2f) / (4.0f * 3.14159265f));
      args.roughness = std::sqrt(alpha);
    }

    if (rings() != s_ringText) {
      parseRings(rings());
    }
    // On a baked height texture the shader skips the wake rings: its alpha holds the height, not the ring texture. Footprints have their own.
    args.ringCount = s_ringCount;
    args.stampTexture0 = s_stampIndex[0];
    args.stampTexture1 = s_stampIndex[1];
    for (uint32_t i = 0; i < s_ringCount * 2; ++i) {
      args.rings[i] = s_rings[i];
    }
    for (uint32_t i = 0; i < s_ringCount; ++i) {
      args.rings[i * 2].w *= ringStrength();
    }
    args.invSceneDiffuseScale = sceneDiffuseScale() > 0.05f ? 1.0f / sceneDiffuseScale() : 1.0f;
    args.enable = 1;
  }

  void DxvkDarkSoulsSwamp::showImguiSettings() {
    if (!ImGui::CollapsingHeader("Swamp (FRPG_Snow)")) {
      return;
    }
    ImGui::Indent();
    RemixGui::Checkbox("Game Swamp Shading", &enableObject());
    RemixGui::DragFloat("Swamp Roughness (-1 = game's highlight)", &roughnessObject(), 0.01f, -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Wake Ring Strength (1 = game)", &ringStrengthObject(), 0.01f, 0.0f, 8.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Text("Rings this frame: %u", s_ringCount);
    ImGui::Unindent();
  }

} // namespace dxvk
