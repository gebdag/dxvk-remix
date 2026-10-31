#include "rtx_dark_souls_ripples.h"
#include "rtx_dark_souls_light_falloff.h"
#include "rtx_context.h"
#include "rtx_scene_manager.h"
#include "rtx_shader_manager.h"
#include "rtx_texture.h"
#include "dxvk_scoped_annotation.h"

#include <rtx_shaders/ds1_ripple_map.h>
#include <rtx_shaders/ds1_ripple_step.h>
#include <rtx_shaders/ds1_ripple_slope.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace dxvk {

  namespace {
    class Ds1RippleMapShader : public ManagedShader {
      SHADER_SOURCE(Ds1RippleMapShader, VK_SHADER_STAGE_COMPUTE_BIT, ds1_ripple_map)

      PUSH_CONSTANTS(Ds1RippleMapArgs)

      BEGIN_PARAMETER()
        ACCELERATION_STRUCTURE(DS1_RIPPLE_BINDING_TLAS)
        STRUCTURED_BUFFER(DS1_RIPPLE_BINDING_SURFACES)
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_HEIGHT)
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_HEIGHT_PREV)
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_WATER)
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_PROBE)
      END_PARAMETER()
    };

    class Ds1RippleStepShader : public ManagedShader {
      SHADER_SOURCE(Ds1RippleStepShader, VK_SHADER_STAGE_COMPUTE_BIT, ds1_ripple_step)

      PUSH_CONSTANTS(Ds1RippleStepArgs)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_HEIGHT)
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_HEIGHT_PREV)
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_WATER)
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_PROBE)
      END_PARAMETER()
    };

    class Ds1RippleSlopeShader : public ManagedShader {
      SHADER_SOURCE(Ds1RippleSlopeShader, VK_SHADER_STAGE_COMPUTE_BIT, ds1_ripple_slope)

      PUSH_CONSTANTS(Ds1RippleSlopeArgs)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_HEIGHT)
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_HEIGHT_PREV)
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_WATER)
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_SLOPE)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(Ds1RippleMapShader);
    PREWARM_SHADER_PIPELINE(Ds1RippleStepShader);
    PREWARM_SHADER_PIPELINE(Ds1RippleSlopeShader);

    // A step that moves the source more than this (metres) is a warp or a load, not wading.
    constexpr float kMaxSourceStep = 1.0f;
    // The water rays start this far above the head and reach this far down.
    constexpr float kRayAboveHead = 1.0f;
    constexpr float kRayLength = 8.0f;
    // Water within this distance of a cell's water height takes its ripples.
    constexpr float kHeightTolerance = 0.3f;
    constexpr float kBorderCells = 16.0f;

    struct Field {
      uint32_t gridSize = 0;
      float cellSize = 0.0f;
      // heights[current] holds the newest step, the other one the step before it.
      Resources::Resource heights[2];
      uint32_t current = 0;
      Resources::Resource water;
      Resources::Resource probe;
      Resources::Resource slope;
      bool reset = true;
      ivec2 origin { 0, 0 };
      ivec2 previousOrigin { 0, 0 };
    };

    // The frame's plan, made in prepare() and carried out by dispatch().
    struct Frame {
      bool active = false;
      uint32_t steps = 0;
      float alpha = 0.0f;
      Vector3 head { 0.0f, 0.0f, 0.0f };
      vec2 sourceFrom { 0.0f, 0.0f };
      vec2 sourceTo { 0.0f, 0.0f };
      uint32_t textureIndex = BINDING_INDEX_INVALID;
      uint32_t samplerIndex = BINDING_INDEX_INVALID;
    };

    Field s_field;
    Frame s_frame;
    // Fixed-step clock: time not yet stepped, and the source's position at the last step.
    double s_lastTime = -1.0;
    double s_accumulator = 0.0;
    vec2 s_lastSource { 0.0f, 0.0f };

    double nowSeconds() {
      using namespace std::chrono;
      return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    int floorDiv(float value, float cell) {
      return int(std::floor(value / cell));
    }

    void createField(RtxContext* ctx, uint32_t gridSize, float cellSize) {
      Rc<DxvkContext> dctx = ctx;
      const VkExtent3D extent { gridSize, gridSize, 1 };
      s_field.heights[0] = Resources::createImageResource(dctx, "DS1 ripple height A", extent, VK_FORMAT_R32_SFLOAT);
      s_field.heights[1] = Resources::createImageResource(dctx, "DS1 ripple height B", extent, VK_FORMAT_R32_SFLOAT);
      s_field.water = Resources::createImageResource(dctx, "DS1 ripple water", extent, VK_FORMAT_R32_SFLOAT);
      s_field.probe = Resources::createImageResource(dctx, "DS1 ripple probe", VkExtent3D { 1, 1, 1 }, VK_FORMAT_R32G32B32A32_SFLOAT);
      s_field.slope = Resources::createImageResource(dctx, "DS1 ripple slope", extent, VK_FORMAT_R16G16B16A16_SFLOAT);
      s_field.gridSize = gridSize;
      s_field.cellSize = cellSize;
      s_field.current = 0;
      s_field.reset = true;
    }
  }

  void DxvkDarkSoulsRipples::prepare(RtxContext* ctx) {
    s_frame = {};
    const Vector3 head = DxvkDarkSoulsLightFalloff::playerAnchor();
    if (!enable() || (head.x == 0.0f && head.y == 0.0f && head.z == 0.0f)) {
      s_lastTime = -1.0;
      return;
    }

    const uint32_t size = uint32_t(std::clamp(gridSize(), 64, 1024) / DS1_RIPPLE_GROUP_SIZE * DS1_RIPPLE_GROUP_SIZE);
    const float cell = std::clamp(cellSize(), 0.01f, 0.5f);
    if (size != s_field.gridSize || cell != s_field.cellSize || !s_field.slope.isValid()) {
      createField(ctx, size, cell);
    }

    // The window centred on the player, in whole cells; a jump of more than half a window starts over.
    const ivec2 origin { floorDiv(head.x, cell) - int(size / 2), floorDiv(head.z, cell) - int(size / 2) };
    s_field.previousOrigin = s_field.reset ? origin : s_field.origin;
    s_field.origin = origin;
    if (std::abs(origin.x - s_field.previousOrigin.x) > int(size / 2) || std::abs(origin.y - s_field.previousOrigin.y) > int(size / 2)) {
      s_field.reset = true;
    }

    // Fixed steps on the wall clock. A long frame (a load, a hitch) drops the time instead of catching up.
    const double stepTime = 1.0 / std::clamp(double(stepRate()), 10.0, 240.0);
    const double now = nowSeconds();
    const vec2 source { head.x, head.z };
    if (s_lastTime < 0.0 || s_field.reset) {
      s_accumulator = 0.0;
      s_lastSource = source;
    } else {
      s_accumulator += std::min(now - s_lastTime, 0.25);
    }
    s_lastTime = now;
    const uint32_t maxSteps = uint32_t(std::clamp(maxStepsPerFrame(), 1, 8));
    uint32_t steps = uint32_t(s_accumulator / stepTime);
    if (steps > maxSteps) {
      steps = maxSteps;
      s_accumulator = std::fmod(s_accumulator, stepTime);
    } else {
      s_accumulator -= steps * stepTime;
    }

    s_frame.active = true;
    s_frame.steps = steps;
    s_frame.alpha = float(std::clamp(s_accumulator / stepTime, 0.0, 1.0));
    s_frame.head = head;
    s_frame.sourceFrom = s_lastSource;
    s_frame.sourceTo = source;
    if (steps > 0) {
      s_lastSource = source;
    }

    SceneManager& scene = ctx->getSceneManager();
    scene.trackTexture(TextureRef(s_field.slope.view), s_frame.textureIndex, true, false);
    s_frame.samplerIndex = scene.trackSampler(
      ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT));
    if (s_frame.textureIndex == BINDING_INDEX_INVALID) {
      s_frame.active = false;
    }
  }

  void DxvkDarkSoulsRipples::fillShaderArgs(Ds1RippleArgs& args) {
    args = {};
    if (!s_frame.active) {
      return;
    }
    const float windowSize = float(s_field.gridSize) * s_field.cellSize;
    args.windowMin = vec2 { float(s_field.origin.x) * s_field.cellSize, float(s_field.origin.y) * s_field.cellSize };
    args.windowSize = windowSize;
    args.invWindowSize = 1.0f / windowSize;
    args.borderFade = std::max(borderFade(), 0.01f);
    args.normalScale = normalScale();
    args.referenceHeight = s_frame.head.y;
    args.heightTolerance = kHeightTolerance;
    args.textureIndex = s_frame.textureIndex;
    args.samplerIndex = s_frame.samplerIndex;
    args.enable = 1;
  }

  void DxvkDarkSoulsRipples::dispatch(RtxContext* ctx) {
    if (!s_frame.active) {
      return;
    }
    const auto& tlas = ctx->getResourceManager().getTLAS(Tlas::Opaque).accelStructure;
    const Rc<DxvkBuffer> surfaceBuffer = ctx->getSceneManager().getSurfaceBuffer();
    if (tlas.ptr() == nullptr || surfaceBuffer == nullptr) {
      return;
    }

    ScopedGpuProfileZone(ctx, "DS1 Ripples");

    // Last frame's ray tracing passes sampled the slope through the bindless table, which DXVK does not
    // track: they finish before it is written again.
    ctx->emitMemoryBarrier(0,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_READ_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);

    const uint32_t n = s_field.gridSize;
    const uint32_t groups = (n + DS1_RIPPLE_GROUP_SIZE - 1) / DS1_RIPPLE_GROUP_SIZE;

    {
      Ds1RippleMapArgs args = {};
      args.origin = s_field.origin;
      args.previousOrigin = s_field.previousOrigin;
      args.gridSize = n;
      args.cellSize = s_field.cellSize;
      args.resetAll = s_field.reset ? 1u : 0u;
      args.rayTop = s_frame.head.y + kRayAboveHead;
      args.probe = vec3 { s_frame.head.x, s_frame.head.y, s_frame.head.z };
      args.rayLength = kRayLength;
      ctx->pushConstants(0, sizeof(args), &args);
      ctx->bindAccelerationStructure(DS1_RIPPLE_BINDING_TLAS, tlas);
      ctx->bindResourceBuffer(DS1_RIPPLE_BINDING_SURFACES, DxvkBufferSlice(surfaceBuffer, 0, surfaceBuffer->info().size));
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT, s_field.heights[0].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT_PREV, s_field.heights[1].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_WATER, s_field.water.view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_PROBE, s_field.probe.view, nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, Ds1RippleMapShader::getShader());
      ctx->dispatch(groups, groups, 1);
      s_field.reset = false;
    }

    const double stepTime = 1.0 / std::clamp(double(stepRate()), 10.0, 240.0);
    // A 10-cell wavelength travels at waveSpeed: the leapfrog step turns a wave at angular frequency w
    // by 2 cos(w dt) = 2 - gravity * K(k), with K the kernel's response at that wavelength.
    const double k = 2.0 * double(kPi) / (10.0 * s_field.cellSize);
    const double omega = std::max(double(waveSpeed()), 0.0) * k;
    const double gravity = std::min(2.0 * (1.0 - std::cos(std::min(omega * stepTime, double(kPi)))) / kDs1RippleKernelResponse10,
                                    3.5 / kDs1RippleKernelResponseMax);
    const float sigma = std::max(sourceRadius(), s_field.cellSize);
    const vec2 from = s_frame.sourceFrom, to = s_frame.sourceTo;
    const bool warped = std::hypot(to.x - from.x, to.y - from.y) > kMaxSourceStep * float(std::max(s_frame.steps, 1u));

    for (uint32_t step = 0; step < s_frame.steps; ++step) {
      const float t0 = float(step) / float(s_frame.steps), t1 = float(step + 1) / float(s_frame.steps);
      Ds1RippleStepArgs args = {};
      args.origin = s_field.origin;
      args.gridSize = n;
      args.cellSize = s_field.cellSize;
      args.gravity = float(gravity);
      args.damping = float(std::max(damping(), 0.0f) * stepTime);
      args.borderCells = kBorderCells;
      args.sourceAmplitude = warped ? 0.0f : sourceHeight();
      args.sourceFrom = vec2 { from.x + (to.x - from.x) * t0, from.y + (to.y - from.y) * t0 };
      args.sourceTo = vec2 { from.x + (to.x - from.x) * t1, from.y + (to.y - from.y) * t1 };
      args.sourceExponent = -1.0f / (2.0f * sigma * sigma);
      ctx->pushConstants(0, sizeof(args), &args);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT, s_field.heights[s_field.current].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT_PREV, s_field.heights[1 - s_field.current].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_WATER, s_field.water.view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_PROBE, s_field.probe.view, nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, Ds1RippleStepShader::getShader());
      ctx->dispatch(groups, groups, 1);
      // The step wrote over the older heights: they are the newest now.
      s_field.current = 1 - s_field.current;
    }

    {
      Ds1RippleSlopeArgs args = {};
      args.origin = s_field.origin;
      args.gridSize = n;
      args.cellSize = s_field.cellSize;
      args.alpha = s_frame.alpha;
      args.referenceHeight = s_frame.head.y;
      ctx->pushConstants(0, sizeof(args), &args);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT, s_field.heights[s_field.current].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT_PREV, s_field.heights[1 - s_field.current].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_WATER, s_field.water.view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_SLOPE, s_field.slope.view, nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, Ds1RippleSlopeShader::getShader());
      ctx->dispatch(groups, groups, 1);
    }

    // The material reads the slope through the bindless table, which DXVK's own barriers do not cover.
    ctx->emitMemoryBarrier(0,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_READ_BIT);
  }

} // namespace dxvk
