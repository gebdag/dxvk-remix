#version 450

// dxvk_present_frag.frag for an HDR10 swap chain (DxvkSwapchainBlitter, rtx_hdr_output.h). The back
// buffer holds the frame gamma-encoded, with values above 1 for what is brighter than SDR white; the
// UI the game drew over it is in the same space, within 0..1. Keep in step with
// dxvk_present_frag_blit_hdr.frag (glslangValidator here has no include path).

layout(constant_id = 1) const bool s_gamma_bound = true;

layout(binding = 0) uniform sampler2D s_image;
layout(binding = 1) uniform sampler1D s_gamma;

layout(location = 0) out vec4 o_color;

layout(push_constant)
uniform present_info_t {
  ivec2 src_offset;
  ivec2 dst_offset;
  float paper_white_nits;
  float peak_nits;
  float shoulder_start;
  float decode_gamma;
};

// 0: sRGB piecewise; otherwise the exponent of a power curve (2.2).
vec3 hdr_decode(vec3 v, float gamma) {
  v = max(v, vec3(0.0));
  if (gamma > 0.0) {
    return pow(v, vec3(gamma));
  }
  vec3 lo = v / 12.92;
  vec3 hi = pow((v + 0.055) / 1.055, vec3(2.4));
  return mix(lo, hi, step(vec3(0.04045), v));
}

// BT.709 linear to BT.2020 linear.
vec3 hdr_709_to_2020(vec3 c) {
  const mat3 m = mat3(
    0.6274040, 0.0690970, 0.0163916,
    0.3292820, 0.9195400, 0.0880132,
    0.0433136, 0.0113612, 0.8955950);
  return m * c;
}

// SMPTE ST 2084 (PQ) of absolute nits.
vec3 hdr_pq(vec3 nits) {
  const float m1 = 0.1593017578125;
  const float m2 = 78.84375;
  const float c1 = 0.8359375;
  const float c2 = 18.8515625;
  const float c3 = 18.6875;
  vec3 y = pow(clamp(nits / 10000.0, vec3(0.0), vec3(1.0)), vec3(m1));
  return pow((c1 + c2 * y) / (1.0 + c3 * y), vec3(m2));
}

// Gamma-encoded frame value -> PQ-encoded BT.2020. paper_white: nits of 1.0. The luminance above
// shoulder_start * peak rolls off towards peak (hue kept), then nothing exceeds peak.
vec4 hdr_encode(vec4 v, float gamma, float paper_white, float peak, float shoulder_start) {
  vec3 lin = hdr_decode(v.rgb, gamma) * paper_white;
  float y = dot(lin, vec3(0.2126, 0.7152, 0.0722));
  float s = shoulder_start * peak;
  if (y > s && peak > s) {
    float range = peak - s;
    float rolled = s + range * (1.0 - exp(-(y - s) / range));
    lin *= rolled / max(y, 1e-5);
  }
  float m = max(lin.r, max(lin.g, lin.b));
  if (m > peak) {
    lin *= peak / m;
  }
  return vec4(hdr_pq(max(hdr_709_to_2020(lin), vec3(0.0))), v.a);
}

void main() {
  ivec2 coord = ivec2(gl_FragCoord.xy) + src_offset - dst_offset;
  vec4 c = texelFetch(s_image, coord, 0);

  // The game's gamma ramp covers 0..1; what is above SDR white passes.
  if (s_gamma_bound) {
    vec3 ramped = vec3(
      texture(s_gamma, c.r).r,
      texture(s_gamma, c.g).g,
      texture(s_gamma, c.b).b);
    c.rgb = mix(c.rgb, ramped, step(c.rgb, vec3(1.0)));
  }

  o_color = hdr_encode(c, decode_gamma, paper_white_nits, peak_nits, shoulder_start);
}
