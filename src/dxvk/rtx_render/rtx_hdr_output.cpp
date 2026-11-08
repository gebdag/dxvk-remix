#include "rtx_hdr_output.h"
#include "rtx_imgui.h"
#include "../imgui/imgui.h"
#include "../../util/util_string.h"

namespace dxvk {

  void DxvkHdrOutput::showImguiSettings() {
    if (!ImGui::CollapsingHeader("HDR10 output (experimental)")) {
      return;
    }
    ImGui::Indent();
    RemixGui::Checkbox("HDR10 Output", &enableObject());
    if (enable()) {
      if (swapchainHdr()) {
        ImGui::Text("Swap chain: HDR10 (10-bit, ST 2084 / BT.2020), back buffer %s", floatBackBuffers() ? "16-bit float" : "8-bit (switching)");
        if (!renderHeadroom()) {
          ImGui::TextDisabled("Switching: the next frame renders with headroom.");
        }
      } else {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                           "The display gave no HDR10 swap chain: the output stays SDR (turn on HDR for this display "
                           "in Windows > System > Display, then re-toggle).");
      }
    } else {
      ImGui::TextDisabled("Off: the stock 8-bit sRGB output.");
    }
    ImGui::BeginDisabled(!enable());
    RemixGui::DragFloat("SDR / UI White (nits)", &paperWhiteNitsObject(), 1.0f, 80.0f, 500.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Display Peak (nits)", &peakNitsObject(), 5.0f, 300.0f, 10000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::SliderFloat("Roll-off Starts (share of peak)", &shoulderStartObject(), 0.3f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::Checkbox("sRGB Piecewise Decode (default: 2.2)", &srgbDecodeObject());
    ImGui::Separator();
    RemixGui::Checkbox("Game Image Pipeline: Keep Clipped Highlights", &ds1HeadroomObject());
    ImGui::TextDisabled("DS1 composites the frame into its own scene target: the proxy's [HDR] Enabled makes it float and\n"
                        "skips the brightness LUT (takes effect at the next game start). Without it nothing above white reaches the display.");
    RemixGui::Checkbox("Remix Tonemapper: Keep Clipped Channels", &remixClipRecoveryObject());
    RemixGui::SliderFloat("Remix Tonemapper: Highlight Expansion", &remixHighlightExpansionObject(), 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::SliderFloat("Remix Tonemapper: Expansion Knee", &remixHighlightKneeObject(), 0.1f, 0.95f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Separator();
    if (ImGui::Button("Dump Final Frame (DDS)")) {
      dumpFrameObject().setDeferred(true);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Screenshots\\hdrFinal_*.dds, 16-bit float, gamma-encoded, 1.0 = SDR white");
    ImGui::EndDisabled();
    ImGui::Text("headroom %.2fx (%.0f / %.0f nits)", headroom(), peakNits(), paperWhiteNits());
    ImGui::Unindent();
  }

} // namespace dxvk
