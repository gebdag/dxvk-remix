// Fork: bakes the tiling noise the fog gates are shaped by (post_fx_dark_souls_fog_gate.h): cheap to read many times per pixel. Runs once.

#include "rtx/pass/post_fx/post_fx_dark_souls_fog_gate.h"
#include "rtx/utility/common.slangh"

layout(binding = POST_FX_DS_FOG_GATE_NOISE_OUTPUT)
RWTexture3D<float> Output;

float hash(int3 p, int period)
{
  p = ((p % period) + period) % period;
  uint h = uint(p.x) * 73856093u ^ uint(p.y) * 19349663u ^ uint(p.z) * 83492791u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return float(h & 0xFFFFu) / 65535.0f;
}

float valueNoise(vec3 p, int period)
{
  const int3 i = int3(floor(p));
  vec3 f = p - vec3(i);
  f = f * f * (3.0f - 2.0f * f);
  float x[4];
  for (int k = 0; k < 4; ++k)
  {
    const int3 c = i + int3(0, k & 1, (k >> 1) & 1);
    x[k] = lerp(hash(c, period), hash(c + int3(1, 0, 0), period), f.x);
  }
  return lerp(lerp(x[0], x[1], f.y), lerp(x[2], x[3], f.y), f.z);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void main(uint3 texel : SV_DispatchThreadID)
{
  const vec3 uvw = (vec3(texel) + 0.5f) / float(DS_FOG_GATE_NOISE_SIZE);
  float sum = 0.0f, weight = 0.0f, amplitude = 1.0f;
  int period = 4;
  for (int octave = 0; octave < 4; ++octave)
  {
    sum += valueNoise(uvw * float(period), period) * amplitude;
    weight += amplitude;
    amplitude *= 0.5f;
    period *= 2;
  }
  Output[texel] = sum / weight;
}
