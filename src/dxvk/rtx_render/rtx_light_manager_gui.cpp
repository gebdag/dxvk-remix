/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#include <vector>
#include <cmath>
#include <cassert>

#include "rtx_light_manager.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "rtx_utils.h"

#include "../d3d9/d3d9_state.h"
#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/raytrace_args.h"
#include "math.h"
#include "rtx_lights.h"
#include "rtx_imgui.h"
#include "../util/util_vector.h"
#include "rtx_intersection_test_helpers.h"
#include "rtx_dark_souls_light_falloff.h"
#include <unordered_map>
#include <atomic>
#include <algorithm>


namespace dxvk {
  struct LightManagerGuiSettings {
    RTX_OPTION_FLAG("rtx.lights", bool, enableDebugMode, false, RtxOptionFlags::NoSave, "Enables light debug visualization.");
    RTX_OPTION_FLAG("rtx.lights", bool, debugDrawLightHashes, false, RtxOptionFlags::NoSave, "Draw light hashes of all visible ob screen lights, when enableDebugMode=true.");
  };

  RemixGui::ComboWithKey<LightManager::FallbackLightMode> fallbackLightModeCombo {
    "Fallback Light Mode",
    RemixGui::ComboWithKey<LightManager::FallbackLightMode>::ComboEntries { {
        {LightManager::FallbackLightMode::Never, "Never"},
        {LightManager::FallbackLightMode::NoLightsPresent, "No Lights Present"},
        {LightManager::FallbackLightMode::Always, "Always"},
    } }
  };

  RemixGui::ComboWithKey<LightManager::FallbackLightType> fallbackLightTypeCombo {
    "Fallback Light Type",
    RemixGui::ComboWithKey<LightManager::FallbackLightType>::ComboEntries { {
        {LightManager::FallbackLightType::Distant, "Distant"},
        {LightManager::FallbackLightType::Sphere, "Sphere"},
    } }
  };

  void LightManager::showImguiLightOverview() {
    if (RemixGui::CollapsingHeader("Light Statistics")) {
      ImGui::Indent();
      ImGui::Text("Sphere Lights: %d", getLightCount(lightTypeSphere));
      ImGui::Text("Rectangle Lights: %d", getLightCount(lightTypeRect));
      ImGui::Text("Disk Lights: %d", getLightCount(lightTypeDisk));
      ImGui::Text("Cylinder Lights: %d", getLightCount(lightTypeCylinder));
      ImGui::Text("Distant Lights: %d", getLightCount(lightTypeDistant));
      ImGui::Text("Total Lights: %d", getActiveCount());
      RemixGui::Separator();
      RemixGui::Checkbox("Enable Debug Visualization", &LightManagerGuiSettings::enableDebugModeObject());
      {
        ImGui::BeginDisabled(!LightManagerGuiSettings::enableDebugMode());
        RemixGui::Checkbox("Draw Light Hashes", &LightManagerGuiSettings::debugDrawLightHashesObject());
        ImGui::EndDisabled();
      }
      ImGui::Dummy({ 0,2 });
      ImGui::Unindent();
    }
  }


  void LightManager::showImguiSettings() {
    bool lightSettingsDirty = false;

    const auto separator = []() {
      ImGui::Dummy({ 0,2 });
      RemixGui::Separator();
      ImGui::Dummy({ 0,2 });
    };

    if (RemixGui::CollapsingHeader("Light Translation")) {
      ImGui::Dummy({ 0,2 });
      ImGui::Indent();

      lightSettingsDirty |= RemixGui::Checkbox("Suppress Light Keeping", &suppressLightKeepingObject());

      separator();

      const bool disableDirectional = ignoreGameDirectionalLights();
      const bool disablePointSpot = ignoreGamePointLights() && ignoreGameSpotLights();

      // TODO(REMIX-3124) remove this warning
      ImGui::TextColored(ImVec4{ 0.87f, 0.75f, 0.20f, 1.0f }, "Warning: changing Light Conversion values can cause crashes.\nManually entering values is safer than dragging.");
      ImGui::BeginDisabled(disablePointSpot);
      ImGui::Text("Sphere / Spot Light settings");
      lightSettingsDirty |= RemixGui::Checkbox("Use Least Squares Intensity", &calculateLightIntensityUsingLeastSquaresObject());
      lightSettingsDirty |= RemixGui::DragFloat("Light Radius", &lightConversionSphereLightFixedRadiusObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      lightSettingsDirty |= RemixGui::DragFloat("Intensity Factor", &lightConversionIntensityFactorObject(), 0.01f, 0.0f, 2.f, "%.3f");
      lightSettingsDirty |= RemixGui::OptionalDragFloat("Max Intensity", &lightConversionMaxIntensityObject(), 1000000.f, 1.f, 0.0f, FLT_MAX, "%.1f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::EndDisabled();

      separator();

      ImGui::BeginDisabled(disableDirectional);
      ImGui::Text("Distant Light settings");
      lightSettingsDirty |= RemixGui::DragFloat("Fixed Intensity", &lightConversionDistantLightFixedIntensityObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      lightSettingsDirty |= RemixGui::DragFloat("Fixed Angle", &lightConversionDistantLightFixedAngleObject(), 0.01f, 0.0f, kPi, "%.4f rad", ImGuiSliderFlags_AlwaysClamp);
      ImGui::EndDisabled();

      separator();

      ImGui::Text("Rectangle Light settings");
      lightSettingsDirty |= RemixGui::Checkbox("Enable Legacy RectLight Cone Shaping", &enableLegacyRectLightConeShapingObject());
      ImGui::BeginDisabled(enableLegacyRectLightConeShaping());
      lightSettingsDirty |= RemixGui::Checkbox("Enable RectLight Cone Shaping Ratio Scaling", &enableRectLightConeShapingRatioScalingObject());
      ImGui::EndDisabled();

      separator();

      ImGui::Text("Ignore Game Lights:");
      ImGui::Indent();
      lightSettingsDirty |= RemixGui::Checkbox("Directional", &ignoreGameDirectionalLightsObject());
      lightSettingsDirty |= RemixGui::Checkbox("Point", &ignoreGamePointLightsObject());
      lightSettingsDirty |= RemixGui::Checkbox("Spot", &ignoreGameSpotLightsObject());
      ImGui::Unindent();

      separator();

      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Fallback Light")) {
      ImGui::Dummy({ 0,2 });
      ImGui::Indent();

      lightSettingsDirty |= fallbackLightModeCombo.getKey(&fallbackLightModeObject());

      ImGui::BeginDisabled(fallbackLightMode() == FallbackLightMode::Never);
      {
        lightSettingsDirty |= fallbackLightTypeCombo.getKey(&fallbackLightTypeObject());

        lightSettingsDirty |= RemixGui::DragFloat3("Fallback Light Radiance", &fallbackLightRadianceObject(), 0.1f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        lightSettingsDirty |= RemixGui::DragFloat("Fallback Light Volumetric Radiance Scale", &fallbackLightVolumetricRadianceScaleObject(), 0.01f, 0.0f, FLOAT16_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);

        if (fallbackLightType() == FallbackLightType::Distant) {
          lightSettingsDirty |= RemixGui::DragFloat3("Fallback Light Direction", &fallbackLightDirectionObject(), 0.1f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
          lightSettingsDirty |= RemixGui::DragFloat("Fallback Light Angle", &fallbackLightAngleObject(), 0.01f, 0.0f, FLT_MAX, "%.3f deg", ImGuiSliderFlags_AlwaysClamp);
        } else if (fallbackLightType() == FallbackLightType::Sphere) {
          lightSettingsDirty |= RemixGui::DragFloat("Fallback Light Radius", &fallbackLightRadiusObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
          lightSettingsDirty |= RemixGui::DragFloat3("Fallback Light Position Offset", &fallbackLightPositionOffsetObject(), 0.1f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

          lightSettingsDirty |= RemixGui::Checkbox("Enable Fallback Light Shaping", &enableFallbackLightShapingObject());

          if (enableFallbackLightShaping()) {
            ImGui::Indent();

            lightSettingsDirty |= RemixGui::Checkbox("Fallback Light Match View Axis", &enableFallbackLightViewPrimaryAxisObject());

            if (!enableFallbackLightViewPrimaryAxis()) {
              lightSettingsDirty |= RemixGui::DragFloat3("Fallback Light Primary Axis", &fallbackLightPrimaryAxisObject(), 0.1f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            }

            lightSettingsDirty |= RemixGui::DragFloat("Fallback Light Cone Angle", &fallbackLightConeAngleObject(), 0.01f, 0.0f, FLT_MAX, "%.3f deg", ImGuiSliderFlags_AlwaysClamp);
            lightSettingsDirty |= RemixGui::DragFloat("Fallback Light Cone Softness", &fallbackLightConeSoftnessObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            lightSettingsDirty |= RemixGui::DragFloat("Fallback Light Focus Exponent", &fallbackLightFocusExponentObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);

            ImGui::Unindent();
          }
        }
      }
      ImGui::EndDisabled();

      ImGui::Unindent();
    }

    // Clear the lights and fallback light if the settings are dirty to recreate the lights on the next frame.
    if (lightSettingsDirty) {
      clearFromUIThread();
    }
  }

  bool transformToScreen(const Matrix4& worldToProj, const Vector2 screen, const Vector3 worldPos, ImVec2& outScreenPos) {
    Vector4 positionPS = worldToProj * Vector4(worldPos.x, worldPos.y, worldPos.z, 1.f);
    positionPS.xyz() /= std::abs(positionPS.w);

    // Projection -> screen transform
    outScreenPos = { (positionPS.x * 0.5f + 0.5f) * screen.x, (-positionPS.y * 0.5f + 0.5f) * screen.y };

    // Coarse culling
    return !(positionPS.x < -1.f || positionPS.y < -1.f || positionPS.x > 1.f || positionPS.y > 1.f || positionPS.w < 0.f);
  }

  void drawLightHash(XXH64_hash_t h,
                     const Vector3& position,
                     const Matrix4& worldToProj,
                     ImDrawList* drawList,
                     bool isFromInstance = false) {
    constexpr auto safe = 2.0f;

    const ImDrawListSharedData* data = ImGui::GetDrawListSharedData();
    assert(data && data->Font && data->FontSize > 0);

    ImVec2 screenPos;
    {
      const ImGuiViewport* viewport = ImGui::GetMainViewport();
      transformToScreen(worldToProj, Vector2{ viewport->Size.x, viewport->Size.y }, position, screenPos);
    }
    std::string str = hashToString(h);
    ImU32 backColor = IM_COL32(0, 0, 0, 200);

    screenPos.y += 24; // offset to not obstruct the original point
    if (isFromInstance) {
      screenPos.y += data->FontSize + safe * 2;
      str = "Instance: " + str;
      backColor = IM_COL32(0, 0, 70, 200);
    }

    const ImVec2 extent = data->Font->CalcTextSizeA(data->FontSize, FLT_MAX, 0, str.c_str());

    const auto offsetText = ImVec2 { screenPos.x - (extent.x / 2), screenPos.y - (extent.y / 2) };
    const auto offsetMin = ImVec2 { screenPos.x - (extent.x / 2 + safe), screenPos.y - (extent.y / 2 + safe) };
    const auto offsetMax = ImVec2 { screenPos.x + (extent.x / 2 + safe), screenPos.y + (extent.y / 2 + safe) };

    drawList->AddRectFilled(offsetMin, offsetMax, backColor);
    drawList->AddText(data->Font, data->FontSize, offsetText, IM_COL32_WHITE, str.c_str());
  }

  void drawLightHashes(const RtLight& light, const Matrix4& worldToProj, ImDrawList* drawList) {
    if (!LightManagerGuiSettings::debugDrawLightHashes()) {
      return;
    }
    drawLightHash(light.getInitialHash(), light.getPosition(), worldToProj, drawList);
  }

  void drawToolTip(const RtLight& light) {
    const ImVec2 windowSize = ImVec2(100, 200); 
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMousePos().x + windowSize.x / 2, ImGui::GetMousePos().y + windowSize.y / 2), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.6f));
    if (ImGui::Begin("Light Info", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
      constexpr static std::pair<RtLightType, const char*> lightTypes[] = {
            { RtLightType::Sphere,   "Sphere" },
            { RtLightType::Rect,     "Rect" },
            { RtLightType::Disk,     "Disk" },
            { RtLightType::Cylinder, "Cylinder" },
            { RtLightType::Distant,  "Distant" },
          };
      ImGui::Text("Position: %.2f %.2f %.2f", light.getPosition().x, light.getPosition().y, light.getPosition().z);
      ImGui::Text("Direction: %.2f %.2f %.2f", light.getDirection().x, light.getDirection().y, light.getDirection().z);
      ImGui::Text("Radiance: %.2f %.2f %.2f", light.getRadiance().x, light.getRadiance().y, light.getRadiance().z);
      
      ImGui::Text("Type: %s", lightTypes[(uint32_t) light.getType()].second);
      const RtLightShaping* pShaping = nullptr;
      switch (light.getType()) {
      case RtLightType::Sphere:
        pShaping = &light.getSphereLight().getShaping();
        ImGui::Text("\tRadius: %.2f", light.getSphereLight().getRadius());
        break;
      case RtLightType::Rect:
        pShaping = &light.getRectLight().getShaping();
        ImGui::Text("\tDimensions: %.2f %.2f", light.getRectLight().getDimensions().x, light.getRectLight().getDimensions().y);
        break;
      case RtLightType::Disk:
        pShaping = &light.getDiskLight().getShaping();
        ImGui::Text("\tHalf-Dimensions: %.2f %.2f", light.getDiskLight().getHalfDimensions().x, light.getDiskLight().getHalfDimensions().y);
        break;
      case RtLightType::Cylinder:
        ImGui::Text("\tRadius: %.2f", light.getCylinderLight().getRadius());
        ImGui::Text("\tLength: %.2f", light.getCylinderLight().getAxisLength());
        ImGui::Text("\tAxis: %.2f %.2f %.2f", light.getCylinderLight().getAxis().x, light.getCylinderLight().getAxis().y, light.getCylinderLight().getAxis().z);
        break;
      case RtLightType::Distant:
        break;
      }

      if (pShaping) {
        if (pShaping->getEnabled()) {
          ImGui::Text("Light Shaping: Enabled");
          ImGui::Text("\tDirection: %.2f %.2f %.2f", pShaping->getDirection().x, pShaping->getDirection().y, pShaping->getDirection().z);
          ImGui::Text("\tCone Angle: %.2f deg", std::acos(pShaping->getCosConeAngle()) * kRadiansToDegrees);
          ImGui::Text("\tCone Softness: %.2f", pShaping->getConeSoftness());
          ImGui::Text("\tFocus Exponent: %.2f", pShaping->getFocusExponent());
        } else {
          ImGui::Text("Light Shaping: Disabled");
        }
      } else {
        ImGui::Text("Light Shaping: Not Supported");
      }

      ImGui::Text("Volumetric Radiance Scale: %.2f", light.getVolumetricRadianceScale());
      ImGui::Text("Initial Hash: 0x%" PRIx64, light.getInitialHash());
      ImGui::Text("Transformed Hash: 0x%" PRIx64, light.getTransformedHash());
      if (light.getPrimInstanceOwner().getReplacementInstance() != nullptr) {
        ImGui::Text("Replacement Index: %d", light.getPrimInstanceOwner().getReplacementIndex());
        ImGui::Text("Is Root: %s", light.getPrimInstanceOwner().isRoot(&light) ? "Yes" : "No");
        switch (light.getPrimInstanceOwner().getReplacementInstance()->root.getType()) {
          case PrimInstance::Type::Instance:
            ImGui::Text("Replacement Root is a Mesh");
            break;
          case PrimInstance::Type::Light:
            ImGui::Text("Replacement Root is a Light");
            break;
          case PrimInstance::Type::Graph:
            ImGui::Text("Replacement Root is a Graph");
            break;
          case PrimInstance::Type::None:
            ImGui::Text("Replacement Root is Unknown");
            break;
        }
      }
      ImGui::Text("Frame last touched: %d", light.getFrameLastTouched());
      RemixGui::Separator();

      if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        ImGui::SetClipboardText(hashToString(light.getInitialHash()).c_str());
      }

    }
    // Note: End must always be called even if Begin returns false (unlike other ImGui patterns).
    ImGui::End();
    ImGui::PopStyleColor();
  }

  struct DrawResult {
    bool mouseHover { false };
    bool isVisible { false };

    DrawResult& operator |=(const DrawResult& other) {
      this->mouseHover |= other.mouseHover;
      this->isVisible |= other.isVisible;
      return *this;
    }
  };

  DrawResult drawSphereLightDebug(const RtSphereLight& sphereLight, const Matrix4& worldToProj, const Vector3& cameraRight, const ImU32 colHex, cFrustum& frustum, ImDrawList* drawList) {
    if (!sphereIntersectsFrustum(frustum, sphereLight.getPosition(), sphereLight.getRadius())) {
      return {};
    }
    // Fork (DS1): a light centre level with or behind the camera (a free camera flown into a light's sphere) projects with
    // w <= 0: the division by it made the circle infinite, ImGui's segment table took (int)inf as a negative index and the
    // runtime crashed (access violation in ImDrawList::_CalcCircleAutoSegmentCount, 2026-11-14). Such a light is not drawn.
    const Vector3& centre = sphereLight.getPosition();
    if (!((worldToProj * Vector4(centre.x, centre.y, centre.z, 1.f)).w > 1e-4f)) {
      return {};
    }
    ImVec2 screenPos[2];
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    transformToScreen(worldToProj, Vector2{ viewport->Size.x, viewport->Size.y }, centre, screenPos[0]);
    transformToScreen(worldToProj, Vector2{ viewport->Size.x, viewport->Size.y }, centre + cameraRight * sphereLight.getRadius(), screenPos[1]);
    float radius = std::max(1.f, sqrtf(ImLengthSqr(ImVec2(screenPos[0].x - screenPos[1].x, screenPos[0].y - screenPos[1].y))));
    if (!std::isfinite(radius) || !std::isfinite(screenPos[0].x) || !std::isfinite(screenPos[0].y)) {
      return {};
    }
    // Larger than a few screens is no different on screen, and keeps the circle's segment count bounded.
    radius = std::min(radius, 4.f * std::max(viewport->Size.x, viewport->Size.y));
    drawList->AddCircleFilled(screenPos[0], radius, colHex);

    return DrawResult {
      ImLengthSqr(ImVec2(screenPos[0].x - ImGui::GetMousePos().x, screenPos[0].y - ImGui::GetMousePos().y)) <= radius * radius,
      true,
    };
  }

  DrawResult drawRectLightDebug(const Vector3& position, const Vector3& xAxis, const Vector3& yAxis, const Vector2& dimensions, const Matrix4& worldToProj, const ImU32 colHex, cFrustum& frustum, ImDrawList* drawList) {
    if (!rectIntersectsFrustum(frustum, position, dimensions, xAxis, yAxis)) {
      return {};
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    Vector3 rectBounds[4];
    rectBounds[0] = position - dimensions.x * 0.5f * xAxis - dimensions.y * yAxis * 0.5f;
    rectBounds[1] = position - dimensions.x * 0.5f * xAxis + dimensions.y * yAxis * 0.5f;
    rectBounds[2] = position + dimensions.x * 0.5f * xAxis + dimensions.y * yAxis * 0.5f;
    rectBounds[3] = position + dimensions.x * 0.5f * xAxis - dimensions.y * yAxis * 0.5f;
    ImVec2 screenPos[4];
    for (uint32_t i = 0; i < 4; i++) {
      transformToScreen(worldToProj, Vector2{ viewport->Size.x, viewport->Size.y }, rectBounds[i], screenPos[i]);
    }
    drawList->AddQuadFilled(screenPos[0], screenPos[1], screenPos[2], screenPos[3], colHex);
    return DrawResult {
      ImTriangleContainsPoint(screenPos[0], screenPos[1], screenPos[2], ImGui::GetMousePos()) || ImTriangleContainsPoint(screenPos[1], screenPos[2], screenPos[3], ImGui::GetMousePos()),
      true,
    };
  }

  DrawResult drawDiskLightDebug(const Vector3& position, const Vector3& xAxis, const Vector3& yAxis, const Vector2& radius, const Matrix4& worldToProj, const ImU32 colHex, cFrustum& frustum, ImDrawList* drawList) {
    if (!rectIntersectsFrustum(frustum, position, radius * 2, xAxis, yAxis)) {
      return {};
    }
    const uint32_t numPoints = 16;
    ImVec2 screenPos[numPoints];
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    for (uint32_t i = 0; i < numPoints; i++) {
      float theta = (float) i * kPi * 2 / numPoints;
      Vector3 worldPos = position + radius.x * cos(theta) * xAxis + radius.y * yAxis * sin(theta);
      transformToScreen(worldToProj, Vector2{ viewport->Size.x, viewport->Size.y }, worldPos, screenPos[i]);
    }
    drawList->AddConvexPolyFilled(&screenPos[0], numPoints, colHex);

    for (uint32_t i = 1; i < numPoints-1; i++) {
      if (ImTriangleContainsPoint(screenPos[0], screenPos[i], screenPos[i+1], ImGui::GetMousePos())) {
        return DrawResult { true, true };
      }
    }
    return DrawResult { false, true };
  }

  DrawResult drawCylinderLightDebug(const RtCylinderLight& cylinderLight, const Matrix4& worldToProj, const ImU32 colHex, cFrustum& frustum, ImDrawList* drawList) {
    const Vector3 pos = cylinderLight.getPosition();
    const Vector3 axis = cylinderLight.getAxis();
    const float radius = cylinderLight.getRadius();
    const float sign = axis.z < 0 ? -1.f : 1.f;

    const float a = 1.0f / (sign + axis.z);
    const float b = axis.x * axis.y * a;

    const Vector3 tangent = Vector3(1.0f + sign * axis.x * axis.x * a, sign * b, -sign * axis.x);
    const Vector3 bitangent = Vector3(b, sign + axis.y * axis.y * a, -axis.y);

    auto result = DrawResult{};
    result |= drawDiskLightDebug(pos + axis * cylinderLight.getAxisLength() * 0.5, tangent, bitangent, Vector2(radius, radius), worldToProj, colHex, frustum, drawList);
    result |= drawDiskLightDebug(pos - axis * cylinderLight.getAxisLength() * 0.5, tangent, bitangent, Vector2(radius, radius), worldToProj, colHex, frustum, drawList);

    const uint32_t numPoints = 16;
    for (uint32_t i = 0; i < numPoints; i++) {
      float theta = (float) i * kPi * 2 / numPoints;
      float theta1 = (float) (i + 1) * kPi * 2 / numPoints;
      const Vector3 position = pos + radius * cos(theta) * tangent + radius * bitangent * sin(theta);
      const Vector3 position1 = pos + radius * cos(theta1) * tangent + radius * bitangent * sin(theta1);
      result |= drawRectLightDebug((position + position1) * 0.5f, normalize(position1 - position), axis, Vector2(length(position1 - position), cylinderLight.getAxisLength()), worldToProj, colHex, frustum, drawList);
    }
    return result;
  }

  void LightManager::showImguiDebugVisualization() const {
    if (!LightManagerGuiSettings::enableDebugMode()) {
      return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.0f));
    if (ImGui::Begin("Light Debug View", nullptr, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings)) {
      std::lock_guard<std::mutex> lock(m_lightUIMutex);
      ImDrawList* drawList = ImGui::GetWindowDrawList();
      drawList->PushClipRectFullScreen();
      const RtCamera& camera = device()->getCommon()->getSceneManager().getCamera();
      const Matrix4 worldToProj = camera.getViewToProjection() * camera.getWorldToView(true);

      cFrustum frustum;
      frustum.Setup(NDC_D3D, *reinterpret_cast<const float4x4*>(&worldToProj));

      for (auto&& linearizedLight : m_linearizedLights) {
        const RtLight* light = linearizedLight;
        if (light->getType() == RtLightType::Distant) {
          continue;
        }

        if (light->getType() > RtLightType::Distant) {
          // This happens because the linearizedLights stored pointers to the actual lights.
          // the actual lights can be garbage collected after linearizedLights is made, but before this function runs.
          Logger::err("tried to use a deleted light in showImguiDebugVisualization.");
          continue;
        }

        Vector4 color = light->getColorAndIntensity();
        const ImU32 colHex = ImColor(color.x, color.y, color.z);

        auto result = DrawResult{};
        switch (light->getType()) {
        case RtLightType::Sphere:
        {
          result = drawSphereLightDebug(light->getSphereLight(), worldToProj, Vector3{ camera.getViewToWorld(true)[0].xyz() }, colHex, frustum, drawList);
          break;
        }
        case RtLightType::Rect:
        {
          const RtRectLight& rectLight = light->getRectLight();
          result = drawRectLightDebug(rectLight.getPosition(), rectLight.getXAxis(), rectLight.getYAxis(), rectLight.getDimensions(), worldToProj, colHex, frustum, drawList);
          break;
        }
        case RtLightType::Disk:
        {
          const RtDiskLight& diskLight = light->getDiskLight();
          result = drawDiskLightDebug(diskLight.getPosition(), diskLight.getXAxis(), diskLight.getYAxis(), diskLight.getHalfDimensions(), worldToProj, colHex, frustum, drawList);
          break;
        }
        case RtLightType::Cylinder:
        {
          result = drawCylinderLightDebug(light->getCylinderLight(), worldToProj, colHex, frustum, drawList);
          break;
        }
        default:
          break;

        }

        if (result.mouseHover) {
          drawToolTip(*light);
        }
        else if (result.isVisible) {
          drawLightHashes(*light, worldToProj, drawList);
        }
      }

      drawList->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleColor();
  }


  // ---------------------------------------------------------------------------------------------------------------
  // Fork (DS1): the light view. Every light Remix renders, coloured by where its values come from and named after
  // the proxy's labels (rtx.ds1Lights.labels, game_lights::send_labels in the DS1 proxy).

  struct Ds1LightViewSettings {
    RTX_OPTION_FLAG("rtx.ds1Lights", bool, showView, false, RtxOptionFlags::NoSave,
                    "DS1: draw every light Remix renders in the world, coloured by where its values come from.");
    RTX_OPTION_FLAG("rtx.ds1Lights", bool, showViewNames, true, RtxOptionFlags::NoSave, "DS1 light view: name each light beside it.");
    RTX_OPTION_FLAG("rtx.ds1Lights", float, viewDistance, 40.0f, RtxOptionFlags::NoSave,
                    "DS1 light view: lights up to this far from the camera, in metres.");
  };

  namespace {
    struct Ds1Provenance {
      char code;
      const char* name;
      const char* detail;
      ImU32 colour;
    };

    const Ds1Provenance kDs1Provenance[] = {
      { 'G', "Game", "the game's own light, read live from the engine (map, character, FX and player lights, the sun)", IM_COL32(90, 220, 90, 255) },
      { 'D', "Derived", "the developers' effect-light rule applied to an effect the game draws without a light (effect table)", IM_COL32(80, 200, 255, 255) },
      { 'T', "Table", "a table of ours from the game's placements (map flames, lamp meshes)", IM_COL32(255, 220, 60, 255) },
      { 'H', "By hand", "placed or set by hand (custom lights, a sun set in area_overrides.ini)", IM_COL32(230, 90, 230, 255) },
      { 'X', "Guess", "guessed from what is drawn (particle stand-ins)", IM_COL32(255, 90, 60, 255) },
      { 'M', "Proxy", "the proxy's own (menu key light, debug probe)", IM_COL32(170, 170, 170, 255) },
      { '?', "Not DS1", "not sent by the DS1 proxy (Remix's fallback light, a D3D9 light or a replacement asset's light)", IM_COL32(255, 255, 255, 255) },
    };
    constexpr int kDs1ProvenanceCount = int(std::size(kDs1Provenance));
    bool s_ds1ShowProvenance[kDs1ProvenanceCount] = { true, true, true, true, true, true, true };
    bool s_ds1OnlyOnScreen = true;
    uint64_t s_ds1Selected = 0;

    int ds1ProvenanceIndex(char code) {
      for (int i = 0; i < kDs1ProvenanceCount; ++i) {
        if (kDs1Provenance[i].code == code) {
          return i;
        }
      }
      return kDs1ProvenanceCount - 1;
    }

    struct Ds1LightEntry {
      const LightManager::Ds1LightSnapshotEntry* light = nullptr;
      int provenance = 0;
      float distance = 0.0f;
      bool onScreen = false;
      ImVec2 screen {};
    };

    const char* ds1LightType(RtLightType type) {
      switch (type) {
      case RtLightType::Sphere: return "Sphere";
      case RtLightType::Rect: return "Rect";
      case RtLightType::Disk: return "Disk";
      case RtLightType::Cylinder: return "Cylinder";
      case RtLightType::Distant: return "Distant";
      default: return "?";
      }
    }

    // The snapshot's lights as seen from the camera now, nearest first.
    std::vector<Ds1LightEntry> ds1ViewLights(const std::vector<LightManager::Ds1LightSnapshotEntry>& snapshot, const Matrix4& worldToProj,
                                             const Vector3& cameraPosition) {
      const ImGuiViewport* viewport = ImGui::GetMainViewport();
      std::vector<Ds1LightEntry> entries;
      entries.reserve(snapshot.size());
      for (const auto& light : snapshot) {
        Ds1LightEntry entry;
        entry.light = &light;
        entry.provenance = ds1ProvenanceIndex(light.provenance);
        if (light.type == RtLightType::Distant) {
          entry.onScreen = true;
        } else {
          entry.distance = length(light.position - cameraPosition);
          entry.onScreen = transformToScreen(worldToProj, Vector2 { viewport->Size.x, viewport->Size.y }, light.position, entry.screen) &&
                           std::isfinite(entry.screen.x) && std::isfinite(entry.screen.y);
        }
        entries.push_back(entry);
      }
      std::sort(entries.begin(), entries.end(), [](const Ds1LightEntry& a, const Ds1LightEntry& b) { return a.distance < b.distance; });
      return entries;
    }

    std::string ds1LightName(const LightManager::Ds1LightSnapshotEntry& light) {
      return light.name.empty() ? std::string("Light ") + hashToString(light.hash) : light.name;
    }

    void ds1LightDetails(const Ds1LightEntry& entry) {
      const Ds1Provenance& provenance = kDs1Provenance[entry.provenance];
      const auto& light = *entry.light;
      ImGui::TextColored(ImColor(provenance.colour), "%s", ds1LightName(light).c_str());
      ImGui::Text("Source: %s, %s", light.kind.empty() ? "Remix" : light.kind.c_str(), provenance.name);
      ImGui::TextWrapped("%s", provenance.detail);
      if (!light.origin.empty()) {
        ImGui::TextWrapped("From: %s", light.origin.c_str());
      }
      if (light.type == RtLightType::Distant) {
        ImGui::Text("Direction: %.2f %.2f %.2f", light.direction.x, light.direction.y, light.direction.z);
      } else {
        ImGui::Text("Position: %.2f %.2f %.2f (%.1f m away)", light.position.x, light.position.y, light.position.z, entry.distance);
      }
      ImGui::Text("Radiance: %.3f %.3f %.3f", light.radiance.x, light.radiance.y, light.radiance.z);
      if (light.type == RtLightType::Sphere) {
        ImGui::Text("Type: Sphere, radius %.3f m", light.radius);
      } else {
        ImGui::Text("Type: %s", ds1LightType(light.type));
      }
      ImGui::Text("Hash: %s", hashToString(light.hash).c_str());
    }
  }

  void LightManager::ds1BuildSnapshot() {
    // On the CS thread, where rtx.ds1Lights.labels is applied, so it is read here and nowhere else.
    struct Label {
      char provenance = '?';
      std::string kind, name, origin;
    };
    static std::unordered_map<uint64_t, Label> s_labels;
    const std::string& text = DxvkDarkSoulsLightFalloff::labels();
    if (text != m_ds1LabelText) {
      m_ds1LabelText = text;
      s_labels.clear();
      size_t start = 0;
      while (start < text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) {
          end = text.size();
        }
        const std::string entry = text.substr(start, end - start);
        start = end + 1;
        std::string fields[5];
        size_t at = 0;
        int count = 0;
        while (count < 5) {
          const size_t bar = count < 4 ? entry.find('|', at) : std::string::npos;
          fields[count++] = entry.substr(at, bar == std::string::npos ? std::string::npos : bar - at);
          if (bar == std::string::npos) {
            break;
          }
          at = bar + 1;
        }
        if (count < 5 || fields[0].empty()) {
          continue;
        }
        Label label;
        label.provenance = fields[1].empty() ? '?' : fields[1][0];
        label.kind = std::move(fields[2]);
        label.name = std::move(fields[3]);
        label.origin = std::move(fields[4]);
        s_labels[std::strtoull(fields[0].c_str(), nullptr, 16)] = std::move(label);
      }
    }

    std::unordered_map<const RtLight*, uint64_t> apiHashes;
    for (const auto& [light, hash] : m_ds1ApiLights) {
      apiHashes[light] = hash;
    }
    std::vector<Ds1LightSnapshotEntry> snapshot;
    snapshot.reserve(m_linearizedLights.size());
    for (const RtLight* light : m_linearizedLights) {
      if (!light || light->getType() > RtLightType::Distant) {
        continue;
      }
      Ds1LightSnapshotEntry entry;
      entry.type = light->getType();
      entry.position = light->getPosition();
      entry.direction = light->getDirection();
      entry.radiance = light->getRadiance();
      entry.radius = entry.type == RtLightType::Sphere ? light->getSphereLight().getRadius() : 0.0f;
      if (const auto found = apiHashes.find(light); found != apiHashes.end()) {
        entry.hash = found->second;
        if (const auto label = s_labels.find(found->second); label != s_labels.end()) {
          entry.provenance = label->second.provenance;
          entry.kind = label->second.kind;
          entry.name = label->second.name;
          entry.origin = label->second.origin;
        }
      } else {
        entry.hash = light->getInitialHash();
      }
      snapshot.push_back(std::move(entry));
    }
    std::lock_guard<std::mutex> lock(m_ds1SnapshotMutex);
    m_ds1Snapshot = std::move(snapshot);
  }

  void LightManager::showDs1LightView() const {
    if (!Ds1LightViewSettings::showView()) {
      return;
    }
    m_ds1SnapshotWanted = true;
    std::vector<Ds1LightSnapshotEntry> snapshot;
    {
      std::lock_guard<std::mutex> lock(m_ds1SnapshotMutex);
      snapshot = m_ds1Snapshot;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.0f));
    if (ImGui::Begin("DS1 Light View", nullptr, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
      ImDrawList* drawList = ImGui::GetWindowDrawList();
      drawList->PushClipRectFullScreen();
      const RtCamera& camera = device()->getCommon()->getSceneManager().getCamera();
      const Matrix4 worldToProj = camera.getViewToProjection() * camera.getWorldToView(true);
      const Vector3 cameraRight = Vector3 { camera.getViewToWorld(true)[0].xyz() };
      const auto entries = ds1ViewLights(snapshot, worldToProj, camera.getPosition(true));
      const ImDrawListSharedData* font = ImGui::GetDrawListSharedData();
      // Hover only while Remix's UI (and its cursor) is up: in play the hidden cursor would raise tooltips.
      const bool canHover = RtxOptions::showUI() != UIType::None;
      const Ds1LightEntry* hovered = nullptr;
      // Far to near, so the near lights' marks and names lie on top.
      for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        const auto& entry = *it;
        const auto& light = *entry.light;
        if (light.type == RtLightType::Distant || !entry.onScreen || entry.distance > Ds1LightViewSettings::viewDistance() ||
            !s_ds1ShowProvenance[entry.provenance]) {
          continue;
        }
        const ImU32 colour = kDs1Provenance[entry.provenance].colour;
        if (light.radius > 0.0f) {
          // The emitter's size on screen.
          ImVec2 edge;
          transformToScreen(worldToProj, Vector2 { viewport->Size.x, viewport->Size.y }, light.position + cameraRight * light.radius, edge);
          const float dx = edge.x - entry.screen.x, dy = edge.y - entry.screen.y;
          const float radius = std::sqrt(dx * dx + dy * dy);
          if (std::isfinite(radius) && radius > 5.0f && radius < 4.0f * viewport->Size.x) {
            drawList->AddCircleFilled(entry.screen, radius, (colour & 0x00FFFFFFu) | IM_COL32(0, 0, 0, 60));
          }
        }
        drawList->AddCircleFilled(entry.screen, 4.0f, colour);
        drawList->AddCircle(entry.screen, 5.0f, IM_COL32(0, 0, 0, 220));
        if (light.hash == s_ds1Selected) {
          drawList->AddCircle(entry.screen, 14.0f, IM_COL32_WHITE, 0, 3.0f);
        }
        const float mx = entry.screen.x - ImGui::GetMousePos().x, my = entry.screen.y - ImGui::GetMousePos().y;
        if (canHover && mx * mx + my * my < 64.0f && (!hovered || entry.distance < hovered->distance)) {
          hovered = &entry;
        }
        if (Ds1LightViewSettings::showViewNames() && font && font->Font) {
          const std::string text = ds1LightName(light);
          const ImVec2 at { entry.screen.x + 8.0f, entry.screen.y - font->FontSize * 0.5f };
          const ImVec2 size = font->Font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, text.c_str());
          drawList->AddRectFilled(ImVec2 { at.x - 2.0f, at.y - 1.0f }, ImVec2 { at.x + size.x + 2.0f, at.y + size.y + 1.0f }, IM_COL32(0, 0, 0, 170));
          drawList->AddText(font->Font, font->FontSize, at, colour, text.c_str());
        }
      }
      if (hovered) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(420.0f);
        ds1LightDetails(*hovered);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
      }
      drawList->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleColor();
  }

  void LightManager::showDs1LightList() {
    if (!ImGui::CollapsingHeader("Lights")) {
      return;
    }
    m_ds1SnapshotWanted = true;
    std::vector<Ds1LightSnapshotEntry> snapshot;
    {
      std::lock_guard<std::mutex> lock(m_ds1SnapshotMutex);
      snapshot = m_ds1Snapshot;
    }
    ImGui::Indent();
    ImGui::TextWrapped("Every light Remix renders, by where its values come from. The colours mark the lights in the world.");
    RemixGui::Checkbox("Show Lights in the World", &Ds1LightViewSettings::showViewObject());
    RemixGui::Checkbox("Name Them", &Ds1LightViewSettings::showViewNamesObject());
    RemixGui::DragFloat("Up To (m)", &Ds1LightViewSettings::viewDistanceObject(), 1.0f, 1.0f, 1000.0f, "%.0f");
    RemixGui::Checkbox("Only Lights on Screen", &s_ds1OnlyOnScreen);

    const RtCamera& camera = device()->getCommon()->getSceneManager().getCamera();
    const Matrix4 worldToProj = camera.getViewToProjection() * camera.getWorldToView(true);
    const auto entries = ds1ViewLights(snapshot, worldToProj, camera.getPosition(true));

    int counts[kDs1ProvenanceCount] = {};
    for (const auto& entry : entries) {
      if (!s_ds1OnlyOnScreen || entry.onScreen) {
        ++counts[entry.provenance];
      }
    }
    for (int i = 0; i < kDs1ProvenanceCount; ++i) {
      ImGui::PushID(i);
      ImGui::Checkbox("##show", &s_ds1ShowProvenance[i]);
      ImGui::SameLine();
      ImGui::TextColored(ImColor(kDs1Provenance[i].colour), "%s (%d)", kDs1Provenance[i].name, counts[i]);
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", kDs1Provenance[i].detail);
      }
      ImGui::PopID();
    }

    const Ds1LightEntry* selected = nullptr;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("ds1_lights", 6, flags, ImVec2(0.0f, 320.0f))) {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn("Source");
      ImGui::TableSetupColumn("Kind");
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Dist (m)");
      ImGui::TableSetupColumn("Radiance (max)");
      ImGui::TableSetupColumn("Radius (m)");
      ImGui::TableHeadersRow();
      int row = 0;
      for (const auto& entry : entries) {
        if (!s_ds1ShowProvenance[entry.provenance] || (s_ds1OnlyOnScreen && !entry.onScreen)) {
          continue;
        }
        const auto& light = *entry.light;
        const Ds1Provenance& provenance = kDs1Provenance[entry.provenance];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushID(row++);
        if (ImGui::Selectable(provenance.name, light.hash == s_ds1Selected, ImGuiSelectableFlags_SpanAllColumns)) {
          s_ds1Selected = light.hash == s_ds1Selected ? 0 : light.hash;
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::PushTextWrapPos(420.0f);
          ds1LightDetails(entry);
          ImGui::PopTextWrapPos();
          ImGui::EndTooltip();
        }
        ImGui::PopID();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(light.kind.empty() ? "Remix" : light.kind.c_str());
        ImGui::TableNextColumn();
        ImGui::TextColored(ImColor(provenance.colour), "%s", ds1LightName(light).c_str());
        ImGui::TableNextColumn();
        if (light.type == RtLightType::Distant) {
          ImGui::TextUnformatted("-");
        } else {
          ImGui::Text("%.1f", entry.distance);
        }
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", std::max({ light.radiance.x, light.radiance.y, light.radiance.z }));
        ImGui::TableNextColumn();
        if (light.type == RtLightType::Sphere) {
          ImGui::Text("%.2f", light.radius);
        } else {
          ImGui::Text("- (%s)", ds1LightType(light.type));
        }
        if (light.hash == s_ds1Selected) {
          selected = &entry;
        }
      }
      ImGui::EndTable();
    }
    if (selected) {
      ImGui::Separator();
      ds1LightDetails(*selected);
    }
    ImGui::Unindent();
  }

}  // namespace dxvk
