#include "rtx_dark_souls_ripples.h"
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
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_OCCUPANCY)
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_OCCUPANCY_PREV)
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
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_OCCUPANCY)
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_OCCUPANCY_PREV)
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

    constexpr float kGravity = 9.81f;
    // A leg or a robe's hem at the waterline is narrower than this (metres); a cell is inside a body when
    // rays this long along +-x and +-z all hit it.
    constexpr float kBodyReach = 0.3f;
    // The water rays start this far above the player's feet and reach this far down.
    constexpr float kRayAbovePlayer = 2.5f;
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
      // occupancy[current] holds the bodies at the waterline this frame, the other one as of the last step.
      Resources::Resource occupancy[2];
      uint32_t current = 0;
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
      Vector3 player { 0.0f, 0.0f, 0.0f };
      uint32_t textureIndex = BINDING_INDEX_INVALID;
      uint32_t samplerIndex = BINDING_INDEX_INVALID;
    };

    Field s_field;
    Frame s_frame;
    // Fixed-step clock: time not yet stepped.
    double s_lastTime = -1.0;
    double s_accumulator = 0.0;

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
      s_field.occupancy[0] = Resources::createImageResource(dctx, "DS1 ripple occupancy A", extent, VK_FORMAT_R32_SFLOAT);
      s_field.occupancy[1] = Resources::createImageResource(dctx, "DS1 ripple occupancy B", extent, VK_FORMAT_R32_SFLOAT);
      s_field.slope = Resources::createImageResource(dctx, "DS1 ripple slope", extent, VK_FORMAT_R16G16B16A16_SFLOAT);
      s_field.gridSize = gridSize;
      s_field.cellSize = cellSize;
      s_field.newest = 0;
      s_field.current = 0;
      s_field.reset = true;
    }
  }

  void DxvkDarkSoulsRipples::prepare(RtxContext* ctx) {
    s_frame = {};
    const Vector3 player = playerPosition();
    if (!enable() || (player.x == 0.0f && player.y == 0.0f && player.z == 0.0f)) {
      s_lastTime = -1.0;
      return;
    }

    const uint32_t size = uint32_t(std::clamp(gridSize(), 64, 1024) / DS1_RIPPLE_GROUP_SIZE * DS1_RIPPLE_GROUP_SIZE);
    const float cell = std::clamp(cellSize(), 0.01f, 0.5f);
    if (size != s_field.gridSize || cell != s_field.cellSize || !s_field.slope.isValid()) {
      createField(ctx, size, cell);
    }

    // The window centred on the player, in whole cells; a jump of more than half a window starts over.
    const ivec2 origin { floorDiv(player.x, cell) - int(size / 2), floorDiv(player.z, cell) - int(size / 2) };
    s_field.previousOrigin = s_field.reset ? origin : s_field.origin;
    s_field.origin = origin;
    if (std::abs(origin.x - s_field.previousOrigin.x) > int(size / 2) || std::abs(origin.y - s_field.previousOrigin.y) > int(size / 2)) {
      s_field.reset = true;
    }

    // Fixed steps on the wall clock. A long frame (a load, a hitch) drops the time instead of catching up.
    const double stepTime = 1.0 / std::clamp(double(stepRate()), 10.0, 240.0);
    const double now = nowSeconds();
    if (s_lastTime < 0.0 || s_field.reset) {
      s_accumulator = 0.0;
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
    s_frame.player = player;

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
    args.referenceHeight = s_frame.player.y;
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
    // The bodies are looked for in a square centred on the player, inside the window.
    const uint32_t collisionCells = std::min(uint32_t(std::max(collisionSize(), 0.0f) / s_field.cellSize), n);
    const ivec2 collisionMin { int(n - collisionCells) / 2, int(n - collisionCells) / 2 };

    {
      Ds1RippleMapArgs args = {};
      args.origin = s_field.origin;
      args.previousOrigin = s_field.previousOrigin;
      args.gridSize = n;
      args.cellSize = s_field.cellSize;
      args.resetAll = s_field.reset ? 1u : 0u;
      args.rayTop = s_frame.player.y + kRayAbovePlayer;
      args.rayLength = kRayLength;
      args.bodyReach = kBodyReach;
      args.collisionSize = collisionCells;
      args.collisionMin = collisionMin;
      ctx->pushConstants(0, sizeof(args), &args);
      ctx->bindAccelerationStructure(DS1_RIPPLE_BINDING_TLAS, tlas);
      ctx->bindResourceBuffer(DS1_RIPPLE_BINDING_SURFACES, DxvkBufferSlice(surfaceBuffer, 0, surfaceBuffer->info().size));
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT, s_field.heights[0].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT_PREV, s_field.heights[1].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT_NEXT, s_field.heights[2].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_WATER, s_field.water.view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_OCCUPANCY, s_field.occupancy[s_field.current].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_OCCUPANCY_PREV, s_field.occupancy[1 - s_field.current].view, nullptr);
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

    for (uint32_t step = 0; step < s_frame.steps; ++step) {
      Ds1RippleStepArgs args = {};
      args.origin = s_field.origin;
      args.gridSize = n;
      args.cellSize = s_field.cellSize;
      args.gravity = float(gravity);
      args.damping = float(std::max(damping(), 0.0f) * stepTime);
      args.viscosity = float(viscosityStep);
      args.borderCells = kBorderCells;
      args.collisionMin = collisionMin;
      args.collisionSize = collisionCells;
      // The bodies' movement since the last step, shared out over this frame's steps.
      args.displacement = displacement() / float(s_frame.steps);
      const uint32_t previous = (s_field.newest + 2) % 3;
      const uint32_t next = (s_field.newest + 1) % 3;
      ctx->pushConstants(0, sizeof(args), &args);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT, s_field.heights[s_field.newest].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT_PREV, s_field.heights[previous].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_HEIGHT_NEXT, s_field.heights[next].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_WATER, s_field.water.view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_OCCUPANCY, s_field.occupancy[s_field.current].view, nullptr);
      ctx->bindResourceView(DS1_RIPPLE_BINDING_OCCUPANCY_PREV, s_field.occupancy[1 - s_field.current].view, nullptr);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, Ds1RippleStepShader::getShader());
      ctx->dispatch(groups, groups, 1);
      s_field.newest = next;
    }
    // This frame's bodies are what the next steps measure movement from; a frame without steps keeps the
    // last step's, so no movement is lost between steps.
    if (s_frame.steps > 0) {
      s_field.current = 1 - s_field.current;
    }

    {
      Ds1RippleSlopeArgs args = {};
      args.origin = s_field.origin;
      args.gridSize = n;
      args.cellSize = s_field.cellSize;
      args.alpha = s_frame.alpha;
      args.referenceHeight = s_frame.player.y;
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
