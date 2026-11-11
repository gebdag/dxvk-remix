#include "rtx_dark_souls_motion_blur.h"
#include "rtx_imgui.h"
#include "rtx_postFx.h"
#include "../imgui/imgui.h"

#include <algorithm>

namespace dxvk {

  // A hitch or a paused frame says nothing about the frame rate the blur is seen at.
  constexpr float kMinFrameMs = 1.0f;
  constexpr float kMaxFrameMs = 100.0f;

  float DxvkDarkSoulsMotionBlur::s_frameMs = 1000.0f / 60.0f;

  void DxvkDarkSoulsMotionBlur::update(float realFrameMs) {
    const float frameMs = std::clamp(realFrameMs, kMinFrameMs, kMaxFrameMs);
    const float weight = std::clamp(frameTimeSmoothing(), 0.0f, 1.0f);
    s_frameMs += (frameMs - s_frameMs) * weight;
  }

  float DxvkDarkSoulsMotionBlur::exposureFraction() {
    return std::clamp(shutterMs() / s_frameMs, 0.0f, std::max(maxExposureFraction(), 0.0f));
  }

  void DxvkDarkSoulsMotionBlur::showImguiSettings() {
    if (!ImGui::CollapsingHeader("Motion blur")) {
      return;
    }
    ImGui::Indent();
    RemixGui::Checkbox("Motion Blur", &DxvkPostFx::enableMotionBlurObject());
    RemixGui::Checkbox("Dark Souls Motion Blur (same at any frame rate)", &enableObject());
    if (enable()) {
      RemixGui::DragFloat("Shutter (ms, 11.4 = game at 30 fps)", &shutterMsObject(), 0.1f, 0.0f, 50.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragFloat("Longest Blur (screen fraction per side)", &blurDiameterFractionObject(), 0.005f, 0.001f, 0.5f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::Text("Now: %.0f fps, exposure fraction %.2f", 1000.0f / s_frameMs, exposureFraction());
    } else {
      ImGui::TextWrapped("Off: Remix's own motion blur settings (Post-Processing) apply.");
    }
    ImGui::Unindent();
  }

} // namespace dxvk
