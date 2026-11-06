#include "rtx_dark_souls_haze.h"
#include "rtx_dark_souls_exposure.h"
#include "rtx_options.h"
#include "rtx_imgui.h"
#include "../imgui/imgui.h"

namespace dxvk {

  namespace {
    vec3 toVec3(const Vector3& v) { return vec3 { v.x, v.y, v.z }; }

    // rtx.ds1Haze.sets, parsed when it changes: per slot, the slot number, PS c103..c111 and
    // VS c128.xy, 39 numbers.
    std::string s_setsText;
    Ds1HazeSet s_sets[DS1_HAZE_SET_COUNT] = {};
    // Packed like the shader's scalar layout: no member may carry C++ alignment padding.
    static_assert(sizeof(Ds1HazeSet) == 36 * sizeof(float) && alignof(Ds1HazeSet) == alignof(float));

    void parseSets(const std::string& text) {
      s_setsText = text;
      for (auto& s : s_sets) {
        s = {};
      }
      constexpr size_t kValues = 39;
      float v[kValues];
      size_t n = 0;
      const char* p = text.c_str();
      while (*p) {
        char* next = nullptr;
        const float value = std::strtof(p, &next);
        if (next == p) {
          ++p;
          continue;
        }
        p = next;
        v[n++] = value;
        if (n < kValues) {
          continue;
        }
        n = 0;
        const int slot = int(v[0]);
        if (slot <= 0 || slot >= DS1_HAZE_SET_COUNT) {
          continue;
        }
        const float* c = &v[1]; // c[4 * i + j] = c(103 + i)[j]
        auto rgb = [c](int r) { const float* q = &c[4 * (r - 103)]; return vec3 { q[0], q[1], q[2] }; };
        auto w = [c](int r) { return c[4 * (r - 103) + 3]; };
        Ds1HazeSet& s = s_sets[slot];
        s.fogColor = rgb(103);
        s.fogStrength = w(103);
        s.extinction = rgb(104);
        // The game's shaders scale the extinction exponent by this constant (1 / ln(2)^2).
        s.extinctionScale = w(111) * 2.08136892f;
        s.sunDirection = rgb(111);
        s.fogStart = v[37];
        s.phase = rgb(107);
        s.fogInvRange = v[38];
        s.rayleigh = rgb(108);
        s.inscatterScale = w(105);
        s.mie = rgb(109);
        s.scatterBlend = w(110);
        s.inverseExtinction = rgb(106);
        s.valid = 1.0f;
        s.surfaceTint = rgb(105);
        s.sunColor = rgb(110);
      }
    }
  }

  bool DxvkDarkSoulsHaze::fillArgs(PostFxDarkSoulsHazeArgs& args, const VkExtent3D& extent,
                                   const Resources::Resource& linearViewZ, const Camera& camera,
                                   const float jitterOffset[2], float missLinearViewZ) const {
    args = {};
    args.camera = camera;
    args.imageSize = { extent.width, extent.height };
    const VkExtent3D viewZExtent = linearViewZ.image->mipLevelExtent(0);
    args.invViewZSize = { 1.0f / float(viewZExtent.width), 1.0f / float(viewZExtent.height) };
    // The jitter is in render-resolution pixels (the view Z buffer's own).
    args.depthJitterUv = { jitterOffset[0] / float(viewZExtent.width), jitterOffset[1] / float(viewZExtent.height) };
    args.missLinearViewZ = missLinearViewZ;
    args.skyDistance = skyDistance();

    args.fogColor = toVec3(fogColor());
    args.fogStrength = fogStrength();
    args.fogStart = fogStart();
    args.fogInvRange = fogInvRange();
    args.extinction = toVec3(extinction());
    // The game's shaders scale the extinction exponent by this constant (1 / ln(2)^2).
    args.extinctionScale = extinctionScale() * 2.08136892f;
    args.sunDirection = toVec3(sunDirection());
    args.phase = toVec3(phase());
    args.rayleigh = toVec3(rayleigh());
    args.mie = toVec3(mie());
    args.inverseExtinction = toVec3(inverseExtinction());
    args.inscatterScale = inscatterScale();
    args.surfaceTint = toVec3(surfaceTint());
    args.sunColor = toVec3(sunColor());
    args.scatterBlend = scatterBlend();
    // The user brightness (rtx.userBrightness, an EV on linear radiance) is 2^(EV / 2.2) on display values, as the scene it
    // hazes already has it (DS1 look scene pass, or the tonemapper ahead of this pass).
    args.displayScale = DxvkDarkSoulsExposure::gameExposureValue() * displayGain() * std::exp2(RtxOptions::calcUserEVBias() / 2.2f);
    if (sets() != s_setsText) {
      parseSets(sets());
    }
    for (int i = 0; i < DS1_HAZE_SET_COUNT; ++i) {
      args.sets[i] = s_sets[i];
    }
    return active();
  }

  void DxvkDarkSoulsHaze::showImguiSettings() {
    if (!ImGui::CollapsingHeader("Haze (game fog and sky scattering)")) {
      return;
    }
    ImGui::Indent();
    RemixGui::DragFloat("Haze Colour Gain", &displayGainObject(), 0.01f, 0.0f, 8.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Sky Haze Distance", &skyDistanceObject(), 10.0f, 0.0f, 100000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);

    // The game's values, as the proxy last sent them (read-only: they follow the area).
    if (!active()) {
      ImGui::TextDisabled("No values from the game yet (needs the DS1 proxy and a map draw).");
    } else if (ImGui::TreeNode("Game values (set by the proxy)")) {
      auto row = [](const char* label, const Vector3& v) { ImGui::Text("%-16s %.4g, %.4g, %.4g", label, v.x, v.y, v.z); };
      row("fog colour", fogColor());
      ImGui::Text("%-16s %.4g", "fog strength", fogStrength());
      ImGui::Text("%-16s %.4g .. +%.4g", "fog range", fogStart(), fogInvRange() > 0.0f ? 1.0f / fogInvRange() : 0.0f);
      row("extinction", extinction());
      ImGui::Text("%-16s %.4g", "distance scale", extinctionScale());
      row("sun direction", sunDirection());
      row("sun colour", sunColor());
      row("1 / extinction", inverseExtinction());
      row("rayleigh", rayleigh());
      row("mie", mie());
      row("mie phase", phase());
      row("surface tint", surfaceTint());
      ImGui::Text("%-16s %.4g", "in-scatter scale", inscatterScale());
      ImGui::Text("%-16s %.4g", "scatter blend", scatterBlend());
      ImGui::TreePop();
    }
    ImGui::Unindent();
  }

} // namespace dxvk
