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
  //   1  soul motes: the green dots over a bloodstain (s01003, a texture that is itself a cluster of dots). The game's
  //      quads are spawn surfaces, dense in each quad's middle as the texture's dots are; each particle is one dot.
  //   2  spark streaks: the warm lines of a weapon striking stone (FRPG_Sfx_LineType0). Each line crosses the hit's
  //      centre; its ends are marked u = -1 and +1 (the proxy), so a spark flies from the line's middle towards the end
  //      on its side, faster further out, then falls and bounces.
  // The tagged draw is hidden; its particles keep its material (self-lit, its blend) with a soft round dot for a
  // texture, and its vertex colour.
  class DxvkDarkSoulsEffectParticles {
  public:
    // From the scene manager, for an instance of a tagged draw: spawns this frame's particles. False when nothing
    // replaces the draw (the kind is off), so it stays visible.
    static bool emit(DxvkContext* ctx, uint32_t instanceIdx, const DrawCallState& drawCallState, const MaterialData& renderMaterialData);
    static void showImguiSettings();

    RTX_OPTION("rtx.ds1EffectParticles", bool, motes, true, "Replace the green dots over a bloodstain with Remix particles.");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteRate, 24000.0f, "Mote spawn attempts per second per bloodstain (about a fifth are kept, densest in the middle).");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteSize, 1.5f, "Mote diameter, cm.");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteLife, 2.4f, "Mean mote life, seconds.");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteDrift, 12.0f, "Mote starting speed, cm/s, mostly upwards.");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteTurbulence, 40.0f, "Mote turbulence force, cm/s^2.");
    RTX_OPTION("rtx.ds1EffectParticles", float, moteBrightness, 1.0f, "Scale on the motes' colour (the game's own). Above 1 clips the green's channels and turns it pale.");

    RTX_OPTION("rtx.ds1EffectParticles", bool, sparks, true, "Replace the streaks of a weapon striking stone with Remix particles.");
    RTX_OPTION("rtx.ds1EffectParticles", float, sparkRate, 360.0f, "Spark spawn attempts per second per streak draw.");
    RTX_OPTION("rtx.ds1EffectParticles", float, sparkSpeed, 9.0f, "Speed of a spark at its streak's end per metre of half the streak, 1/s (slower further in).");
    RTX_OPTION("rtx.ds1EffectParticles", float, sparkSize, 2.0f, "Spark width, cm.");
    RTX_OPTION("rtx.ds1EffectParticles", float, sparkLife, 0.6f, "Mean spark life, seconds.");
    RTX_OPTION("rtx.ds1EffectParticles", float, sparkTrail, 1.0f, "Spark streak length in frames of its travel (the game draws its sparks as streaks).");
    RTX_OPTION("rtx.ds1EffectParticles", float, sparkBounce, 0.35f, "Share of a spark's speed kept when it bounces.");
    RTX_OPTION("rtx.ds1EffectParticles", float, sparkBrightness, 1.6f, "Scale on the sparks' colour (the game's own).");
  };

} // namespace dxvk
