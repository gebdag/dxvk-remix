#include "rtx_dark_souls_effect_particles.h"
#include "rtx_dark_souls_ripples.h"
#include "rtx_dark_souls_swamp.h"
#include "rtx_dark_souls_skin.h"
#include "rtx_dark_souls_light_falloff.h"

#include "rtx_imgui.h"
#include "rtx_particle_system.h"
#include "rtx_materials.h"
#include "rtx_types.h"
#include "../dxvk_context.h"
#include "../../util/util_global_time.h"
#include "../imgui/imgui.h"

namespace dxvk {

  namespace {
    // A soft round dot, white, in each mip: the particle's texture. Its colour is the particle's, from the game's
    // vertex colour, so one texture serves both kinds.
    Rc<DxvkImage> s_dotImage;
    Rc<DxvkImageView> s_dotView;
    constexpr size_t kDotHash = 0x44533146584454ull; // "DS1FXDT"

    void createDot(DxvkContext* ctx) {
      static const uint32_t kSize = 32, kMips = 6;
      std::vector<uint8_t> pixels;
      for (uint32_t mip = 0; mip < kMips; ++mip) {
        const uint32_t size = kSize >> mip;
        for (uint32_t y = 0; y < size; ++y) {
          for (uint32_t x = 0; x < size; ++x) {
            const float u = (float(x) + 0.5f) / float(size) * 2.0f - 1.0f;
            const float v = (float(y) + 0.5f) / float(size) * 2.0f - 1.0f;
            // A bright core with a soft rim, like the game's dots.
            const float r = std::sqrt(u * u + v * v);
            const float alpha = saturate(1.0f - r) * saturate(1.0f - r) * 1.6f;
            pixels.insert(pixels.end(), { 255, 255, 255, uint8_t(saturate(alpha) * 255.0f + 0.5f) });
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
      s_dotImage = ctx->getDevice()->createImage(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXMaterialTexture, "DS1 effect particle dot");

      DxvkBufferCreateInfo stagingInfo = {};
      stagingInfo.size = pixels.size();
      stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      stagingInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT;
      stagingInfo.access = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
      Rc<DxvkBuffer> staging = ctx->getDevice()->createBuffer(stagingInfo, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, DxvkMemoryStats::Category::RTXBuffer, "DS1 effect particle dot staging");
      std::memcpy(DxvkBufferSlice(staging).mapPtr(0), pixels.data(), pixels.size());

      DxvkImageViewCreateInfo viewInfo = {};
      viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = imageInfo.format;
      viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
      viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.numLevels = kMips;
      viewInfo.numLayers = 1;
      s_dotView = ctx->getDevice()->createImageView(s_dotImage, viewInfo);

      ctx->changeImageLayout(s_dotImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      VkDeviceSize offset = 0;
      for (uint32_t mip = 0; mip < kMips; ++mip) {
        VkImageSubresourceLayers subresource = {};
        subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource.mipLevel = mip;
        subresource.layerCount = 1;
        const VkExtent3D extent = util::computeMipLevelExtent(imageInfo.extent, mip);
        ctx->copyBufferToImage(s_dotImage, subresource, VkOffset3D { 0, 0, 0 }, extent, staging, offset, 0, 0);
        offset += util::computeImageDataSize(imageInfo.format, extent);
      }
      ctx->changeImageLayout(s_dotImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      s_dotImage->setHash(kDotHash);
    }
  }

  bool DxvkDarkSoulsEffectParticles::emit(DxvkContext* ctx, uint32_t instanceIdx, const DrawCallState& drawCallState, const MaterialData& renderMaterialData) {
    const uint8_t kind = drawCallState.getMaterialData().ds1EffectParticles;
    // The snow's footprint textures (rtx_dark_souls_swamp.h): this draw only carries one.
    if (kind == 4 || kind == 5) {
      if (renderMaterialData.getType() == MaterialDataType::Opaque) {
        MaterialData carried(renderMaterialData);
        DxvkDarkSoulsSwamp::setStampTexture(kind - 4, carried.getOpaqueMaterialData().getAlbedoOpacityTexture());
      }
      return true;
    }
    if ((kind == 1 && !motes()) || (kind == 3 && !sparks()) || kind == 0 || kind > 3) {
      return false;
    }
    // The game's streaks only hide. The emitter is never shown, and with the sparks off spawns nothing.
    if (kind == 3 || (kind == 2 && !sparks())) {
      return true;
    }
    // The particles take the draw's own material (self-lit, its blend), with the dot for its texture.
    if (renderMaterialData.getType() != MaterialDataType::Opaque) {
      return false;
    }
    if (s_dotView == nullptr) {
      createDot(ctx);
    }
    MaterialData material(renderMaterialData);
    material.getOpaqueMaterialData().setAlbedoOpacityTexture(TextureRef(s_dotView, kDotHash));

    // The particles' draws must not be emitters themselves.
    DrawCallState state(drawCallState);
    state.modifyMaterialData().ds1EffectParticles = 0;

    RtxParticleSystemDesc desc;
    desc.spriteSheetMode = UseMaterialSpriteSheet;
    desc.spriteSheetRows = 1;
    desc.spriteSheetCols = 1;
    desc.enableCollisionDetection = 1;
    desc.collisionMode = Bounce;
    desc.minRotationSpeed = { 0.0f };
    desc.maxRotationSpeed = { 0.0f };
    desc.maxVelocity = { vec3(2000.0f, 2000.0f, 2000.0f) };

    if (kind == 1) {
      const float life = std::max(moteLife(), 0.1f);
      const float size = std::max(moteSize(), 0.1f); // cm, as the particle system takes sizes
      const float bright = std::max(moteBrightness(), 0.0f);
      desc.ds1Motes = 1;
      desc.maxNumParticles = 32768;
      // Below the pool's size, or the first draw of a frame would take all of it (RtxParticleSystemManager::spawnParticles).
      desc.spawnRatePerSecond = std::clamp(moteRate(), 0.0f, 30000.0f);
      desc.minTimeToLive = life * 0.6f;
      desc.maxTimeToLive = life * 1.4f;
      // initialVelocityFromNormal carries the drift speed (cm/s) for the motes' own spawn (ds1Motes).
      desc.initialVelocityFromNormal = std::max(moteDrift(), 0.0f);
      desc.useTurbulence = moteTurbulence() > 0.0f ? 1 : 0;
      desc.turbulenceForce = std::max(moteTurbulence(), 0.0f);
      // Per cm, scaled by sceneScale: 150 is swirls of about 70 cm, gentle against the rise.
      desc.turbulenceFrequency = 150.0f;
      desc.dragCoefficient = 0.4f;
      desc.gravityForce = 0.0f;
      // No collision: Remix particles collide with the G-buffer, what the screen shows at their position, not the world. The
      // motes are pushed by the player's body (fillParticleArgs, in the world); against the screen they were pushed and stopped by
      // whatever passed in front of them, a corner or the camera's own movement, so they drifted with the camera and a stain seen
      // round a corner filled in slowly.
      desc.enableCollisionDetection = 0;
      desc.billboardType = FaceCamera_Spherical;
      // Quickly in, full while rising, out over the last 40%: the game's cards fade as they climb.
      desc.minColor = { vec4(bright, bright, bright, 0.0f), vec4(bright, bright, bright, 1.0f), vec4(bright, bright, bright, 1.0f),
                        vec4(bright, bright, bright, 1.0f), vec4(bright, bright, bright, 0.6f), vec4(bright, bright, bright, 0.0f) };
      desc.maxColor = desc.minColor;
      desc.minSize = { vec2(size * 0.6f, size * 0.6f) };
      desc.maxSize = { vec2(size * 1.3f, size * 1.3f) };
    } else {
      const float life = std::max(sparkLife(), 0.05f);
      const float size = std::max(sparkSize(), 0.1f); // cm
      const float bright = std::max(sparkBrightness(), 0.0f);
      desc.ds1Sparks = 1;
      desc.maxNumParticles = 8192;
      // A hit's sparks all spawn when the game makes the hit: the rate carries sparks per streak, which the spawn count takes once per
      // call (RtxParticleSystemManager::getNumberOfParticlesToSpawn, ds1Sparks). Constant, so every hit shares one system.
      desc.spawnRatePerSecond = std::clamp(sparkCount(), 0.0f, 20.0f);
      desc.minTimeToLive = life * 0.5f;
      desc.maxTimeToLive = life * 1.5f;
      // initialVelocityFromNormal carries the scale on the speeds of the game's effects for the sparks' own spawn (ds1Sparks).
      desc.initialVelocityFromNormal = std::max(sparkSpeed(), 0.0f);
      desc.gravityForce = -981.0f;
      desc.dragCoefficient = 0.4f;
      desc.collisionThickness = 6.0f;
      desc.collisionRestitution = std::clamp(sparkBounce(), 0.0f, 1.0f);
      desc.billboardType = FaceCamera_Spherical;
      desc.alignParticlesToVelocity = 1;
      desc.enableMotionTrail = sparkTrail() > 0.0f ? 1 : 0;
      desc.motionTrailMultiplier = std::max(sparkTrail(), 0.0f);
      // Hot, then cooling to red as it fades.
      desc.minColor = { vec4(bright, bright, bright, 1.0f), vec4(bright, bright * 0.85f, bright * 0.7f, 1.0f), vec4(bright, bright * 0.45f, bright * 0.25f, 0.0f) };
      desc.maxColor = desc.minColor;
      desc.minSize = { vec2(size * 0.7f, size * 0.7f), vec2(size * 0.5f, size * 0.5f) };
      desc.maxSize = { vec2(size * 1.2f, size * 1.2f), vec2(size * 0.8f, size * 0.8f) };
    }

    ctx->getCommonObjects()->metaParticleSystem().spawnParticles(ctx, desc, instanceIdx, state, material);
    return true;
  }

  void DxvkDarkSoulsEffectParticles::fillParticleArgs(ParticleSystemConstants& constants) {
    // The player's velocity from rtx.ds1Ripple.playerPosition, which the proxy sends while the player moves: the step over the
    // time since the last change, eased; none after 0.15 s without a change, and none across a jump (a warp).
    static Vector3 s_lastPosition(0.0f);
    static uint64_t s_lastChangeMs = 0;
    static Vector3 s_velocity(0.0f);
    const Vector3 position = DxvkDarkSoulsRipples::playerPosition();
    const uint64_t nowMs = GlobalTime::get().absoluteTimeMs();
    if (position != s_lastPosition) {
      const float secs = std::max(float(nowMs - s_lastChangeMs) * 0.001f, 1e-3f);
      const Vector3 step = (position - s_lastPosition) / secs;
      // A step over a long gap (no frames ran this, or the player stood) is no speed: start from rest.
      const bool fresh = s_lastChangeMs != 0 && nowMs - s_lastChangeMs <= 500;
      s_velocity = fresh && length(step) < 15.0f ? s_velocity * 0.5f + step * 0.5f : Vector3(0.0f);
      s_lastPosition = position;
      s_lastChangeMs = nowMs;
    } else if (nowMs - s_lastChangeMs > 150) {
      s_velocity = Vector3(0.0f);
    }
    const bool push = motes() && motePush() > 0.0f && position != Vector3(0.0f);
    const float cm = 0.01f; // the options are cm; the world is in metres (DS1)
    constants.ds1PlayerPosition = vec4 { position.x, position.y, position.z, push ? std::max(motePushRadius(), 1.0f) * cm : 0.0f };
    constants.ds1PlayerVelocity = vec4 { s_velocity.x, s_velocity.y, s_velocity.z, std::max(motePush(), 0.0f) };
    constants.ds1MoteShape = vec4 { std::clamp(moteCore(), 0.5f, 16.0f), std::max(moteReach(), 0.0f) * cm, 1.8f, 0.0f };
  }

  void DxvkDarkSoulsSkin::showImguiSettings() {
    if (!ImGui::CollapsingHeader("Characters")) {
      return;
    }
    ImGui::Indent();
    RemixGui::Checkbox("Shadow Terminator for Skinned Meshes", &skinnedShadowTerminatorObject());
    RemixGui::Checkbox("Player Light Wall Clearance", &DxvkDarkSoulsLightFalloff::playerLightClearanceObject());
    RemixGui::DragFloat("Player Light Body Clearance (m)", &DxvkDarkSoulsLightFalloff::playerLightBodyRadiusObject(), 0.01f, 0.0f, 1.0f, "%.2f");
    RemixGui::Checkbox("Skin Scattering (experimental)", &enableObject());
    ImGui::Unindent();
  }

  void DxvkDarkSoulsEffectParticles::showImguiSettings() {
    if (!ImGui::CollapsingHeader("Effect particles (motes, sparks)")) {
      return;
    }
    ImGui::Indent();
    RemixGui::Checkbox("Bloodstain Motes", &motesObject());
    RemixGui::DragFloat("Mote Rate", &moteRateObject(), 50.0f, 0.0f, 30000.0f, "%.0f");
    RemixGui::DragFloat("Mote Size (cm)", &moteSizeObject(), 0.05f, 0.1f, 10.0f, "%.2f");
    RemixGui::DragFloat("Mote Life (s)", &moteLifeObject(), 0.05f, 0.1f, 10.0f, "%.2f");
    RemixGui::DragFloat("Mote Drift (cm/s)", &moteDriftObject(), 0.5f, 0.0f, 200.0f, "%.1f");
    RemixGui::DragFloat("Mote Turbulence", &moteTurbulenceObject(), 0.5f, 0.0f, 500.0f, "%.1f");
    RemixGui::DragFloat("Mote Core", &moteCoreObject(), 0.1f, 0.5f, 16.0f, "%.1f");
    RemixGui::DragFloat("Mote Reach (cm)", &moteReachObject(), 1.0f, 0.0f, 300.0f, "%.0f");
    RemixGui::DragFloat("Mote Push (m/s)", &motePushObject(), 0.05f, 0.0f, 10.0f, "%.2f");
    RemixGui::DragFloat("Mote Push Radius (cm)", &motePushRadiusObject(), 1.0f, 5.0f, 150.0f, "%.0f");
    RemixGui::DragFloat("Mote Brightness", &moteBrightnessObject(), 0.01f, 0.0f, 8.0f, "%.2f");
    RemixGui::Checkbox("Hit Sparks", &sparksObject());
    RemixGui::DragFloat("Sparks per Streak", &sparkCountObject(), 0.1f, 0.0f, 20.0f, "%.1f");
    RemixGui::DragFloat("Spark Speed (x game)", &sparkSpeedObject(), 0.01f, 0.0f, 5.0f, "%.2f");
    RemixGui::DragFloat("Spark Size (cm)", &sparkSizeObject(), 0.05f, 0.1f, 10.0f, "%.2f");
    RemixGui::DragFloat("Spark Life (s)", &sparkLifeObject(), 0.01f, 0.05f, 5.0f, "%.2f");
    RemixGui::DragFloat("Spark Trail (frames)", &sparkTrailObject(), 0.05f, 0.0f, 10.0f, "%.2f");
    RemixGui::DragFloat("Spark Bounce", &sparkBounceObject(), 0.01f, 0.0f, 1.0f, "%.2f");
    RemixGui::DragFloat("Spark Brightness", &sparkBrightnessObject(), 0.01f, 0.0f, 8.0f, "%.2f");
    ImGui::Unindent();
  }

} // namespace dxvk
