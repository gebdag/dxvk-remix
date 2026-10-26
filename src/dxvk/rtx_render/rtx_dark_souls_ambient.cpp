#include "rtx_dark_souls_ambient.h"
#include "rtx_imgui.h"
#include "../imgui/imgui.h"

namespace dxvk {

  void DxvkDarkSoulsAmbient::fillShaderArgs(Ds1AmbientArgs& args) {
    args = {};
    // The sun's lightmap scale works whether or not the ambient is the sky.
    args.sunFollowsLightmap = sunFollowsLightmap() ? 1u : 0u;
    args.sunBase = vec4 { sunBase().x, sunBase().y, sunBase().z, 0.0f };
    args.sunLobe = vec4 { sunLobe().x, sunLobe().y, sunLobe().z, 0.0f };
    if (!enable() || !active()) {
      return;
    }
    const Vector3 faces[6] = { posX(), negX(), posY(), negY(), posZ(), negZ() };
    for (int i = 0; i < 6; ++i) {
      args.cube[i] = vec4 { faces[i].x * gain(), faces[i].y * gain(), faces[i].z * gain(), 0.0f };
    }
    args.enable = 1;
  }

  void DxvkDarkSoulsAmbient::showImguiSettings() {
    if (!ImGui::CollapsingHeader("Ambient light (the game's)", ImGuiTreeNodeFlags_DefaultOpen)) {
      return;
    }
    ImGui::Indent();
    RemixGui::Checkbox("Game Ambient as Sky Light", &enableObject());
    RemixGui::DragFloat("Ambient Gain", &gainObject(), 0.01f, 0.0f, 8.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::Checkbox("Sun Follows the Lightmap", &sunFollowsLightmapObject());
    if (!active()) {
      ImGui::TextDisabled("No ambient from the game yet (needs the DS1 proxy and a probe-lit draw).");
    } else {
      auto row = [](const char* label, const Vector3& v) { ImGui::Text("%-4s %.3f, %.3f, %.3f", label, v.x, v.y, v.z); };
      row("+Y", posY());
      row("-Y", negY());
      row("+X", posX());
      row("-X", negX());
      row("+Z", posZ());
      row("-Z", negZ());
    }
    ImGui::Unindent();
  }

} // namespace dxvk
