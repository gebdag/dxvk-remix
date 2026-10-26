// Fork: Dark Souls' light shafts, path traced (see post_fx_dark_souls_look.h).
//
// The game blurs the bright parts of its frame (BrightPassFilter, threshold c71.x for the sky)
// radially towards the shaft sun, so gaps in the clouds and the sky between towers streak across
// the picture. Here each pixel's view ray is a homogeneous medium lit by that sun: a point in the
// air is lit through the sky it sees the sun through, and only where no geometry stands in the way.
//
//   sky:       the sky as a shell of radius cloudDistance around the camera. The sun ray from a
//              point leaves through it at one spot, and the sky raster there, put through the
//              game's bright pass, is what reaches the point: a cloud gap lights it, a cloud does not.
//   geometry:  a ray traced from the point towards the sun (opaque surfaces).
//
// The scattering air is the haze under the clouds: its density rises towards the shell as
// exp((t - R) / layerDepth). A ray into the sky gathers the sky along the end of the arc from the
// sun to itself, the streak behind each gap (the game's radial blur reaches a similar way back);
// a ray stopped by a wall in front of the layer gathers nothing. Samples follow the density, and
// the result is measured against a ray that reaches the shell.
// The result is in the game's display units, for the composite to add as ColAdj adds the game's.

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/pass/instance_definitions.h"
#include "rtx/utility/common.slangh"
#include "rtx/concept/camera/camera.slangh"

layout(binding = POST_FX_DS_LOOK_SHAFTS_TLAS)
RaytracingAccelerationStructure topLevelAS;

layout(binding = POST_FX_DS_LOOK_SHAFTS_OUTPUT)
RWTexture2D<float4> Output;

layout(binding = POST_FX_DS_LOOK_SHAFTS_LINEAR_VIEW_Z)
Texture2D<float> LinearViewZ;

layout(binding = POST_FX_DS_LOOK_SHAFTS_SKY_PROBE)
SamplerCube<float4> SkyProbe;

layout(binding = POST_FX_DS_LOOK_SHAFTS_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookShaftArgs> cb;

// Henyey-Greenstein, 1 for isotropic scattering.
float shaftPhase(float g, float cosTheta)
{
  return (1.0f - g * g) / pow(max(1.0f + g * g - 2.0f * g * cosTheta, 1e-4f), 1.5f);
}

// The sky towards `direction` through the game's bright pass: max(s - t, 0) / (1 - t) on its 8-bit value.
vec3 shaftSkyGap(vec3 direction)
{
  const vec3 raster = SkyProbe.SampleLevel(direction, 0).rgb;
  const vec3 display = pow(ds1SkyRadiance(cb.ambient, raster), vec3(1.0f / 2.2f)) * cb.exposure;
  return max(display - cb.threshold, vec3(0.0f)) / max(1.0f - cb.threshold, 1e-3f);
}

bool shaftSunVisible(vec3 origin)
{
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = cb.direction;
  ray.TMin = 0.05f;
  ray.TMax = cb.occlusionDistance;

  RayQuery<0> rayQuery = RayQuery<0>();
  rayQuery.TraceRayInline(topLevelAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE,
                          OBJECT_MASK_OPAQUE, ray);
  while (rayQuery.Proceed())
  {
  }
  return rayQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(pixel >= cb.outputSize))
  {
    return;
  }

  const vec2 screenUV = (vec2(pixel) + 0.5f) * cb.invOutputSize;
  const CameraDirections directions = cameraScreenUVToDirection(cb.camera, screenUV, false);

  uint2 viewZSize;
  LinearViewZ.GetDimensions(viewZSize.x, viewZSize.y);
  const vec2 viewZUV = screenUV + cb.depthJitterUv;
  const uint2 viewZTexel = min(uint2(max(viewZUV, 0.0f) * vec2(viewZSize)), viewZSize - 1);
  const float viewZ = LinearViewZ[viewZTexel];

  const float radius = max(cb.cloudDistance, 1.0f);
  float end = radius;
  if (viewZ != cb.missLinearViewZ)
  {
    end = min(length(cameraReconstructViewPosition(cb.camera, directions.viewDirection, abs(viewZ))), radius);
  }

  const vec3 cameraPosition = cameraGetWorldPosition(cb.camera);
  const vec3 view = directions.worldDirection;
  const vec3 sun = cb.direction;

  // Stratified along the ray, offset per pixel and frame (interleaved gradient noise).
  const vec2 noisePixel = vec2(pixel) + 5.588238f * float(cb.frameIndex & 63u);
  const float jitter = frac(52.9829189f * frac(dot(noisePixel, vec2(0.06711056f, 0.00583715f))));

  // Density exp((t - R) / L) on [0, end]: its integral, against a ray to the shell, and its inverse CDF.
  const float depth = max(cb.layerDepth, 1.0f);
  const float lowest = exp(-radius / depth);
  const float highest = exp((end - radius) / depth);
  const float share = (highest - lowest) / max(1.0f - lowest, 1e-6f);
  if (share <= 1e-4f)
  {
    Output[pixel] = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    return;
  }

  const uint count = max(cb.samples, 1u);
  vec3 sum = vec3(0.0f);
  for (uint i = 0; i < count; ++i)
  {
    const float u = (float(i) + jitter) / float(count);
    const float t = radius + depth * log(lowest + u * (highest - lowest));
    const vec3 p = view * t; // camera relative

    // Where the sun ray from p leaves the shell.
    const float b = dot(p, sun);
    const float c = dot(p, p) - radius * radius;
    const float s = -b + sqrt(max(b * b - c, 0.0f));
    const vec3 gap = shaftSkyGap(normalize(p + sun * s));
    if (all(gap <= 0.0f))
    {
      continue;
    }
    if (t < cb.occlusionDistance && !shaftSunVisible(cameraPosition + p))
    {
      continue;
    }
    sum += gap;
  }

  const vec3 shafts = sum * share / float(count) * shaftPhase(cb.anisotropy, dot(view, sun)) * cb.gain;
  Output[pixel] = vec4(shafts, 1.0f);
}
