#include "rtx_dark_souls_ripples.h"
#include "rtx_dark_souls_swamp.h"
#include "rtx_context.h"
#include "rtx_scene_manager.h"
#include "rtx_shader_manager.h"
#include "rtx_texture.h"
#include "rtx_particle_system.h"
#include "dxvk_scoped_annotation.h"

#include <rtx_shaders/ds1_ripple_map.h>
#include <rtx_shaders/ds1_ripple_step.h>
#include <rtx_shaders/ds1_ripple_slope.h>
#include <rtx_shaders/ds1_wet_level.h>
#include <rtx_shaders/ds1_splash_top.h>
#include <rtx_shaders/ds1_wet_decay.h>
#include <rtx_shaders/ds1_wet_splat.h>

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

    class Ds1WetLevelShader : public ManagedShader {
      SHADER_SOURCE(Ds1WetLevelShader, VK_SHADER_STAGE_COMPUTE_BIT, ds1_wet_level)

      PUSH_CONSTANTS(Ds1WetLevelArgs)

      BEGIN_PARAMETER()
        ACCELERATION_STRUCTURE(DS1_RIPPLE_BINDING_TLAS)
        STRUCTURED_BUFFER(DS1_RIPPLE_BINDING_SURFACES)
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_WATER)
        RW_TEXTURE2D(DS1_WET_BINDING_LEVEL)
      END_PARAMETER()
    };

    class Ds1SplashTopShader : public ManagedShader {
      SHADER_SOURCE(Ds1SplashTopShader, VK_SHADER_STAGE_COMPUTE_BIT, ds1_splash_top)

      PUSH_CONSTANTS(Ds1SplashTopArgs)

      BEGIN_PARAMETER()
        ACCELERATION_STRUCTURE(DS1_RIPPLE_BINDING_TLAS)
        STRUCTURED_BUFFER(DS1_RIPPLE_BINDING_SURFACES)
        RW_TEXTURE2D(DS1_WET_BINDING_BODY_TOP)
      END_PARAMETER()
    };

    class Ds1WetDecayShader : public ManagedShader {
      SHADER_SOURCE(Ds1WetDecayShader, VK_SHADER_STAGE_COMPUTE_BIT, ds1_wet_decay)

      PUSH_CONSTANTS(Ds1WetDecayArgs)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(DS1_WET_BINDING_GRID)
      END_PARAMETER()
    };

    class Ds1WetSplatShader : public ManagedShader {
      SHADER_SOURCE(Ds1WetSplatShader, VK_SHADER_STAGE_COMPUTE_BIT, ds1_wet_splat)

      PUSH_CONSTANTS(Ds1WetSplatArgs)

      BEGIN_PARAMETER()
        RW_TEXTURE2D(DS1_RIPPLE_BINDING_WATER)
        RW_TEXTURE2D(DS1_WET_BINDING_LEVEL)
        RW_TEXTURE2D(DS1_WET_BINDING_GRID)
        STRUCTURED_BUFFER(DS1_WET_BINDING_POSITIONS)
        STRUCTURED_BUFFER(DS1_WET_BINDING_COLOURS)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(Ds1WetLevelShader);
    PREWARM_SHADER_PIPELINE(Ds1SplashTopShader);
    PREWARM_SHADER_PIPELINE(Ds1WetDecayShader);
    PREWARM_SHADER_PIPELINE(Ds1WetSplatShader);

    constexpr float kGravity = 9.81f;
    // A leg or a robe's hem at the waterline is narrower than this (metres); a cell is inside a body when
    // rays this long along +-x and +-z all hit it.
    constexpr float kBodyReach = 0.3f;
    // The water rays start this far above the player's feet and reach this far down.
    constexpr float kRayAbovePlayer = 2.5f;
    constexpr float kRayLength = 8.0f;
    // The water depth at the player is looked for this far to each side (metres).
    constexpr float kWetReach = 0.3f;
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
      // The wet player: the body's grid (survives everything but a new field) and the water level at the feet.
      Resources::Resource wetGrid;
      Resources::Resource wetLevel;
      Resources::Resource bodyTop;
      Rc<DxvkBuffer> wetLevelReadback;
      bool wetReset = true;
      bool reset = true;
      ivec2 origin { 0, 0 };
      ivec2 previousOrigin { 0, 0 };
    };

    // The frame's plan, made in prepare() and carried out by dispatch().
    struct Frame {
      bool active = false;
      uint32_t steps = 0;
      float alpha = 0.0f;
      float dt = 0.0f;
      Vector3 player { 0.0f, 0.0f, 0.0f };
      uint32_t wetTextureIndex = BINDING_INDEX_INVALID;
      uint32_t wetLevelIndex = BINDING_INDEX_INVALID;
      uint32_t bodyTopIndex = BINDING_INDEX_INVALID;
      uint32_t textureIndex = BINDING_INDEX_INVALID;
      uint32_t samplerIndex = BINDING_INDEX_INVALID;
    };

    Field s_field;
    Frame s_frame;
    // Fixed-step clock: time not yet stepped.
    double s_lastTime = -1.0;
    double s_accumulator = 0.0;
    // Drips: when the player last touched water, read back from the GPU a few frames late, and whether they run.
    double s_lastInWater = -1e9;
    // The splash grid has been traced, around here, since the player went into falling water. The particles run before the ripple pass: they read the last frame's.
    bool s_bodyTopReady = false;
    vec2 s_bodyTopCentre = { 0.0f, 0.0f };
    // Whether that was mud: the drips are brown then.
    bool s_lastInMud = false;
    bool s_dripsRun = false;
    // The drip's round sprite and glossy material.
    Rc<DxvkImage> s_dripImage[2];
    Rc<DxvkImageView> s_dripView[2];

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
      const VkExtent3D wetExtent { DS1_WET_CELLS_X * DS1_WET_SLICES_PER_ROW, DS1_WET_CELLS_Y * (DS1_WET_CELLS_Z / DS1_WET_SLICES_PER_ROW), 1 };
      // 32-bit: a half float near 1 swallows a per-frame step below 2.4e-4, so mud (120 s) and lava (90 s) never wore off at 60 fps.
      s_field.wetGrid = Resources::createImageResource(dctx, "DS1 wet grid", wetExtent, VK_FORMAT_R32G32B32A32_SFLOAT);
      s_field.wetLevel = Resources::createImageResource(dctx, "DS1 wet level", VkExtent3D { 1, 1, 1 }, VK_FORMAT_R32G32_SFLOAT);
      s_field.bodyTop = Resources::createImageResource(dctx, "DS1 splash body top", VkExtent3D { DS1_SPLASH_TOP_SIZE, DS1_SPLASH_TOP_SIZE, 1 }, VK_FORMAT_R32_SFLOAT);
      DxvkBufferCreateInfo readback;
      readback.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      readback.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT;
      readback.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
      readback.size = 2 * sizeof(float);
      s_field.wetLevelReadback = ctx->getDevice()->createBuffer(readback, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                                                                DxvkMemoryStats::Category::RTXBuffer, "DS1 wet level readback");
      s_field.wetReset = true;
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
      s_dripsRun = false;
      return;
    }

    const uint32_t size = uint32_t(std::clamp(gridSize(), 64, 1024) / DS1_RIPPLE_GROUP_SIZE * DS1_RIPPLE_GROUP_SIZE);
    const float cell = std::clamp(cellSize(), 0.01f, 0.5f);
    if (size != s_field.gridSize || cell != s_field.cellSize || !s_field.slope.isValid()) {
      createField(ctx, size, cell);
      s_bodyTopReady = false;
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
    s_frame.dt = s_lastTime < 0.0 ? 0.0f : float(std::min(now - s_lastTime, 0.25));
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
    scene.trackTexture(TextureRef(s_field.wetGrid.view), s_frame.wetTextureIndex, true, false);
    scene.trackTexture(TextureRef(s_field.wetLevel.view), s_frame.wetLevelIndex, true, false);
    scene.trackTexture(TextureRef(s_field.bodyTop.view), s_frame.bodyTopIndex, true, false);
  }

  void DxvkDarkSoulsRipples::fillWetArgs(Ds1WetArgs& args) {
    args = {};
    if (!s_frame.active || !DxvkDarkSoulsWet::enable() || s_frame.wetTextureIndex == BINDING_INDEX_INVALID) {
      return;
    }
    args.playerXZ = vec2 { s_frame.player.x, s_frame.player.z };
    args.radiusSquared = DxvkDarkSoulsWet::radius() * DxvkDarkSoulsWet::radius();
    args.roughness = DxvkDarkSoulsWet::roughness();
    args.specularScale = DxvkDarkSoulsWet::specularScale();
    args.porousFactor = DxvkDarkSoulsWet::porousFactor();
    args.forceWetness = DxvkDarkSoulsWet::forceWetness();
    args.dropletScale = DxvkDarkSoulsWet::dropletScale();
    args.runnerScale = DxvkDarkSoulsWet::runnerScale();
    const Vector3 mud = DxvkDarkSoulsWet::mudColour();
    args.mudColour = vec3 { mud.x, mud.y, mud.z };
    args.forceMud = DxvkDarkSoulsWet::forceMud();
    const float mudTime = std::max(DxvkDarkSoulsWet::mudDryTime(), 1e-3f);
    args.mudHarden = std::clamp(DxvkDarkSoulsWet::mudHardenTime() / mudTime, 1e-3f, 1.0f);
    args.mudFade = std::clamp(DxvkDarkSoulsWet::mudFadeTime() / mudTime, 1e-3f, 1.0f);
    const float lavaTime = std::max(DxvkDarkSoulsWet::lavaTime(), 1e-3f);
    args.forceLava = DxvkDarkSoulsWet::forceLava();
    args.lavaMolten = std::clamp(DxvkDarkSoulsWet::lavaMoltenTime() / lavaTime, 1e-3f, 1.0f);
    args.lavaFade = std::clamp(DxvkDarkSoulsWet::lavaFadeTime() / lavaTime, 1e-3f, 1.0f);
    args.lavaGlow = std::max(DxvkDarkSoulsWet::lavaGlow(), 0.0f);
    args.textureIndex = s_frame.wetTextureIndex;
    args.samplerIndex = s_frame.samplerIndex;
    args.enable = 1;
  }

  void DxvkDarkSoulsRipples::fillShaderArgs(Ds1RippleArgs& args) {
    args = {};
    args.refractionFlatDepth = std::max(refractionFlatDepth(), 0.0f);
    args.refractionFullDepth = refractionFullDepth();
    args.smearFullDepth = std::max(smearFullDepth(), 0.001f);
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

  void DxvkDarkSoulsRipples::fillParticleArgs(ParticleSystemConstants& constants) {
    constants.ds1DripThreshold = 2.0f;
    const Vector3 contact = DxvkDarkSoulsWet::fallContact();
    constants.ds1FallContact = vec4 { contact.x, contact.y, contact.z, DxvkDarkSoulsWet::fallStrength() };
    constants.ds1SplashRadius = DxvkDarkSoulsWet::splashRadius();
    constants.ds1BodyTopIndex = BINDING_INDEX_INVALID;
    if (!s_frame.active || !DxvkDarkSoulsWet::enable() || s_frame.wetTextureIndex == BINDING_INDEX_INVALID || s_frame.wetLevelIndex == BINDING_INDEX_INVALID) {
      return;
    }
    constants.ds1WetGridIndex = s_frame.wetTextureIndex;
    constants.ds1WetLevelIndex = s_frame.wetLevelIndex;
    constants.ds1BodyTopIndex = s_frame.bodyTopIndex;
    constants.ds1BodyTopCentre = s_bodyTopCentre;
    constants.ds1WetSamplerIndex = s_frame.samplerIndex;
    constants.ds1DripThreshold = saturate(1.0f - DxvkDarkSoulsWet::dripTime() / std::max(DxvkDarkSoulsWet::dryTime(), 1e-3f));
  }

  void DxvkDarkSoulsRipples::emitDrips(DxvkContext* ctx, uint32_t instanceIdx) {
    const bool splashing = DxvkDarkSoulsWet::splashes() && DxvkDarkSoulsWet::fallStrength() > 0.0f && s_bodyTopReady;
    if (!s_dripsRun && !splashing) {
      return;
    }
    // Mud is dark, opaque and only dully shiny; water mostly shows what it reflects.
    const bool muddy = s_lastInMud && !splashing;
    const int look = muddy ? 1 : 0;
    // The sprite: a disc in the alpha of each mip, for the alpha test. Its colour is the drop's: a material with a texture ignores
    // its constant. As a texture stores colour: water a mid grey, enough to catch the light in the dark places waterfalls are; mud the swamp's own.
    static const uint32_t kSize = 32, kMips = 6;
    const size_t kHash = 0x4453314452495053ull + size_t(look);
    if (s_dripView[look] == nullptr) {
      const Vector3 stored = muddy ? DxvkDarkSoulsWet::mudColour() : Vector3(0.48f, 0.5f, 0.52f);
      const uint8_t red = uint8_t(saturate(stored.x) * 255.0f + 0.5f), green = uint8_t(saturate(stored.y) * 255.0f + 0.5f), blue = uint8_t(saturate(stored.z) * 255.0f + 0.5f);
      std::vector<uint8_t> pixels;
      for (uint32_t mip = 0; mip < kMips; ++mip) {
        const uint32_t size = kSize >> mip;
        for (uint32_t y = 0; y < size; ++y) {
          for (uint32_t x = 0; x < size; ++x) {
            const float u = (float(x) + 0.5f) / float(size) * 2.0f - 1.0f;
            const float v = (float(y) + 0.5f) / float(size) * 2.0f - 1.0f;
            const float alpha = saturate((1.0f - std::sqrt(u * u + v * v)) * float(size) * 0.5f + 0.5f);
            pixels.insert(pixels.end(), { red, green, blue, uint8_t(alpha * 255.0f + 0.5f) });
          }
        }
      }
      DxvkImageCreateInfo imageInfo = {};
      imageInfo.type = VK_IMAGE_TYPE_2D;
      imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
      imageInfo.sampleCount = VK_SAMPLE_COUNT_1_BIT;
      imageInfo.extent = { kSize, kSize, 1 };
      imageInfo.numLayers = 1;
      imageInfo.mipLevels = kMips;
      imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      imageInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
      imageInfo.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
      imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
      imageInfo.layout = VK_IMAGE_LAYOUT_UNDEFINED;
      s_dripImage[look] = ctx->getDevice()->createImage(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXMaterialTexture, "DS1 drip sprite");

      DxvkBufferCreateInfo stagingInfo = {};
      stagingInfo.size = pixels.size();
      stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      stagingInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT;
      stagingInfo.access = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
      Rc<DxvkBuffer> staging = ctx->getDevice()->createBuffer(stagingInfo, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, DxvkMemoryStats::Category::RTXBuffer, "DS1 drip sprite staging");
      std::memcpy(DxvkBufferSlice(staging).mapPtr(0), pixels.data(), pixels.size());

      DxvkImageViewCreateInfo viewInfo = {};
      viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = imageInfo.format;
      viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
      viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.numLevels = kMips;
      viewInfo.numLayers = 1;
      s_dripView[look] = ctx->getDevice()->createImageView(s_dripImage[look], viewInfo);

      ctx->changeImageLayout(s_dripImage[look], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      VkDeviceSize offset = 0;
      for (uint32_t mip = 0; mip < kMips; ++mip) {
        VkImageSubresourceLayers subresource = {};
        subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource.mipLevel = mip;
        subresource.layerCount = 1;
        const VkExtent3D extent = util::computeMipLevelExtent(imageInfo.extent, mip);
        ctx->copyBufferToImage(s_dripImage[look], subresource, VkOffset3D { 0, 0, 0 }, extent, staging, offset, 0, 0);
        offset += util::computeImageDataSize(imageInfo.format, extent);
      }
      ctx->changeImageLayout(s_dripImage[look], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      s_dripImage[look]->setHash(kHash);
    }

    OpaqueMaterialData opaque;
    opaque.setAlbedoOpacityTexture(TextureRef(s_dripView[look], kHash));
    opaque.setOpacityConstant(1.0f);
    opaque.setRoughnessConstant(muddy ? 0.45f : 0.1f);
    opaque.setMetallicConstant(0.0f);
    opaque.setUseLegacyAlphaState(false);
    opaque.setBlendEnabled(false);
    opaque.setAlphaTestType(AlphaTestType::kGreater);
    opaque.setAlphaTestReferenceValue(128);
    const MaterialData material { opaque };

    RtxParticleSystemDesc desc;
    desc.minTimeToLive = 0.5f;
    desc.maxTimeToLive = 0.9f;
    desc.maxNumParticles = 2048;
    desc.spawnRatePerSecond = std::max(DxvkDarkSoulsWet::dripRate(), 0.0f);
    desc.gravityForce = -981.0f;
    desc.initialVelocityFromMotion = 0.5f;
    desc.billboardType = FaceCamera_UpAxisLocked;
    desc.spriteSheetMode = UseMaterialSpriteSheet;
    desc.spriteSheetRows = 1;
    desc.spriteSheetCols = 1;
    desc.enableMotionTrail = 1;
    desc.motionTrailMultiplier = 0.4f;
    desc.enableCollisionDetection = 1;
    desc.collisionMode = Kill;
    desc.collisionThickness = 5.0f;
    desc.ds1Drip = 1;
    const float size = std::max(DxvkDarkSoulsWet::dripSize(), 0.01f);
    desc.minSize = { vec2(size * 0.7f, size * 0.7f), vec2(size * 0.5f, size * 0.5f) };
    desc.maxSize = { vec2(size * 1.3f, size * 1.3f), vec2(size, size) };
    desc.minColor = { vec4(1.0f, 1.0f, 1.0f, 1.0f) };
    desc.maxColor = { vec4(1.0f, 1.0f, 1.0f, 1.0f) };
    desc.minRotationSpeed = { 0.0f };
    desc.maxRotationSpeed = { 0.0f };
    desc.maxVelocity = { vec3(1500.0f, 1500.0f, 1500.0f) };

    static const DrawCallState s_state {};
    if (s_dripsRun) {
      RtxParticleSystemDesc drip = desc;
      if (muddy) {
        // Heavier, fewer and barely streaking.
        drip.spawnRatePerSecond *= 0.5f;
        drip.motionTrailMultiplier = 0.1f;
        drip.minSize = { vec2(size * 1.0f, size * 1.0f), vec2(size * 0.8f, size * 0.8f) };
        drip.maxSize = { vec2(size * 1.7f, size * 1.7f), vec2(size * 1.3f, size * 1.3f) };
      }
      ctx->getCommonObjects()->metaParticleSystem().spawnParticles(ctx, drip, instanceIdx, s_state, material);
    }
    if (splashing) {
      const float splash = std::max(DxvkDarkSoulsWet::splashSize(), 0.01f);
      RtxParticleSystemDesc bounce = desc;
      bounce.ds1Drip = 0;
      bounce.ds1Splash = 1;
      // One pool for the whole body. A rate at or above the pool's size means the first draw of a frame takes all of it (RtxParticleSystemManager::spawnParticles),
      // which is the helmet: no drops off anything else. Most candidates are refused, so the pool is large.
      bounce.maxNumParticles = 32768;
      bounce.spawnRatePerSecond = std::clamp(DxvkDarkSoulsWet::splashRate(), 0.0f, 32000.0f);
      bounce.minTimeToLive = 0.2f;
      bounce.maxTimeToLive = 0.55f;
      bounce.initialVelocityFromMotion = 0.0f;
      // They fly in every direction: along their own velocity, not upright like a falling drip.
      bounce.billboardType = FaceCamera_Spherical;
      bounce.alignParticlesToVelocity = 1;
      bounce.motionTrailMultiplier = 0.12f;
      bounce.minSize = { vec2(splash * 0.6f, splash * 0.6f), vec2(splash * 0.4f, splash * 0.4f) };
      bounce.maxSize = { vec2(splash * 1.3f, splash * 1.3f), vec2(splash, splash) };
      ctx->getCommonObjects()->metaParticleSystem().spawnParticles(ctx, bounce, instanceIdx, s_state, material);
    }
  }

  void DxvkDarkSoulsRipples::dispatch(RtxContext* ctx) {
    if (!s_frame.active) {
      s_bodyTopReady = false;
      return;
    }
    const auto& tlas = ctx->getResourceManager().getTLAS(Tlas::Opaque).accelStructure;
    const Rc<DxvkBuffer> surfaceBuffer = ctx->getSceneManager().getSurfaceBuffer();
    if (tlas.ptr() == nullptr || surfaceBuffer == nullptr) {
      s_bodyTopReady = false;
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
      args.swampIsLiquid = DxvkDarkSoulsSwamp::wade() ? 1u : 0u;
      args.collisionMin = collisionMin;
      const Vector3 test = testBody();
      if (test.x > 0.0f) {
        const float angle = float(std::fmod(nowSeconds() * double(test.z) / double(std::max(test.y, 0.01f)), 2.0 * double(kPi)));
        args.testBody = vec2 { s_frame.player.x + test.y * std::cos(angle), s_frame.player.z + test.y * std::sin(angle) };
        args.testBodyRadius = test.x;
      }
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

    if (DxvkDarkSoulsWet::enable()) {
      if (DxvkDarkSoulsWet::splashes() && DxvkDarkSoulsWet::fallStrength() > 0.0f) {
        const Vector3 contact = DxvkDarkSoulsWet::fallContact();
        Ds1SplashTopArgs args = {};
        args.centre = vec2 { contact.x, contact.z };
        args.radius = std::max(DxvkDarkSoulsWet::splashRadius(), 0.01f);
        // From just over any helmet down to the feet: map pieces can carry bones and count as skinned, so an arch overhead must stay out of it.
        args.top = s_frame.player.y + 2.2f;
        args.bottom = s_frame.player.y - 0.1f;
        ctx->pushConstants(0, sizeof(args), &args);
        ctx->bindAccelerationStructure(DS1_RIPPLE_BINDING_TLAS, tlas);
        ctx->bindResourceBuffer(DS1_RIPPLE_BINDING_SURFACES, DxvkBufferSlice(surfaceBuffer, 0, surfaceBuffer->info().size));
        ctx->bindResourceView(DS1_WET_BINDING_BODY_TOP, s_field.bodyTop.view, nullptr);
        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, Ds1SplashTopShader::getShader());
        ctx->dispatch(DS1_SPLASH_TOP_SIZE / 8, DS1_SPLASH_TOP_SIZE / 8, 1);
        s_bodyTopReady = true;
        s_bodyTopCentre = args.centre;
      } else {
        s_bodyTopReady = false;
      }
      {
        Ds1WetLevelArgs args = {};
        args.origin = s_field.origin;
        args.gridSize = n;
        args.cellSize = s_field.cellSize;
        args.reachCells = int(kWetReach / s_field.cellSize);
        args.feet = s_frame.player.y;
        args.maxDepth = DxvkDarkSoulsWet::maxDepth();
        args.swampIsLiquid = DxvkDarkSoulsSwamp::wade() ? 1u : 0u;
        ctx->pushConstants(0, sizeof(args), &args);
        ctx->bindAccelerationStructure(DS1_RIPPLE_BINDING_TLAS, tlas);
        ctx->bindResourceBuffer(DS1_RIPPLE_BINDING_SURFACES, DxvkBufferSlice(surfaceBuffer, 0, surfaceBuffer->info().size));
        ctx->bindResourceView(DS1_RIPPLE_BINDING_WATER, s_field.water.view, nullptr);
        ctx->bindResourceView(DS1_WET_BINDING_LEVEL, s_field.wetLevel.view, nullptr);
        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, Ds1WetLevelShader::getShader());
        ctx->dispatch(1, 1, 1);

        // Whether drips run, from the level as of a few frames ago: at most a moment late.
        const float* levelRead = reinterpret_cast<const float*>(s_field.wetLevelReadback->mapPtr(0));
        const float level = levelRead[0];
        const double now = nowSeconds();
        if (DxvkDarkSoulsWet::soak() || DxvkDarkSoulsWet::fallStrength() > 0.0f || DxvkDarkSoulsWet::testWaterHeight() > -99999.0f ||
            (level > DS1_RIPPLE_NO_WATER && levelRead[1] < 1.5f && level > s_frame.player.y - DxvkDarkSoulsWet::contactMargin())) {
          s_lastInWater = now;
          s_lastInMud = DxvkDarkSoulsWet::fallStrength() > 0.0f ? false : DxvkDarkSoulsWet::testWaterHeight() > -99999.0f ? DxvkDarkSoulsWet::testMud() : levelRead[1] > 0.5f;
        }
        s_dripsRun = DxvkDarkSoulsWet::drips() && now - s_lastInWater < double(DxvkDarkSoulsWet::dripTime());
        VkImageSubresourceLayers subresource = {};
        subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource.layerCount = 1;
        ctx->copyImageToBuffer(s_field.wetLevelReadback, 0, 0, 0, s_field.wetLevel.image, subresource, VkOffset3D { 0, 0, 0 }, VkExtent3D { 1, 1, 1 });
      }
      {
        Ds1WetDecayArgs args = {};
        args.decay = s_frame.dt / std::max(DxvkDarkSoulsWet::dryTime(), 1e-3f);
        args.reset = s_field.wetReset || DxvkDarkSoulsWet::clear() ? 1u : 0u;
        args.soak = DxvkDarkSoulsWet::soak() || DxvkDarkSoulsWet::fallStrength() > 0.0f ? 1u : 0u;
        args.mudDecay = s_frame.dt / std::max(DxvkDarkSoulsWet::mudDryTime(), 1e-3f);
        args.lavaDecay = s_frame.dt / std::max(DxvkDarkSoulsWet::lavaTime(), 1e-3f);
        ctx->pushConstants(0, sizeof(args), &args);
        ctx->bindResourceView(DS1_WET_BINDING_GRID, s_field.wetGrid.view, nullptr);
        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, Ds1WetDecayShader::getShader());
        const VkExtent3D gridExtent = s_field.wetGrid.image->mipLevelExtent(0);
        ctx->dispatch((gridExtent.width + DS1_WET_GROUP_SIZE - 1) / DS1_WET_GROUP_SIZE, (gridExtent.height + DS1_WET_GROUP_SIZE - 1) / DS1_WET_GROUP_SIZE, 1);
        s_field.wetReset = false;
      }
      const uint32_t frameId = ctx->getDevice()->getCurrentFrameId();
      for (const RtInstance* instance : ctx->getSceneManager().getInstanceManager().getInstanceTable()) {
        if (!instance->surface.isDs1WetBody || instance->getFrameLastUpdated() != frameId || instance->getBlas() == nullptr) {
          continue;
        }
        const RaytraceGeometry& geometry = instance->getBlas()->modifiedGeometryData;
        if (!geometry.positionBuffer.defined() || !geometry.color0Buffer.defined() || geometry.vertexCount == 0) {
          continue;
        }
        Ds1WetSplatArgs args = {};
        const Matrix4& objectToWorld = instance->surface.objectToWorld;
        for (uint32_t row = 0; row < 3; ++row) {
          args.objectToWorld[row] = vec4 { objectToWorld[0][row], objectToWorld[1][row], objectToWorld[2][row], objectToWorld[3][row] };
        }
        args.origin = s_field.origin;
        args.gridSize = n;
        args.cellSize = s_field.cellSize;
        args.positionWord = uint32_t((geometry.positionBuffer.offset() + geometry.positionBuffer.offsetFromSlice()) / 4);
        args.positionStride = geometry.positionBuffer.stride() / 4;
        args.colourWord = uint32_t((geometry.color0Buffer.offset() + geometry.color0Buffer.offsetFromSlice()) / 4);
        args.colourStride = geometry.color0Buffer.stride() / 4;
        args.vertexCount = geometry.vertexCount;
        args.contactMargin = DxvkDarkSoulsWet::contactMargin();
        args.radiusSquared = DxvkDarkSoulsWet::radius() * DxvkDarkSoulsWet::radius();
        args.testLevel = DxvkDarkSoulsWet::testWaterHeight();
        args.playerXZ = vec2 { s_frame.player.x, s_frame.player.z };
        args.useTestLevel = args.testLevel > -99999.0f ? 1u : 0u;
        args.testMud = DxvkDarkSoulsWet::testLava() ? 2u : DxvkDarkSoulsWet::testMud() ? 1u : 0u;
        args.lavaContact = DxvkDarkSoulsWet::lavaContact();
        ctx->pushConstants(0, sizeof(args), &args);
        ctx->bindResourceView(DS1_RIPPLE_BINDING_WATER, s_field.water.view, nullptr);
        ctx->bindResourceView(DS1_WET_BINDING_LEVEL, s_field.wetLevel.view, nullptr);
        ctx->bindResourceView(DS1_WET_BINDING_GRID, s_field.wetGrid.view, nullptr);
        ctx->bindResourceBuffer(DS1_WET_BINDING_POSITIONS, DxvkBufferSlice(geometry.positionBuffer.buffer(), 0, geometry.positionBuffer.buffer()->info().size));
        ctx->bindResourceBuffer(DS1_WET_BINDING_COLOURS, DxvkBufferSlice(geometry.color0Buffer.buffer(), 0, geometry.color0Buffer.buffer()->info().size));
        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, Ds1WetSplatShader::getShader());
        ctx->dispatch((geometry.vertexCount + 63) / 64, 1, 1);
      }
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
      // On the 5-point Laplacian a thick liquid's strong damping leaves the viscosity's limit unstable: the shortest waves
      // would be multiplied by (1 - d) / (1 + d) - 8 viscosity each step, below -1 at 0.2. Its damping does that job anyway.
      args.viscosity = thickDamping() > 0.0f ? 0.0f : float(viscosityStep);
      args.thickDamping = float(std::max(thickDamping(), 0.0f) * stepTime);
      args.thickSettle = float(std::clamp(double(thickSettle()) * stepTime, 0.0, 1.0));
      args.smoothing = std::clamp(smoothing(), 0.0f, 0.2f);
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
