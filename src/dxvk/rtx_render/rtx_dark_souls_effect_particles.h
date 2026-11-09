#pragma once

#include "rtx_option.h"
#include "rtx/pass/particles/particle_system_common.h"

namespace dxvk {

  class DxvkContext;
  struct DrawCallState;
  struct MaterialData;

  // Fork (DS1): one of the game's sprite effects replaced by Remix particles, which move by themselves (gravity,
  // drag, turbulence) and meet the scene (collision against the G-buffer, the moving player included).
  //
  // The DS1 proxy tags the draws (stage 4 D3DTSS_CONSTANT, LegacyMaterialData::ds1EffectParticles):
  //   1  soul motes: the green dots over a bloodstain. The game draws 12-16 camera-facing cards (s01003: a picture of a
  //      ball of dots, dense in its middle) that rise from the ground to about 0.6 m, fading. Each card spawns its dots
  //      as a ball: across the card with the texture's density, and in depth as far as a sphere of the card's radius
  //      reaches. Cards spawn by their alpha, so fading cards add few dots. The dots rise and fade.
  // The tagged draw is hidden; its particles keep its material (self-lit, its blend) with a soft round dot for a
  // texture, and its vertex colour.
  class DxvkDarkSoulsEffectParticles {
  public:
    // From the scene manager, for an instance of a tagged draw: spawns this frame's particles. False when nothing
    // replaces the draw (the kind is off), so it stays visible.
    static bool emit(DxvkContext* ctx, uint32_t instanceIdx, const DrawCallState& drawCallState, const MaterialData& renderMaterialData);
    static void showImguiSettings();
    // The motes' shape and the player's push, into the particle constants (once a frame).
    static void fillParticleArgs(ParticleSystemConstants& constants);

    RTX_OPTION("rtx.ds1EffectParticles", bool, motes, true, "Replace the green dots over a bloodstain with Remix particles.");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteRate, 24000.0f, "Mote spawn attempts per second per bloodstain (kept by the texture's density and the card's alpha).");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteSize, 1.5f, "Mote diameter, cm.");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteLife, 1.8f, "Mean mote life, seconds.");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteDrift, 18.0f, "Mote rise speed at spawn, cm/s, nearly straight up.");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteCore, 5.0f, "How strongly the motes gather in each card's middle: the density exponent (3: the texture's own falloff, higher: a tighter ball).");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteReach, 50.0f, "Motes fade out once they have travelled half to all of this far, cm: they stay near the shape or fade away.");
    RTX_OPTION("rtx.ds1EffectParticles", float, motePush, 1.5f, "How fast the player's body pushes motes out of its way, m/s at its middle; they also move with the player. 0: no push.");
    RTX_OPTION("rtx.ds1EffectParticles", float, motePushRadius, 45.0f, "Radius of the body that pushes the motes, cm (a capsule from the feet to 1.8 m).");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteTurbulence, 6.0f, "Mote turbulence force, cm/s^2 (slow swirls of about 70 cm).");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteBrightness, 1.0f, "Scale on the motes' colour (the game's own). Above 1 clips the green's channels and turns it pale.");

  };

} // namespace dxvk
