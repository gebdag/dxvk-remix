// Fork: the fog of the gate nearest the player (post_fx_dark_souls_fog_gate.h): velocity and density in a small box around the gate, carried along by the velocity. The body
// clears the fog where it stands and drags it along, what is pushed aside swirls, the velocity dies out, and the fog closes back to its rest shape.

#include "rtx/pass/post_fx/post_fx_dark_souls_fog_gate.h"
#include "rtx/utility/common.slangh"

layout(binding = POST_FX_DS_FOG_GATE_SIM_HISTORY)
Sampler3D<float4> History;

layout(binding = POST_FX_DS_FOG_GATE_SIM_OUTPUT)
RWTexture3D<float4> Output;

layout(binding = POST_FX_DS_FOG_GATE_SIM_CONSTANTS)
ConstantBuffer<DsFogGateSimArgs> cb;

// A cheap divergence-free swirl: enough to break up a straight wake.
vec3 swirl(vec3 p)
{
  return vec3(sin(p.y * 2.1f + 1.3f) + cos(p.z * 2.7f), sin(p.z * 2.3f + 0.7f) + cos(p.x * 1.9f), sin(p.x * 2.5f + 2.1f) + cos(p.y * 2.9f));
}

[shader("compute")]
[numthreads(4, 4, 4)]
void main(uint3 texel : SV_DispatchThreadID)
{
  const vec3 grid = vec3(DS_FOG_GATE_SIM_X, DS_FOG_GATE_SIM_Y, DS_FOG_GATE_SIM_Z);
  const vec3 uvw = (vec3(texel) + 0.5f) / grid;
  const vec3 halfBox = dsFogGateSimHalfBox(cb.gate);
  const vec3 local = (uvw * 2.0f - 1.0f) * halfBox;
  const float rest = dsFogGateRest(cb.gate, local);
  if (cb.historyValid == 0)
  {
    Output[texel] = vec4(0.0f, 0.0f, 0.0f, rest);
    return;
  }

  // Carried along: what is here now came from where the velocity points back to.
  const vec3 velocityHere = History.SampleLevel(uvw, 0.0f).xyz;
  const vec4 previous = History.SampleLevel(uvw - velocityHere * cb.dt / (2.0f * halfBox), 0.0f);
  vec3 velocity = previous.xyz * exp(-cb.dt / 0.7f);
  float density = lerp(rest, previous.w, exp(-cb.dt / max(cb.recoverSeconds, 0.05f)));

  // The body, a capsule from the feet up.
  const vec3 feet = dsFogGateToLocal(cb.gate, cb.player);
  const vec3 axisToBody = vec3(feet.x, clamp(local.y, feet.y + cb.radius, feet.y + cb.height - cb.radius), feet.z);
  const vec3 fromBody = local - axisToBody;
  const float distance = length(fromBody);
  const vec3 outward = distance > 1e-3f ? fromBody / distance : vec3(0.0f, 1.0f, 0.0f);
  const vec3 bodyVelocity = vec3(dot(cb.playerVelocity, cb.gate.axisUHalfWidth.xyz), cb.playerVelocity.y, dot(cb.playerVelocity, cb.gate.axisWHalfThick.xyz));
  const float speed = length(bodyVelocity);
  if (distance < cb.radius)
  {
    density = 0.0f;
    velocity = bodyVelocity;
  }
  else
  {
    const float near = saturate(1.0f - (distance - cb.radius) / 0.8f);
    const float push = near * near;
    velocity += (outward * (0.6f + speed * 1.4f) + bodyVelocity * 0.9f) * push * cb.dt * 8.0f;
    density *= 1.0f - 0.5f * push * saturate(cb.dt * 6.0f);
  }
  // What moves, swirls.
  velocity += swirl(local * 1.6f) * length(velocity) * 0.8f * cb.dt;

  Output[texel] = vec4(velocity, saturate(density));
}
