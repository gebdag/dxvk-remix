// Fork: draws the fog gates (post_fx_dark_souls_fog_gate.h) into the look's scene image, in its gamma space, as the game composites its fog: a ray through each gate's box, per pixel,
// stopping at the surface behind. Light: the froxel radiance cache (the area's real lights and shadows), the area's ambient cube, and a little light of the fog's own. After the
// upscaler, so Ray Reconstruction cannot smear it; so it shows in no reflection and casts no light.

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/pass/post_fx/post_fx_dark_souls_fog_gate.h"
#include "rtx/utility/common.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/color.slangh"
#include "rtx/utility/froxel.slangh"
#include "rtx/concept/camera/camera.slangh"
#include "rtx/concept/light/spherical_harmonics.slangh"

layout(binding = POST_FX_DS_FOG_GATE_MARCH_INOUT)
RWTexture2D<float4> InOutColor;

layout(binding = POST_FX_DS_FOG_GATE_MARCH_VIEW_Z)
Texture2D<float> LinearViewZ;

layout(binding = POST_FX_DS_FOG_GATE_MARCH_NOISE)
Sampler3D<float> Noise;

layout(binding = POST_FX_DS_FOG_GATE_MARCH_SIM)
Sampler3D<float4> Sim;

layout(binding = POST_FX_DS_FOG_GATE_MARCH_RADIANCE_Y)
Sampler3D<float4> RadianceY;

layout(binding = POST_FX_DS_FOG_GATE_MARCH_RADIANCE_COCG)
Sampler3D<float2> RadianceCoCg;

layout(binding = POST_FX_DS_FOG_GATE_MARCH_HAZE)
ConstantBuffer<PostFxDarkSoulsHazeArgs> haze;

layout(binding = POST_FX_DS_FOG_GATE_MARCH_CONSTANTS)
ConstantBuffer<DsFogGateMarchArgs> cb;

layout(binding = POST_FX_DS_FOG_GATE_MARCH_DOF_RATE)
RWTexture2D<float2> DofRate;

// For the bloom: x how much of the pixel the fog hides, y its view Z. View Z knows nothing of the fog: with the sky behind a gate, the bloom took it for the far plane.
layout(binding = POST_FX_DS_FOG_GATE_MARCH_MASK)
RWTexture2D<float2> Mask;

// The froxel radiance cache's light at a point, as evalVolumetricNEE has it, without the integrator's bindings that comes with.
vec3 cacheLight(vec3 world)
{
  const VolumeArgs volumeArgs = cb.volumeArgs;
  const VolumeDefinitionCamera camera = volumeArgs.cameras[froxelVolumeMain];
  const vec3 virtualUVW = translatedWorldPositionToFroxelUVW(
    camera.translatedWorldToView, camera.translatedWorldToProjectionJittered, packedFlagGet(camera.flags, rightHandedFlag),
    volumeArgs.froxelDepthSlices, volumeArgs.froxelDepthSliceDistributionExponent, volumeArgs.froxelMaxDistance, camera.nearPlane,
    worldToTranslatedWorld(camera, world));
  if (!all(virtualUVW > 0.0f) || !all(virtualUVW < 1.0f))
  {
    return vec3(0.0f);
  }
  const vec3 physicalUVW = virtualFroxelUVWToPhysicalFroxelUVW(
    virtualUVW, froxelVolumeMain, volumeArgs.minFilteredRadianceU, volumeArgs.maxFilteredRadianceU, volumeArgs.inverseNumFroxelVolumes);
  SphericalHarmonic sh = HarmonicsHelpers::loadFiltered3D<SphericalHarmonic>(RadianceY, RadianceCoCg, physicalUVW);
  sh.hanningFilter(0.7f);
  return sh.getIrradiance();
}

// Where a ray enters and leaves a box around the origin. False when it misses.
bool rayBox(vec3 origin, vec3 direction, vec3 halfBox, out float enter, out float leave)
{
  const vec3 inverse = 1.0f / (abs(direction) + 1e-6f) * sign(direction + 1e-9f);
  const vec3 a = (-halfBox - origin) * inverse, b = (halfBox - origin) * inverse;
  const vec3 near = min(a, b), far = max(a, b);
  enter = max(max(near.x, near.y), near.z);
  leave = min(min(far.x, far.y), far.z);
  return leave > max(enter, 0.0f);
}

float fogShape(vec3 local, float time)
{
  // Tall slow curtains, then finer wisps moved by them.
  const vec3 curtain = vec3(local.x * 0.16f + time * 0.012f, local.y * 0.07f - time * 0.03f, local.z * 0.2f);
  const float large = Noise.SampleLevel(curtain, 0.0f);
  const vec3 wisp = vec3(local.x * 0.9f - time * 0.04f, local.y * 0.45f - time * 0.07f + large, local.z * 0.9f + large);
  const float fine = Noise.SampleLevel(wisp, 0.0f);
  return saturate((large * 0.6f + fine * 0.6f - cb.shapeLow) * cb.shapeGain);
}

vec3 ambientLight()
{
  const vec3 up = ds1AmbientRadiance(cb.ambient, vec3(0.0f, 1.0f, 0.0f));
  const vec3 side = 0.25f * (ds1AmbientRadiance(cb.ambient, vec3(1.0f, 0.0f, 0.0f)) + ds1AmbientRadiance(cb.ambient, vec3(-1.0f, 0.0f, 0.0f)) +
                             ds1AmbientRadiance(cb.ambient, vec3(0.0f, 0.0f, 1.0f)) + ds1AmbientRadiance(cb.ambient, vec3(0.0f, 0.0f, -1.0f)));
  const vec3 both = 0.5f * (up + side);
  return lerp(vec3(dot(both, vec3(0.2126f, 0.7152f, 0.0722f))), both, cb.ambientSaturation);
}

// Interleaved gradient noise: starts each ray a little further along, so slices do not show.
float rayJitter(uint2 pixel)
{
  return frac(52.9829189f * frac(dot(vec2(pixel), vec2(0.06711056f, 0.00583715f))));
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(pixel >= haze.imageSize))
  {
    return;
  }

  const vec2 screenUV = (vec2(pixel) + 0.5f) / vec2(haze.imageSize);
  const CameraDirections directions = cameraScreenUVToDirection(haze.camera, screenUV, false);
  const vec3 cameraWorld = cameraGetWorldPosition(haze.camera);
  const vec3 direction = vec3(directions.worldDirection);

  uint2 viewZSize;
  LinearViewZ.GetDimensions(viewZSize.x, viewZSize.y);
  bool looked = false;
  float surfaces[9];
  const vec3 ambient = ambientLight() * cb.ambientGain;

  float transmittance = 1.0f;
  vec3 scattered = vec3(0.0f);
  float scatteredWeight = 0.0f;
  float distanceSum = 0.0f;
  float coverage = 0.0f;

  for (uint g = 0; g < cb.gateCount; ++g)
  {
    const DsFogGate gate = cb.gates[g];
    const float level = gate.centreLevel.w;
    if (level <= 0.001f)
    {
      continue;
    }
    const bool simulated = gate.halfHeightSim.y > 0.5f;
    const vec3 halfBox = simulated ? dsFogGateSimHalfBox(gate)
                                   : vec3(gate.axisUHalfWidth.w, gate.halfHeightSim.x, gate.axisWHalfThick.w * 2.0f);
    const vec3 originLocal = dsFogGateToLocal(gate, cameraWorld);
    const vec3 directionLocal = vec3(dot(direction, gate.axisUHalfWidth.xyz), direction.y, dot(direction, gate.axisWHalfThick.xyz));
    float enter, leave;
    if (!rayBox(originLocal, directionLocal, halfBox, enter, leave))
    {
      continue;
    }
    enter = max(enter, 0.0f);

    // The surface behind, from view Z at this frame's jittered sample positions. Its silhouette flips from frame to frame: a tent over its neighbours, each stopped at the box's far side.
    if (!looked)
    {
      looked = true;
      const vec2 viewZUV = screenUV + haze.depthJitterUv;
      int n = 0;
      for (int y = -1; y <= 1; ++y)
      {
        for (int x = -1; x <= 1; ++x)
        {
          const int2 texel = clamp(int2(clamp(viewZUV, 0.0f, 1.0f) * vec2(viewZSize)) + int2(x, y), int2(0, 0), int2(viewZSize) - 1);
          const float viewZ = LinearViewZ[texel];
          surfaces[n++] = viewZ == haze.missLinearViewZ ? 1e9f : length(cameraReconstructViewPosition(haze.camera, directions.viewDirection, abs(viewZ)));
        }
      }
    }
    // Where the ray meets the plane, within the box: seen at a slant it would meet the infinite plane far past the gate.
    const float planeAt = clamp(abs(directionLocal.z) > 1e-4f ? -originLocal.z / directionLocal.z : enter, enter, leave);
    // What stands in front of the fog hides it by how many of the samples it takes: an edge as fine as the frame's, not the stairs of the lower resolution view Z. The others give where
    // the ray ends.
    const float fogStarts = max(planeAt - 1.5f * gate.axisWHalfThick.w / max(abs(directionLocal.z), 0.1f), enter);
    float end = 0.0f;
    float surface = 0.0f;
    float clear = 0.0f;
    for (int n = 0; n < 9; ++n)
    {
      if (surfaces[n] <= fogStarts)
      {
        continue;
      }
      const int x = n % 3 - 1, y = n / 3 - 1;
      const float weight = float((2 - abs(x)) * (2 - abs(y))) / 16.0f;
      end += min(surfaces[n], leave) * weight;
      surface += min(surfaces[n], 1000.0f) * weight;
      clear += weight;
    }
    if (clear <= 0.0f)
    {
      continue;
    }
    end /= clear;
    surface /= clear;
    if (end <= enter)
    {
      continue;
    }

    // A surface close behind the gate's plane fades the fog, as a soft particle's: it stays in its doorway instead of hanging in front of the wall around it, and meets the floor softly.
    const float contact = saturate((surface - planeAt) / max(cb.contact, 0.01f));
    const float contactFade = contact * contact * (3.0f - 2.0f * contact);
    if (contactFade <= 0.001f)
    {
      continue;
    }

    coverage = max(coverage, clear);
    const float span = end - enter;
    const uint steps = clamp(uint(span / 0.1f) + 4u, 4u, cb.maxSteps);
    const float stepSize = span / float(steps);
    const float jitter = rayJitter(pixel);
    for (uint s = 0; s < steps; ++s)
    {
      const float t = enter + (float(s) + jitter) * stepSize;
      const vec3 local = originLocal + directionLocal * t;
      float density;
      if (simulated)
      {
        density = Sim.SampleLevel(local / (2.0f * halfBox) + 0.5f, 0.0f).w;
      }
      else
      {
        density = dsFogGateRest(gate, local);
      }
      if (density <= 0.002f)
      {
        continue;
      }
      // Whole gate: never a hole, the noise only shapes how it is lit and how thick. A dissolving gate thins out from its thin parts: holes grow, it does not just turn see-through.
      const float shape = fogShape(local, cb.time);
      const float cut = saturate((shape - (1.0f - level) * 1.25f) / 0.25f + 1.0f);
      const float eroded = lerp(cb.solid, 1.0f, shape) * cut * cut * (3.0f - 2.0f * cut) * level;
      const float sigma = cb.density * density * eroded * contactFade;
      if (sigma <= 1e-4f)
      {
        continue;
      }
      const float absorbed = 1.0f - exp(-sigma * stepSize);

      vec3 light = ambient;
      // What gives a volume its shape: light comes mostly from above, and fog above a point takes it. Thin wisps come out bright, dense folds dark.
      const float above = fogShape(local + vec3(0.12f, 0.45f, 0.0f), cb.time);
      const float lit = lerp(1.0f, exp(-above * 2.2f), cb.selfShadow);
      light *= lit;
      if (cb.useCache != 0)
      {
        light += cacheLight(cameraWorld + direction * t) * cb.lightGain * lit;
      }
      scattered += light * absorbed * transmittance;
      scatteredWeight += absorbed * transmittance;
      distanceSum += t * absorbed * transmittance;
      transmittance *= 1.0f - absorbed;
      if (transmittance < 0.01f)
      {
        break;
      }
    }
    if (transmittance < 0.01f)
    {
      break;
    }
  }

  if (scatteredWeight <= 0.0f)
  {
    Mask[pixel] = float2(0.0f, 0.0f);
    return;
  }

  // In the scene's own space: its gamma, with its exposure, as the game's sprites were.
  const vec3 lit = scattered / scatteredWeight;
  const vec3 fog = saturate(pow(max(lit, vec3(0.0f)), vec3(1.0f / 2.2f)) * cb.exposure + cb.glow);
  const vec4 frame = InOutColor[pixel];
  const float opacity = (1.0f - transmittance) * coverage;
  InOutColor[pixel] = vec4(lerp(frame.rgb, fog, opacity), frame.a);

  // What follows goes by depth: fog stands where it stands, not where the surface behind it does.
  const vec3 viewDirection = vec3(directions.viewDirection);
  const float fogViewZ = distanceSum / scatteredWeight * abs(viewDirection.z) / max(length(viewDirection), 1e-6f);
  Mask[pixel] = float2(opacity, fogViewZ);
  const float fogRate = saturate(saturate((fogViewZ - cb.dofFarBegin) / max(cb.dofFarEnd - cb.dofFarBegin, 1e-6f)) * cb.dofFarStrength);
  const float2 rate = DofRate[pixel];
  DofRate[pixel] = float2(lerp(rate.x, fogRate, opacity), rate.y);
}
