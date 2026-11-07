#pragma once

#include "rtx_option.h"

namespace dxvk {

  class DxvkContext;
  struct DrawCallState;
  struct MaterialData;

  // Fork (DS1): two of the game's sprite effects replaced by Remix particles, which move by themselves (gravity,
  // drag, turbulence) and meet the scene (collision against the G-buffer, the moving player included).
  //
  // The DS1 proxy tags the draws (stage 4 D3DTSS_CONSTANT, LegacyMaterialData::ds1EffectParticles):
  //   1  soul motes: the green dots over a bloodstain. The game draws 12-16 camera-facing cards (s01003: a picture of a
  //      ball of dots, dense in its middle) that rise from the ground to about 0.6 m, fading. Each card spawns its dots
  //      as a ball: across the card with the texture's density, and in depth as far as a sphere of the card's radius
  //      reaches. Cards spawn by their alpha, so fading cards add few dots. The dots rise and fade.
  //   2  spark streaks, a hit's first frame: the warm lines of a weapon striking stone (FRPG_Sfx_LineType0). Each line runs
  //      along one spark's flight. The proxy moves every line's middle to the burst's centre and marks its ends u = -1
  //      and +1: sparks fly from that centre both ways along each line, at a speed from its length, then fall and bounce.
  //   3  the same effect's later frames: hidden, nothing spawned (its sparks are already flying).
  // The tagged draw is hidden; its particles keep its material (self-lit, its blend) with a soft round dot for a
  // texture, and its vertex colour.
  class DxvkDarkSoulsEffectParticles {
  public:
    // From the scene manager, for an instance of a tagged draw: spawns this frame's particles. False when nothing
    // replaces the draw (the kind is off), so it stays visible.
    static bool emit(DxvkContext* ctx, uint32_t instanceIdx, const DrawCallState& drawCallState, const MaterialData& renderMaterialData);
    static void showImguiSettings();

    RTX_OPTION("rtx.ds1EffectParticles", bool, motes, true, "Replace the green dots over a bloodstain with Remix particles.");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteRate, 24000.0f, "Mote spawn attempts per second per bloodstain (kept by the texture's density and the card's alpha).");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteSize, 1.5f, "Mote diameter, cm.");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteLife, 1.8f, "Mean mote life, seconds.");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteDrift, 30.0f, "Mote rise speed at spawn, cm/s, nearly straight up.");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteTurbulence, 6.0f, "Mote turbulence force, cm/s^2 (slow swirls of about 70 cm).");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteBrightness, 1.0f, "Scale on the motes' colour (the game's own). Above 1 clips the green's channels and turns it pale.");

    RTX_OPTION("rtx.ds1EffectParticles", bool, sparks, true, "Replace the streaks of a weapon striking stone with Remix particles.");
    RTX_OPTION("rtx.ds1EffectParticles", float, sparkCount, 2.0f, "Sparks spawned per streak in a hit's first frame.");
    RTX_OPTION("rtx.ds1EffectParticles", float, sparkSpeed, 35.0f, "Speed of a spark per metre of half its streak in the burst's first frame, 1/s (each spark varies by -40% to +20%).");
    RTX_OPTION("rtx.ds1EffectParticles", float, sparkSize, 2.0f, "Spark width, cm.");
    RTX_OPTION("rtx.ds1EffectParticles", float, sparkLife, 0.6f, "Mean spark life, seconds.");
    RTX_OPTION("rtx.ds1EffectParticles", float, sparkTrail, 1.0f, "Spark streak length in frames of its travel (the game draws its sparks as streaks).");
    RTX_OPTION("rtx.ds1EffectParticles", float, sparkBounce, 0.35f, "Share of a spark's speed kept when it bounces.");
    RTX_OPTION("rtx.ds1EffectParticles", float, sparkBrightness, 1.6f, "Scale on the sparks' colour (the game's own).");
  };

} // namespace dxvk
