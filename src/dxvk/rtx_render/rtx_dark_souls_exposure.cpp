#include "rtx_dark_souls_exposure.h"
#include "rtx_dark_souls_look.h"
#include "rtx_imgui.h"
#include "../imgui/imgui.h"
#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include <algorithm>
#include <cmath>

namespace dxvk {

  bool DxvkDarkSoulsExposure::replacesAutoExposure() {
    return DxvkDarkSoulsLook::isActive();
  }

  void DxvkDarkSoulsExposure::adapt(float lum, float ambientScale) {
    if (!std::isfinite(lum) || lum <= 0.0f) {
      return;
    }
    s_meteredLum = lum;
    if (!s_haveAdapted) {
      // Start where the game is: its exposure when metering begins.
      s_adapted = (gameActive() ? gameExposure() : 1.0f) * exposureDivisor();
      s_haveAdapted = true;
    }
    // The game's clamp is for a frame lit by its unoccluded ambient; the path-traced frame keeps
    // ambientScale of that light, so the same clamp in its terms is ambientScale times as low. Never
    // above 1: where Remix is brighter (its direct sun is not in the estimate) the game's clamp stands.
    s_ambientScale = ambientClamp() && std::isfinite(ambientScale)
                   ? std::clamp(ambientScale, std::clamp(ambientClampMinScale(), 0.01f, 1.0f), 1.0f) : 1.0f;
    const float minLum = adaptMinLum() * std::max(minLumScale(), 0.01f) * s_ambientScale;
    const float maxLum = adaptMaxLum() * s_ambientScale;
    const float clamped = std::min(std::max(lum, minLum), maxLum);
    const float target = adaptKey() / (clamped + 0.001f);
    const float k = 1.0f - std::exp2(-0.874390364f * adaptRate());
    s_adapted += k * (target - s_adapted);

    static uint32_t steps = 0;
    if (steps++ % 600 == 0) {
      Logger::info(str::format("[DS1 exposure] metered lum ", lum, " ambient share ", s_ambientScale, " -> E ", s_adapted,
                               " (target ", target, "; the game's own c135.x ", gameExposure() * exposureDivisor(), ")"));
    }
  }

  void DxvkDarkSoulsExposure::showImguiSettings() {
    if (!ImGui::CollapsingHeader("Exposure (the game's)")) {
      return;
    }
    ImGui::Indent();
    if (!gameActive()) {
      ImGui::TextDisabled("No exposure from the game yet (needs the DS1 proxy and a lit draw).");
    } else {
      ImGui::Text("game exposure %.3f (gamma)  ->  Remix exposure %.3f (linear)", gameExposure(), factor());
    }
    RemixGui::Checkbox("Meter Remix's Frame (the game's meter)", &meterFrameObject());
    ImGui::BeginDisabled(!meterFrame());
    RemixGui::DragFloat("Exposure Floor (1 = game's limit)", &minLumScaleObject(), 0.005f, 0.05f, 1.0f, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
    RemixGui::Checkbox("Clamp Follows the Path-Traced Ambient", &ambientClampObject());
    ImGui::EndDisabled();
    if (metering()) {
      ImGui::Text("metered lum %.4f  ->  E %.3f, exposure %.3f (game's own value %.3f)",
                  s_meteredLum, s_adapted, gameExposureValue(), gameExposure());
      ImGui::Text("ambient share %.3f", s_ambientScale);
      ImGui::Text("luminance clamp %.3f .. %.3f (game's %.3f .. %.3f)",
                  adaptMinLum() * std::max(minLumScale(), 0.01f) * s_ambientScale, adaptMaxLum() * s_ambientScale,
                  adaptMinLum(), adaptMaxLum());
    }
    ImGui::Unindent();
  }

} // namespace dxvk
