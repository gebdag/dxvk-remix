#include "rtx_dark_souls_light_falloff.h"
#include "rtx_lights.h"
#include "rtx_context.h"
#include "rtx_scene_manager.h"
#include "rtx_shader_manager.h"
#include "dxvk_scoped_annotation.h"
#include "rtx/pass/ds1_player_light_clearance.h"
#include "rtx_dark_souls_ripples.h"

#include <rtx_shaders/ds1_player_light_clearance.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace dxvk {

  namespace {
    struct Falloff {
      float begin;
      float end;
    };

    // The parsed table and the text it came from: the option changes when the proxy's set of
    // listed lights does, far less often than this runs.
    std::string s_parsedText;
    std::unordered_map<uint64_t, Falloff> s_table;

    // The player light this frame (by rtx.ds1Lights.playerLight), or null.
    std::string s_playerHashText;
    uint64_t s_playerHash = 0;
    RtLight* s_playerLight = nullptr;

    void parse(const std::string& text) {
      s_parsedText = text;
      s_table.clear();
      const char* p = text.c_str();
      while (*p) {
        char* next = nullptr;
        const uint64_t hash = std::strtoull(p, &next, 16);
        if (next == p || *next != ':') {
          break;
        }
        p = next + 1;
        const float begin = std::strtof(p, &next);
        if (next == p || *next != ':') {
          break;
        }
        p = next + 1;
        const float end = std::strtof(p, &next);
        if (next == p) {
          break;
        }
        p = next;
        if (end > begin && end > 0.0f) {
          s_table[hash] = Falloff { std::max(begin, 0.0f), end };
        }
        while (*p == ';' || *p == ' ') {
          ++p;
        }
      }
    }

    class Ds1PlayerLightClearanceShader : public ManagedShader {
      SHADER_SOURCE(Ds1PlayerLightClearanceShader, VK_SHADER_STAGE_COMPUTE_BIT, ds1_player_light_clearance)

      PUSH_CONSTANTS(Ds1PlayerLightClearanceArgs)

      BEGIN_PARAMETER()
        ACCELERATION_STRUCTURE(DS1_PLC_BINDING_TLAS)
        STRUCTURED_BUFFER(DS1_PLC_BINDING_SURFACES)
        RW_STRUCTURED_BUFFER(DS1_PLC_BINDING_LIGHTS)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(Ds1PlayerLightClearanceShader);
  }

  void DxvkDarkSoulsLightFalloff::beginFrame() {
    s_playerLight = nullptr;
    const std::string& text = playerLight();
    if (text != s_playerHashText) {
      s_playerHashText = text;
      s_playerHash = text.empty() ? 0 : std::strtoull(text.c_str(), nullptr, 16);
    }
  }

  void DxvkDarkSoulsLightFalloff::apply(uint64_t handle, RtLight& light) {
    if (s_playerHash != 0 && handle == s_playerHash && light.getType() == RtLightType::Sphere) {
      s_playerLight = &light;
    }
    const std::string& text = falloffTable();
    if (text != s_parsedText) {
      parse(text);
    }
    if (auto found = s_table.find(handle); found != s_table.end()) {
      light.setDs1Falloff(found->second.begin, found->second.end);
      return;
    }
    light.setDs1Falloff(0.0f, 0.0f);
  }

  void DxvkDarkSoulsLightFalloff::dispatchPlayerLightClearance(RtxContext* ctx) {
    if (!playerLightClearance() || s_playerLight == nullptr) {
      return;
    }
    // Written into the light buffer this frame (an unlit or dropped light keeps the invalid index).
    const uint32_t index = s_playerLight->getBufferIdx();
    LightManager& lights = ctx->getSceneManager().getLightManager();
    const Rc<DxvkBuffer> lightBuffer = lights.getLightBuffer();
    const Rc<DxvkBuffer> surfaceBuffer = ctx->getSceneManager().getSurfaceBuffer();
    const auto& tlas = ctx->getResourceManager().getTLAS(Tlas::Opaque).accelStructure;
    if (index == kNewLightIdx || index >= lights.getActiveCount() || lightBuffer == nullptr ||
        surfaceBuffer == nullptr || tlas.ptr() == nullptr) {
      return;
    }

    ScopedGpuProfileZone(ctx, "DS1 Player Light Clearance");

    Ds1PlayerLightClearanceArgs args = {};
    const Vector3 anchor = playerAnchor();
    args.anchor = { anchor.x, anchor.y, anchor.z };
    args.lightIndex = index;
    args.margin = std::max(playerLightClearanceMargin(), 0.0f);
    args.ignore = std::max(playerLightClearanceIgnore(), 0.0f);
    // The proxy's offset sliders reach 5 per axis.
    args.maxOffset = 9.0f;
    // The feet the proxy sends every frame (rtx.ds1Ripple.playerPosition); none yet: no body.
    const Vector3 feet = DxvkDarkSoulsRipples::playerPosition();
    args.bodyRadius = feet != Vector3(0.0f) ? std::max(playerLightBodyRadius(), 0.0f) : 0.0f;
    args.bodyFeet = { feet.x, feet.y, feet.z };
    args.bodyHeight = 1.8f;
    ctx->pushConstants(0, sizeof(args), &args);

    ctx->bindAccelerationStructure(DS1_PLC_BINDING_TLAS, tlas);
    ctx->bindResourceBuffer(DS1_PLC_BINDING_SURFACES, DxvkBufferSlice(surfaceBuffer, 0, surfaceBuffer->info().size));
    ctx->bindResourceBuffer(DS1_PLC_BINDING_LIGHTS, DxvkBufferSlice(lightBuffer, 0, lightBuffer->info().size));
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, Ds1PlayerLightClearanceShader::getShader());
    ctx->dispatch(1, 1, 1);
    // The light buffer is created for transfer writes only, so DXVK's own barrier after this write
    // does not cover the shader reads that follow (volumetrics, RTXDI, the integrators).
    ctx->emitMemoryBarrier(0,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_READ_BIT);
  }

} // namespace dxvk
