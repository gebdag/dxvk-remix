// Fork: the haze region blend of each view Z texel (see post_fx_dark_souls_look.h), for the scene pass.
//
// The game hazes each draw with its region's DrawParams, and lights it with that region's ambient:
// the two are tuned together, so where two regions' surfaces meet the game shows no seam. Remix lights
// every surface with one continuous light, so the fog alone of two regions met with a hard line
// (Firelink's passage floor, frame firelink_line2: the passage set's dark fog against the outdoor
// set's light one). Each texel's haze is therefore a blend of the sets of the surfaces around it in
// the world: the weight of every surface point within cb.radius of the texel's own falls off with
// their distance in 3D, so a seam becomes a ramp 2 * radius wide, while surfaces behind a silhouette,
// or far apart (a distant region seen past a near one), never mix.

#include "rtx/pass/post_fx/post_fx_dark_souls_look.h"
#include "rtx/utility/common.slangh"
#include "rtx/concept/camera/camera.slangh"
#include "rtx/pass/post_fx/post_fx_motion_blur_geometry_flags.slangh"

layout(binding = POST_FX_DS_LOOK_REGION_LINEAR_VIEW_Z)
Texture2D<float> LinearViewZ;

layout(binding = POST_FX_DS_LOOK_REGION_SURFACE_FLAGS)
Texture2D<uint> PrimarySurfaceFlags;

layout(binding = POST_FX_DS_LOOK_REGION_HAZE)
ConstantBuffer<PostFxDarkSoulsHazeArgs> haze;

layout(binding = POST_FX_DS_LOOK_REGION_CONSTANTS)
ConstantBuffer<PostFxDarkSoulsLookRegionArgs> cb;

layout(binding = POST_FX_DS_LOOK_REGION_OUTPUT)
RWTexture2D<uint> Output;

// Packed for the scene pass (post_fx_dark_souls_look_scene.comp.slang ds1HazeRegionAt): the first set in
// bits 0-3, the second in 4-7, the second's share of their weight in 16-31.
uint ds1HazeRegionPack(uint first, uint second, float secondShare)
{
  return first | (second << 4) | (uint(saturate(secondShare) * 65535.0f + 0.5f) << 16);
}

uint hazeSetAt(uint2 texel)
{
  return (PrimarySurfaceFlags[texel] >> kPrimarySurfaceFlagDs1HazeSetShift) & 0xF;
}

vec3 viewPositionAt(uint2 texel, float viewZ)
{
  const vec2 uv = (vec2(texel) + 0.5f) / vec2(cb.size);
  const CameraDirections directions = cameraScreenUVToDirection(haze.camera, uv, false);
  return cameraReconstructViewPosition(haze.camera, directions.viewDirection, abs(viewZ));
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 texel : SV_DispatchThreadID)
{
  if (any(texel >= cb.size))
  {
    return;
  }

  const uint own = hazeSetAt(texel);
  const float viewZ = LinearViewZ[texel];
  if (cb.radius <= 0.0f || viewZ == haze.missLinearViewZ)
  {
    Output[texel] = ds1HazeRegionPack(own, own, 0.0f);
    return;
  }

  float weights[DS1_HAZE_SET_COUNT];
  for (uint i = 0; i < DS1_HAZE_SET_COUNT; ++i)
  {
    weights[i] = 0.0f;
  }
  weights[own] = 1.0f;

  // The disc the radius spans on screen at this depth (capped: right in front of the camera it would
  // cover the screen with taps too sparse to resolve anything near).
  const float z = abs(viewZ);
  const vec3 centre = viewPositionAt(texel, viewZ);
  const vec2 uvRadius = min(0.5f * cb.radius / z * vec2(abs(haze.camera.viewToProjection[0][0]), abs(haze.camera.viewToProjection[1][1])),
                            vec2(cb.maxUvRadius));
  const vec2 uv = (vec2(texel) + 0.5f) / vec2(cb.size);
  const float invRadiusSq = 1.0f / (cb.radius * cb.radius);
  // A Vogel disc, rotated per texel and frame (interleaved gradient noise): with one fixed pattern a
  // tap's set flips where its offset crosses a seam, the same texels apart everywhere, which stairs the
  // ramp. Rotated, the steps become noise that the scene pass's 3x3 and history average out.
  const vec2 noisePosition = vec2(texel) + 5.588238f * float(cb.frameIndex & 63);
  const float rotation = 6.28318531f * fract(52.9829189f * fract(dot(noisePosition, vec2(0.06711056f, 0.00583715f))));
  for (uint i = 0; i < cb.tapCount; ++i)
  {
    const float r = sqrt((float(i) + 0.5f) / float(cb.tapCount));
    const float angle = float(i) * 2.39996323f + rotation;
    const vec2 tapUv = uv + r * vec2(cos(angle), sin(angle)) * uvRadius;
    if (any(tapUv < 0.0f) || any(tapUv > 1.0f))
    {
      continue;
    }
    const uint2 tap = min(uint2(tapUv * vec2(cb.size)), cb.size - 1);
    const float tapViewZ = LinearViewZ[tap];
    if (tapViewZ == haze.missLinearViewZ)
    {
      continue;
    }
    const vec3 d = viewPositionAt(tap, tapViewZ) - centre;
    const float q = 1.0f - dot(d, d) * invRadiusSq;
    if (q > 0.0f)
    {
      weights[hazeSetAt(tap)] += q * q;
    }
  }

  // The surface's own set and the strongest other one (where three regions meet, the weakest drops
  // out). Both sides of a seam weigh the same two sets, so they blend alike and the ramp is
  // continuous. A small surface with a set of its own inside another region takes mostly its
  // surroundings' haze: the bonfire mound's teal set (c103 0.05, 0.11, 0.11) alone turned it teal,
  // where the game, lighting it far brighter, shows it yellow.
  uint other = own;
  float otherWeight = 0.0f;
  for (uint i = 0; i < DS1_HAZE_SET_COUNT; ++i)
  {
    if (i != own && weights[i] > otherWeight)
    {
      other = i;
      otherWeight = weights[i];
    }
  }
  Output[texel] = ds1HazeRegionPack(own, other, otherWeight / (weights[own] + otherWeight));
}
