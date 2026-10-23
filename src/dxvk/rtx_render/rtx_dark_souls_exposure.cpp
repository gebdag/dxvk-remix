#include "rtx_dark_souls_exposure.h"
#include "rtx_imgui.h"
#include "../imgui/imgui.h"
#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include <algorithm>
#include <cmath>

namespace dxvk {

  void DxvkDarkSoulsExposure::adapt(float lum) {
    if (!std::isfinite(lum) || lum <= 0.0f) {
      return;
    }
    s_meteredLum = lum;
    if (!s_haveAdapted) {
      // Start where the game is: its exposure when metering begins.
      s_adapted = (gameActive() ? gameExposure() : 1.0f) * exposureDivisor();
      s_haveAdapted = true;
    }
    const float clamped = std::min(std::max(lum, adaptMinLum() * std::max(minLumScale(), 0.01f)), adaptMaxLum());
    const float target = adaptKey() / (clamped + 0.001f);
    const float k = 1.0f - std::exp2(-0.874390364f * adaptRate());
    s_adapted += k * (target - s_adapted);

    static uint32_t steps = 0;
    if (steps++ % 600 == 0) {
      Logger::info(str::format("[DS1 exposure] metered lum ", lum, " -> E ", s_adapted, " (target ", target,
                               "; the game's own c135.x ", gameExposure() * exposureDivisor(), ")"));
    }
  }

  void DxvkDarkSoulsExposure::showImguiSettings() {
    if (!ImGui::CollapsingHeader("Exposure (the game's)", ImGuiTreeNodeFlags_DefaultOpen)) {
      return;
    }
    ImGui::Indent();
    RemixGui::Checkbox("Follow the Game's Exposure", &followGameObject());
    RemixGui::DragFloat("Calibration", &calibrationObject(), 0.01f, 0.01f, 16.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::Checkbox("Game's Tone Curve (saturate)", &gameCurveObject());
    if (!gameActive()) {
      ImGui::TextDisabled("No exposure from the game yet (needs the DS1 proxy and a lit draw).");
    } else {
      ImGui::Text("game exposure %.3f (gamma)  ->  Remix exposure %.3f (linear)", gameExposure(), factor());
    }
    RemixGui::Checkbox("Meter Remix's Frame (the game's meter)", &meterFrameObject());
    if (metering()) {
      ImGui::Text("metered lum %.4f  ->  E %.3f, exposure %.3f (game's own value %.3f)",
                  s_meteredLum, s_adapted, gameExposureValue(), gameExposure());
    }
    ImGui::Unindent();
  }

} // namespace dxvk
