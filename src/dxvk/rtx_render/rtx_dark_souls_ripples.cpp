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
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_HEIGHT_NEXT)
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
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_HEIGHT_NEXT)
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

    // A frame that moves the player more than this (metres) is a warp or a load, not wading.
    constexpr float kMaxFrameMove = 1.0f;
    // The player's velocity is smoothed over this time (seconds): the head sways as they walk.
    constexpr float kVelocitySmoothing = 0.1f;
    constexpr float kGravity = 9.81f;
    // The water rays start this far above the head and reach this far down.
    constexpr float kRayAboveHead = 1.0f;
    constexpr float kRayLength = 8.0f;
    // Water within this distance of a cell's water height takes its ripples.
    constexpr float kHeightTolerance = 0.3f;
    constexpr float kBorderCells = 16.0f;

    struct Field {
      uint32_t gridSize = 0;
      float cellSize = 0.0f;
      // heights[newest] holds the newest step, heights[(newest + 2) % 3] the step before it; a step writes
      // the third.
      Resources::Resource heights[3];
      uint32_t newest = 0;
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
      // The bow at the last step and now (Ds1RippleStepArgs).
      vec2 position0 { 0.0f, 0.0f };
      vec2 position1 { 0.0f, 0.0f };
      vec2 direction0 { 1.0f, 0.0f };
      vec2 direction1 { 1.0f, 0.0f };
      float bowHeight0 = 0.0f;
      float bowHeight1 = 0.0f;
      uint32_t textureIndex = BINDING_INDEX_INVALID;
      uint32_t samplerIndex = BINDING_INDEX_INVALID;
    };

    Field s_field;
    Frame s_frame;
    // Fixed-step clock: time not yet stepped.
    double s_lastTime = -1.0;
    double s_accumulator = 0.0;
    // The player's last position and smoothed velocity (x, z), and the bow as of the last step.
    vec2 s_lastHead { 0.0f, 0.0f };
    vec2 s_velocity { 0.0f, 0.0f };
    vec2 s_bowPosition { 0.0f, 0.0f };
    vec2 s_bowDirection { 1.0f, 0.0f };
    float s_bowHeight = 0.0f;

    vec2 normalized(vec2 v, vec2 fallback) {
      const float length = std::hypot(v.x, v.y);
      return length > 1e-4f ? vec2 { v.x / length, v.y / length } : fallback;
    }

    vec2 mix(vec2 a, vec2 b, float t) {
      return vec2 { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
    }

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
      s_field.heights[2] = Resources::createImageResource(dctx, "DS1 ripple height C", extent, VK_FORMAT_R32_SFLOAT);
      s_field.water = Resources::createImageResource(dctx, "DS1 ripple water", extent, VK_FORMAT_R32_SFLOAT);
      s_field.probe = Resources::createImageResource(dctx, "DS1 ripple probe", VkExtent3D { 1, 1, 1 }, VK_FORMAT_R32G32B32A32_SFLOAT);
      s_field.slope = Resources::createImageResource(dctx, "DS1 ripple slope", extent, VK_FORMAT_R16G16B16A16_SFLOAT);
      s_field.gridSize = gridSize;
      s_field.cellSize = cellSize;
      s_field.newest = 0;
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
    const vec2 position { head.x, head.z };
    const float frameTime = s_lastTime < 0.0 ? 0.0f : float(std::min(now - s_lastTime, 0.25));
    const bool warped = std::hypot(position.x - s_lastHead.x, position.y - s_lastHead.y) > kMaxFrameMove;
    if (s_lastTime < 0.0 || s_field.reset || warped) {
      s_accumulator = 0.0;
      s_velocity = vec2 { 0.0f, 0.0f };
      s_bowPosition = position;
      s_bowHeight = 0.0f;
    } else {
      s_accumulator += frameTime;
      if (frameTime > 0.0f) {
        const float follow = 1.0f - std::exp(-frameTime / kVelocitySmoothing);
        s_velocity.x += ((position.x - s_lastHead.x) / frameTime - s_velocity.x) * follow;
        s_velocity.y += ((position.y - s_lastHead.y) / frameTime - s_velocity.y) * follow;
      }
    }
    s_lastHead = position;
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
    // A leg moving at U through water lifts it ahead by at most the stagnation head U^2 / 2g; the waves it
    // sheds carry bowEfficiency of that.
    const float speed = std::hypot(s_velocity.x, s_velocity.y);
    s_frame.position0 = s_bowPosition;
    s_frame.direction0 = s_bowDirection;
    s_frame.bowHeight0 = s_bowHeight;
    s_frame.position1 = position;
    s_frame.direction1 = normalized(s_velocity, s_bowDirection);
    s_frame.bowHeight1 = std::min(std::max(bowEfficiency(), 0.0f) * speed * speed / (2.0f * kGravity),
                                  std::max(maxBowHeight(), 0.0f));
    if (steps > 0) {
      s_bowPosition = s_frame.position1;
      s_bowDirection = s_frame.direction1;
      s_bowHeight = s_frame.bowHeight1;
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
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT_NEXT, s_field.heights[2].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_WATER, s_field.water.view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_PROBE, s_field.probe.view, nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, Ds1RippleMapShader::getShader());
      ctx->dispatch(groups, groups, 1);
      s_field.reset = false;
    }

    const double stepTime = 1.0 / std::clamp(double(stepRate()), 10.0, 240.0);
    const double cell = s_field.cellSize;
    // Real gravity waves: a 12-cell wave runs at sqrt(g / k) when gravity * K12 = g dt^2 k_cell / cell, with
    // k_cell its wavenumber in cells and K12 the kernel's response there (fork_ds1_ripples.h).
    const double k12 = 2.0 * double(kPi) / 12.0;
    const double gravity = std::min(double(std::max(gravityScale(), 0.0f)) * kGravity * stepTime * stepTime / cell * k12 /
                                      kDs1RippleKernelResponse12,
                                    3.5 / kDs1RippleKernelResponseMax);
    // The explicit diffusion step stays stable below 0.25 on the 5-point Laplacian.
    const double viscosityStep = std::min(2.0 * std::max(double(viscosity()), 0.0) * stepTime / (cell * cell), 0.2);
    const float sigma = std::max(sourceRadius(), s_field.cellSize);

    for (uint32_t step = 0; step < s_frame.steps; ++step) {
      const float t0 = float(step) / float(s_frame.steps);
      const float t1 = float(step + 1) / float(s_frame.steps);
      Ds1RippleStepArgs args = {};
      args.origin = s_field.origin;
      args.gridSize = n;
      args.cellSize = s_field.cellSize;
      args.gravity = float(gravity);
      args.damping = float(std::max(damping(), 0.0f) * stepTime);
      args.viscosity = float(viscosityStep);
      args.borderCells = kBorderCells;
      args.position0 = mix(s_frame.position0, s_frame.position1, t0);
      args.position1 = mix(s_frame.position0, s_frame.position1, t1);
      args.direction0 = normalized(mix(s_frame.direction0, s_frame.direction1, t0), s_frame.direction1);
      args.direction1 = normalized(mix(s_frame.direction0, s_frame.direction1, t1), s_frame.direction1);
      args.bowHeight0 = s_frame.bowHeight0 + (s_frame.bowHeight1 - s_frame.bowHeight0) * t0;
      args.bowHeight1 = s_frame.bowHeight0 + (s_frame.bowHeight1 - s_frame.bowHeight0) * t1;
      args.legOffset = std::max(legRadius(), 0.0f);
      args.bowExponent = -1.0f / (2.0f * sigma * sigma);
      const uint32_t previous = (s_field.newest + 2) % 3;
      const uint32_t next = (s_field.newest + 1) % 3;
      ctx->pushConstants(0, sizeof(args), &args);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT, s_field.heights[s_field.newest].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT_PREV, s_field.heights[previous].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT_NEXT, s_field.heights[next].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_WATER, s_field.water.view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_PROBE, s_field.probe.view, nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, Ds1RippleStepShader::getShader());
      ctx->dispatch(groups, groups, 1);
      s_field.newest = next;
    }

    {
      Ds1RippleSlopeArgs args = {};
      args.origin = s_field.origin;
      args.gridSize = n;
      args.cellSize = s_field.cellSize;
      args.alpha = s_frame.alpha;
      args.referenceHeight = s_frame.head.y;
      ctx->pushConstants(0, sizeof(args), &args);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT, s_field.heights[s_field.newest].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT_PREV, s_field.heights[(s_field.newest + 2) % 3].view, nullptr);
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
