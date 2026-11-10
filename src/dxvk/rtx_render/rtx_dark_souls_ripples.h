#pragma once

#include "rtx_option.h"
#include "rtx/pass/fork_ds1_ripples.h"
#include "rtx/pass/particles/particle_system_common.h"

namespace dxvk {

  class RtxContext;

  // Dark Souls 1 water ripples around the wading player (prototype): an iWave height field on a window
  // of world cells that follows the player, stepped at a fixed rate on the GPU, whose slope the DS1 water
  // material adds to the game's wave layers (fork_ds1_ripples.h has the passes, the layout and the kernel).
  //
  // The window follows rtx.ds1Ripple.playerPosition (the player's feet, sent by the proxy every frame). The
  // sources are the characters' bodies where they cross the water: in a square around the player, rays at
  // the waterline find which cells a character fills (the shins, a robe's hem, an enemy beside them). Those
  // cells hold the water at 0, so waves wrap around the legs, and the water a body pushes aside as it moves
  // rises around it and sinks behind it. A planted foot makes no waves, a swinging leg does.
  // The wet player. Wetness lives in a grid in the body's own space (the proxy puts each vertex's bind position in its
  // colour on the player's draws). Each frame every vertex of the body at or under the water wets its place in the
  // grid, in whatever pose it is, and the whole grid dries a little. So what has been under water stays wet through
  // rolls and animation, and nothing else gets wet.
  class DxvkDarkSoulsWet {
  public:
    RTX_OPTION("rtx.ds1Wet", bool, enable, true, "The player's body gets wet where it has been under water, and dries (needs rtx.ds1Ripple.enable).");
    RTX_OPTION("rtx.ds1Wet", float, dryTime, 10.0f, "Seconds for a wet part of the body to dry completely.");
    RTX_OPTION("rtx.ds1Wet", float, contactMargin, 0.06f, "A vertex this many metres above the water touches it: the game's shallow water lies at or under the ground.");
    RTX_OPTION("rtx.ds1Wet", float, dropletScale, 1.0f, "Size of the beads of water on wet surfaces, 1 = about a centimetre across; 0: none.");
    RTX_OPTION("rtx.ds1Wet", float, runnerScale, 1.0f, "Size of the drops that run down the body as it dries, leaving a trail, 1 = about a centimetre across; 0: none.");
    RTX_OPTION("rtx.ds1Wet", float, mudDryTime, 14.0f, "Seconds for mud on the body to go; water takes it off at once.");
    RTX_OPTION("rtx.ds1Wet", float, mudHardenTime, 5.0f, "Seconds for mud on the body to harden into a dry crust.");
    RTX_OPTION("rtx.ds1Wet", float, mudFadeTime, 2.0f, "The last seconds of the mud on the body, over which the crust fades away in patches.");
    RTX_OPTION("rtx.ds1Wet", float, lavaTime, 14.0f, "Seconds for the marks of lava on the body to go; water takes them off at once.");
    RTX_OPTION("rtx.ds1Wet", float, lavaMoltenTime, 4.0f, "Seconds a part of the body that was in lava stays molten before it has cooled to char.");
    RTX_OPTION("rtx.ds1Wet", float, lavaFadeTime, 10.0f, "The last seconds of the char on the body, over which it fades away in patches.");
    RTX_OPTION("rtx.ds1Wet", float, lavaContact, 0.05f, "A part of the body this many metres above the lava's surface still takes it. The lava is a floor the body stands in, so the coat ends about at the surface.");
    RTX_OPTION("rtx.ds1Wet", float, lavaGlow, 4.0f, "Molten parts of the body at their hottest, radiance.");
    RTX_OPTION_FLAG("rtx.ds1Wet", float, forceLava, -1.0f, RtxOptionFlags::NoSave, "Test: 0 or above, this much lava on the whole body.");
    RTX_OPTION_FLAG("rtx.ds1Wet", bool, testLava, false, RtxOptionFlags::NoSave, "Test: rtx.ds1Wet.testWaterHeight is lava.");
    RTX_OPTION("rtx.ds1Wet", Vector3, mudColour, Vector3(0.16f, 0.13f, 0.065f),
               "The mud's colour, as a texture would store it: Blighttown's deep swamp colour, c170 (0.094, 0.078, 0.039), over its map's c100 of 0.6.");
    RTX_OPTION("rtx.ds1Wet", bool, splashes, true, "Falling water bounces off the player's body as droplets.");
    RTX_OPTION("rtx.ds1Wet", float, splashRate, 3000.0f, "Droplet candidates per second per body draw; only the top of the body near the contact takes them.");
    RTX_OPTION("rtx.ds1Wet", float, splashSize, 0.8f, "Size of a droplet in centimetres.");
    RTX_OPTION("rtx.ds1Wet", float, splashRadius, 0.6f, "Droplets come off the body within this of where the water meets it, sideways, in metres.");
    RTX_OPTION_FLAG("rtx.ds1Wet", Vector3, fallContact, Vector3(0.0f), RtxOptionFlags::NoSave, "Set by the DS1 proxy: where falling water meets the player.");
    RTX_OPTION_FLAG("rtx.ds1Wet", float, fallStrength, 0.0f, RtxOptionFlags::NoSave, "Set by the DS1 proxy: above 0 while the player stands in falling water.");
    RTX_OPTION_FLAG("rtx.ds1Wet", bool, testMud, false, RtxOptionFlags::NoSave, "Test: rtx.ds1Wet.testWaterHeight is mud.");
    RTX_OPTION_FLAG("rtx.ds1Wet", float, forceMud, -1.0f, RtxOptionFlags::NoSave, "Test: 0 or above, this much mud on the whole body.");
    RTX_OPTION("rtx.ds1Wet", bool, drips, true, "Water drips off the downward facing parts of the wet body.");
    RTX_OPTION("rtx.ds1Wet", float, dripTime, 6.0f, "Seconds drips go on after the player leaves the water.");
    RTX_OPTION("rtx.ds1Wet", float, dripRate, 120.0f, "Drip candidates per second per body draw; most are refused by the grid and the surface's direction.");
    RTX_OPTION("rtx.ds1Wet", float, dripSize, 1.0f, "Size of a drip in centimetres.");
    RTX_OPTION("rtx.ds1Wet", float, maxDepth, 1.8f, "Water deeper than this over the feet, in metres, is not the water the player is in.");
    RTX_OPTION("rtx.ds1Wet", float, radius, 1.5f, "Only the part draws this close to the player, sideways, are the player's: other characters wear them too.");
    RTX_OPTION("rtx.ds1Wet", float, roughness, 0.3f, "Roughness of a wet surface, at most: the game's P_Wet exponent 60 as a Blinn-Phong 4n.");
    RTX_OPTION("rtx.ds1Wet", float, specularScale, 1.33f, "Scale on the base reflectivity in display gamma: P_Wet's specular scale 2 over leather's 1.5.");
    RTX_OPTION("rtx.ds1Wet", float, porousFactor, 0.2f, "Albedo scale of a fully porous wet surface.");
    RTX_OPTION_FLAG("rtx.ds1Wet", bool, soak, false, RtxOptionFlags::NoSave, "Wet all over, as under a waterfall.");
    RTX_OPTION_FLAG("rtx.ds1Wet", bool, clear, false, RtxOptionFlags::NoSave, "Test: dry the whole body at once.");
    RTX_OPTION_FLAG("rtx.ds1Wet", int, clearEvent, 0, RtxOptionFlags::NoSave, "Set by the DS1 proxy: a new value clears water, mud and lava off the body (resting at a bonfire, a warp, a load).");
    RTX_OPTION_FLAG("rtx.ds1Wet", float, testWaterHeight, -100000.0f, RtxOptionFlags::NoSave, "Test: above -99999, the water's height everywhere.");
    RTX_OPTION_FLAG("rtx.ds1Wet", float, forceWetness, -1.0f, RtxOptionFlags::NoSave, "Test: 0 or above, this grid value on the whole body.");
  };

  class DxvkDarkSoulsRipples {
  public:
    // Before the frame's bindless tables are built: creates the field, plans the frame's steps and
    // registers the slope texture and its sampler for the material.
    static void prepare(RtxContext* ctx);

    // The material's view of the field (zeroed when it is off or not prepared this frame).
    static void fillShaderArgs(Ds1RippleArgs& args);

    // The material's view of how wet the player is.
    static void fillWetArgs(Ds1WetArgs& args);

    // The particle spawn pass's view of it.
    static void fillParticleArgs(ParticleSystemConstants& constants);

    // A draw of the player's body, this frame: drips off it, while they run.
    static void emitDrips(DxvkContext* ctx, uint32_t instanceIdx);

    // After the frame's TLAS build, before the ray tracing passes: water map, steps, slope.
    static void dispatch(RtxContext* ctx);

    RTX_OPTION("rtx.ds1Ripple", bool, enable, true, "Ripples on DS1 water around the wading player.");
    RTX_OPTION("rtx.ds1Water", float, refractionFlatDepth, 0.3f, "DS1 water no deeper than this, in metres, does not bend the view: legs in shallow water look straight. Works where the wave smear does: not with PSR prepare (no NRC, SHARC or sparse rendering).");
    RTX_OPTION("rtx.ds1Water", float, shoreMinDepth, 0.0f, "Where DS1 water is shallower than twice this, in metres, a view entering it continues from this far under the surface at the least, so it cannot meet the surface again.");
    RTX_OPTION("rtx.ds1Water", float, smearFullDepth, 0.3f, "The game's refraction smear on DS1 water shifts the view in full from this depth in metres, less in shallower water, none at the shore.");
    RTX_OPTION("rtx.ds1Water", float, refractionFullDepth, 2.5f, "DS1 water this deep, in metres, bends the view fully (IOR 1.33). 0 bends it fully at every depth.");
    RTX_OPTION_FLAG("rtx.ds1Ripple", Vector3, playerPosition, Vector3(0.0f), RtxOptionFlags::NoSave,
                    "Set by the DS1 proxy every frame the player moves: their position (feet), where the ripple window is centred.");
    RTX_OPTION("rtx.ds1Ripple", int, gridSize, 512, "Cells along each side of the ripple window (a multiple of 16).");
    RTX_OPTION("rtx.ds1Ripple", float, cellSize, 0.015f,
               "Size of a ripple cell in metres. The shortest ripple that travels is about 8 cells long.");
    RTX_OPTION("rtx.ds1Ripple", float, stepRate, 60.0f, "Ripple simulation steps per second (fixed, independent of the frame rate).");
    RTX_OPTION("rtx.ds1Ripple", int, maxStepsPerFrame, 3, "At most this many steps per frame; time beyond is dropped, never caught up.");
    RTX_OPTION("rtx.ds1Ripple", float, gravityScale, 0.8f,
               "Scale on gravity: 1 gives real water's wave speeds (about 0.5 m/s at 18 cm wavelength).");
    RTX_OPTION("rtx.ds1Ripple", float, damping, 2.4f, "Damping on every wavelength, per second (the paper's alpha).");
    RTX_OPTION("rtx.ds1Ripple", float, viscosity, 1.4e-3f,
               "Viscous damping 2 nu k^2 in m^2/s: short ripples die fast, long ones travel. Clean water is 1e-6; "
               "this stands in for the surface film and the spreading the grid cannot resolve. The step holds it at "
               "its stability limit, 0.1 cellSize^2 * stepRate (0.00135 at 1.5 cm and 60 steps).");
    RTX_OPTION("rtx.ds1Ripple", float, thickDamping, 0.0f,
               "Above 0, a thick liquid such as mud: damping per second on the surface's velocity alone, so what a body pushes up stays up. 0: water, rtx.ds1Ripple.damping.");
    RTX_OPTION("rtx.ds1Ripple", float, thickSettle, 0.4f, "How fast a thick liquid goes flat again, per second.");
    RTX_OPTION("rtx.ds1Ripple", float, smoothing, 0.0f, "Share of the height difference to the neighbours evened out each step, at most 0.2: rounds the edge a body leaves in a thick liquid.");
    RTX_OPTION("rtx.ds1Ripple", float, displacement, 0.01f,
               "Height of the water a body pushes aside per cell it moves into, in metres (spread over the cells around it).");
    RTX_OPTION("rtx.ds1Ripple", float, collisionSize, 3.0f,
               "Side of the square around the player in which characters' bodies push the water, in metres.");
    RTX_OPTION("rtx.ds1Ripple", float, normalScale, 0.5f, "Scale on the ripple slope added to the water normal (1 = the real slope).");
    RTX_OPTION("rtx.ds1Ripple", float, borderFade, 0.75f, "Metres over which ripples fade towards the window's border.");
    RTX_OPTION("rtx.ds1Ripple", float, parallaxScale, 1.0f,
               "Parallax of the ripples on DS1 water and liquid mud: the camera's ray is walked to where it meets the rippled surface, so a crest hides the trough behind it, "
               "and the surface is shaded as it is there. Scale on the heights walked against (they follow rtx.ds1Ripple.normalScale too); 0 = none, the ripples only tilt the normal.");
    RTX_OPTION("rtx.ds1Ripple", float, parallaxBound, 0.05f, "The parallax walk covers ripples up to this high and this deep, in metres. Higher ones are cut off flat there.");
    RTX_OPTION("rtx.ds1Ripple", int, parallaxSteps, 8, "Steps of the parallax walk at the least (looking straight down).");
    RTX_OPTION("rtx.ds1Ripple", int, parallaxMaxSteps, 32, "Steps of the parallax walk at the most (looking along the surface, where a step would otherwise skip a crest).");
    RTX_OPTION_FLAG("rtx.ds1Ripple", Vector3, testBody, Vector3(0.0f), RtxOptionFlags::NoSave,
                    "Test on a replay, where nothing moves: x > 0 is a body of that radius, walking a circle of radius y around the player at z m/s.");
  };

} // namespace dxvk
