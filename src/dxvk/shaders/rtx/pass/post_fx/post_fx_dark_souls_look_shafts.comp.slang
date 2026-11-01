// Fork: Dark Souls' light shafts, the game's FRPG_Fil_LightShaft chain in direction space (see
// post_fx_dark_souls_look.h).
//
// The game blurs its shaft source radially towards the shaft sun's screen position, in texels of its 256-wide
// buffer. Here every step runs on view directions instead: the source, the blur and the first pass live in
// cube maps around the camera, and the passes step along the great circle towards the shaft sun's direction,
// the game's reach in texels turned into an angle (PostFxDarkSoulsLookShaftArgs::texelAngle). So the shafts
// stay put when the camera turns, and sky beyond the screen's edge still shines in.
//
// Modes:
//   0 source  the shaft source cube (the sky layers the game redraws for its filter) through the game's
//             BrightPassFilter at c71.x, where the camera sees open sky: on screen the primary rays' misses
//             (alpha-tested foliage leaves gaps, as in the game's depth mask), off screen a ray from the
//             camera that solid geometry stops.
//   1 blur    GaussBlur5x5, its 13 taps an angle apart in the direction's tangent plane.
//   2 pass 1  LightShaft with the first pass's c70, into a cube.
//   3 pass 2  LightShaft with the second pass's c70 at each output texel's view direction, into the bright
//             pass buffer the composite adds.

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/pass/instance_definitions.h"
#include "rtx/utility/common.slangh"
#include "rtx/concept/camera/camera.slangh"

layout(binding = POST_FX_DS_LOOK_SHAFTS_TLAS)
RaytracingAccelerationStructure topLevelAS;

layout(binding = POST_FX_DS_LOOK_SHAFTS_SOURCE)
SamplerCube<float4> Source;

layout(binding = POST_FX_DS_LOOK_SHAFTS_INPUT)
SamplerCube<float4> Input;

layout(binding = POST_FX_DS_LOOK_SHAFTS_OUTPUT_CUBE)
RWTexture2DArray<float4> OutputCube;

layout(binding = POST_FX_DS_LOOK_SHAFTS_OUTPUT)
RWTexture2D<float4> Output;

layout(binding = POST_FX_DS_LOOK_SHAFTS_LINEAR_VIEW_Z)
Texture2D<float> LinearViewZ;

layout(binding = POST_FX_DS_LOOK_SHAFTS_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookShaftArgs> cb;

// The direction through a cube texel's centre plus an offset in texels (Vulkan's cube face layout).
vec3 cubeDirection(uint face, vec2 texel)
{
  const vec2 st = texel / float(cb.cubeSize) * 2.0f - 1.0f;
  vec3 d;
  switch (face)
  {
  case 0: d = vec3(1.0f, -st.y, -st.x); break;
  case 1: d = vec3(-1.0f, -st.y, st.x); break;
  case 2: d = vec3(st.x, 1.0f, st.y); break;
  case 3: d = vec3(st.x, -1.0f, -st.y); break;
  case 4: d = vec3(st.x, -st.y, 1.0f); break;
  default: d = vec3(-st.x, -st.y, -1.0f); break;
  }
  return normalize(d);
}

// Open sky in a direction from the camera.
float skyVisible(vec3 direction)
{
  const vec4 view = mul(cb.camera.worldToView, vec4(direction, 0.0f));
  const vec4 clip = mul(cb.camera.viewToProjection, view);
  if (clip.w > 0.0f)
  {
    const vec2 uv = cameraNDCToScreenUV(cb.camera, clip.xy / clip.w);
    if (all(uv >= 0.0f) && all(uv < 1.0f))
    {
      uint2 size;
      LinearViewZ.GetDimensions(size.x, size.y);
      const uint2 texel = min(uint2(max(uv + cb.depthJitterUv, 0.0f) * vec2(size)), size - 1);
      return LinearViewZ[texel] == cb.missLinearViewZ ? 1.0f : 0.0f;
    }
  }

  RayDesc ray;
  ray.Origin = cameraGetWorldPosition(cb.camera);
  ray.Direction = direction;
  ray.TMin = 0.0f;
  ray.TMax = 1e5f;
  RayQuery<0> rayQuery = RayQuery<0>();
  rayQuery.TraceRayInline(topLevelAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_NON_OPAQUE |
                          RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, OBJECT_MASK_OPAQUE, ray);
  while (rayQuery.Proceed())
  {
  }
  return rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0f : 1.0f;
}

// The source as the game's bright pass sees it: the sky raster turned into the game's scene value (as the
// scene pass turns a primary miss), thresholded.
vec3 brightSource(vec3 direction)
{
  const vec3 raster = Source.SampleLevel(direction, 0.0f).rgb;
  const vec3 scene = saturate(pow(cb.skyBrightness * ds1SkyRadiance(cb.skyGamma, cb.skyUnexpose, raster), vec3(1.0f / 2.2f)) * cb.exposure);
  return max(scene - cb.threshold, vec3(0.0f)) / max(1.0f - cb.threshold, 1e-6f);
}

// An orthonormal pair across a direction.
void tangentFrame(vec3 d, out vec3 t, out vec3 b)
{
  const vec3 up = abs(d.y) < 0.99f ? vec3(0.0f, 1.0f, 0.0f) : vec3(1.0f, 0.0f, 0.0f);
  t = normalize(cross(up, d));
  b = cross(d, t);
}

// One LightShaft pass at a direction: 10 taps stepping towards the shaft sun, a tenth of the reach apart, the
// reach the angle to the sun capped at maxLength texels; each tap's weight the previous one's times decay.
vec3 lightShaft(vec3 d)
{
  const vec3 s = cb.sunDirection;
  const float cosToSun = clamp(dot(d, s), -1.0f, 1.0f);
  const float toSun = acos(cosToSun);
  const vec3 across = cross(d, s);
  const float acrossLength = length(across);
  // Towards the sun along the great circle; any perpendicular when the sun is straight behind.
  vec3 towards;
  if (acrossLength > 1e-5f)
  {
    towards = cross(across / acrossLength, d);
  }
  else
  {
    vec3 b;
    tangentFrame(d, towards, b);
  }
  const float step = min(toSun, cb.maxLength * cb.texelAngle) * 0.1f;

  vec3 colour = vec3(0.0f);
  float decay = 1.0f;
  [unroll]
  for (int i = 0; i < 10; i++)
  {
    const float a = step * float(i);
    colour += Input.SampleLevel(d * cos(a) + towards * sin(a), 0.0f).rgb * decay;
    decay *= cb.decay;
  }
  return colour * cb.weight;
}

static const vec3 kGauss5x5[13] = {
  vec3(-2, 0, 0.024882), vec3(-1, -1, 0.067638), vec3(-1, 0, 0.111515), vec3(-1, 1, 0.067638),
  vec3(0, -2, 0.024882), vec3(0, -1, 0.111515), vec3(0, 0, 0.183858), vec3(0, 1, 0.111515),
  vec3(0, 2, 0.024882), vec3(1, -1, 0.067638), vec3(1, 0, 0.111515), vec3(1, 1, 0.067638),
  vec3(2, 0, 0.024882),
};

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
  if (cb.mode == 3)
  {
    if (any(thread.xy >= cb.outputSize))
    {
      return;
    }
    const vec2 uv = (vec2(thread.xy) + 0.5f) / vec2(cb.outputSize);
    const vec3 d = cameraScreenUVToDirection(cb.camera, uv, false).worldDirection;
    Output[thread.xy] = vec4(lightShaft(d), 1.0f);
    return;
  }

  if (any(thread.xy >= cb.cubeSize))
  {
    return;
  }
  const uint face = thread.z;
  const vec3 d = cubeDirection(face, vec2(thread.xy) + 0.5f);
  vec3 result;
  if (cb.mode == 0)
  {
    // Visibility over the texel's four quarters, so silhouettes keep some coverage.
    float visible = 0.0f;
    [unroll]
    for (int q = 0; q < 4; q++)
    {
      visible += skyVisible(cubeDirection(face, vec2(thread.xy) + vec2(0.25f + 0.5f * float(q & 1), 0.25f + 0.5f * float(q >> 1))));
    }
    result = brightSource(d) * (visible * 0.25f);
  }
  else if (cb.mode == 1)
  {
    vec3 t, b;
    tangentFrame(d, t, b);
    result = vec3(0.0f);
    [unroll]
    for (int i = 0; i < 13; i++)
    {
      const vec3 tap = normalize(d + (t * kGauss5x5[i].x + b * kGauss5x5[i].y) * cb.texelAngle);
      result += Input.SampleLevel(tap, 0.0f).rgb * kGauss5x5[i].z;
    }
  }
  else
  {
    result = lightShaft(d);
  }
  OutputCube[uint3(thread.xy, face)] = vec4(result, 1.0f);
}
