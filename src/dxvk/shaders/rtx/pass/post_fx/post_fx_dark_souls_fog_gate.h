// Fork: DS1 fog gates as a volume instead of the game's sprite ribbons (rtx_dark_souls_look.h, rtx.ds1FogGate.*). A slab fitted to each gate by the proxy, marched per pixel;
// the gate nearest the player has a small 3D simulation of the fog, with the player's body as an obstacle.
#pragma once

#include "rtx/concept/camera/camera.h"
#include "rtx/pass/volume_args.h"
#include "rtx/pass/fork_ds1_ambient.h"
#include "rtx/utility/shader_types.h"

#define POST_FX_DS_FOG_GATE_NOISE_OUTPUT       0

#define POST_FX_DS_FOG_GATE_SIM_HISTORY        0
#define POST_FX_DS_FOG_GATE_SIM_OUTPUT         1
#define POST_FX_DS_FOG_GATE_SIM_CONSTANTS      2

#define POST_FX_DS_FOG_GATE_MARCH_INOUT        0
#define POST_FX_DS_FOG_GATE_MARCH_VIEW_Z       1
#define POST_FX_DS_FOG_GATE_MARCH_NOISE        2
#define POST_FX_DS_FOG_GATE_MARCH_SIM          3
#define POST_FX_DS_FOG_GATE_MARCH_RADIANCE_Y   4
#define POST_FX_DS_FOG_GATE_MARCH_RADIANCE_COCG 5
#define POST_FX_DS_FOG_GATE_MARCH_HAZE         6
#define POST_FX_DS_FOG_GATE_MARCH_CONSTANTS    7
#define POST_FX_DS_FOG_GATE_MARCH_DOF_RATE     8
#define POST_FX_DS_FOG_GATE_MARCH_MASK         9

#define DS_FOG_GATE_MAX 4
#define DS_FOG_GATE_NOISE_SIZE 64
#define DS_FOG_GATE_SIM_X 80
#define DS_FOG_GATE_SIM_Y 48
#define DS_FOG_GATE_SIM_Z 32
// How far the simulated box reaches past the gate: fog pushed aside needs somewhere to go.
#define DS_FOG_GATE_SIM_MARGIN_U 0.8f
#define DS_FOG_GATE_SIM_MARGIN_V 0.3f
#define DS_FOG_GATE_SIM_HALF_W 1.6f

struct DsFogGate {
  vec4 centreLevel;     // world centre; w: how much of the gate there is, 0-1 (the game's effect)
  vec4 axisUHalfWidth;  // along the gate; w: half width
  vec4 axisWHalfThick;  // through the gate; w: half thickness
  vec4 halfHeightSim;   // x: half height; y: 1 for the simulated gate
};

struct DsFogGateSimArgs {
  DsFogGate gate;

  vec3 player;          // the feet
  float dt;
  vec3 playerVelocity;
  float recoverSeconds;
  uint historyValid;
  float radius;
  float height;
  float pad0;
};

struct DsFogGateMarchArgs {
  VolumeArgs volumeArgs;
  Ds1AmbientArgs ambient;
  DsFogGate gates[DS_FOG_GATE_MAX];

  // -- Struct objects should go above this line to preserve alignment --

  vec3 glow;            // the fog's own light, scene gamma
  float density;        // per metre at full density
  uint gateCount;
  uint maxSteps;
  float exposure;
  float time;
  float lightGain;      // on the froxel cache's light
  float ambientGain;
  uint useCache;
  float shapeLow;       // where the noise starts to hold fog
  float shapeGain;      // how sharply fog sets in from there
  float selfShadow;     // how much fog above a point darkens it
  float solid;          // what the thinnest noise still holds at rest: above 0 there are no holes
  float contact;        // over this many metres behind the gate's plane a surface fades the fog

  float dofFarBegin;    // the scene pass's far DoF rate, for what stands in front of the view Z
  float dofFarEnd;
  float dofFarStrength;
  float ambientSaturation; // of the ambient's colour: the game's fog is neutral
};

#ifndef __cplusplus
// The fog at rest: solid across the gate, because past it there is often nothing to see (the next area is not loaded, only the sky). Thinning only out of its plane and in its last
// bit towards the sides and top, which the door frame normally hides.
float dsFogGateRest(DsFogGate gate, vec3 local)
{
  const float halfWidth = gate.axisUHalfWidth.w, halfHeight = gate.halfHeightSim.x, halfThick = max(gate.axisWHalfThick.w, 0.1f);
  const float side = saturate((halfWidth - abs(local.x)) / 0.25f);
  const float top = saturate((halfHeight - abs(local.y)) / 0.25f);
  const float through = local.z / halfThick;
  return side * side * (3.0f - 2.0f * side) * top * exp(-through * through * through * through);
}

vec3 dsFogGateSimHalfBox(DsFogGate gate)
{
  return vec3(gate.axisUHalfWidth.w + DS_FOG_GATE_SIM_MARGIN_U, gate.halfHeightSim.x + DS_FOG_GATE_SIM_MARGIN_V, DS_FOG_GATE_SIM_HALF_W);
}

vec3 dsFogGateToLocal(DsFogGate gate, vec3 world)
{
  const vec3 d = world - gate.centreLevel.xyz;
  return vec3(dot(d, gate.axisUHalfWidth.xyz), d.y, dot(d, gate.axisWHalfThick.xyz));
}
#endif
