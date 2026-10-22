#include "rtx_dark_souls_exposure.h"
#include "rtx_imgui.h"
#include "../imgui/imgui.h"

namespace dxvk {

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
    ImGui::Unindent();
  }

} // namespace dxvk
