#version 460 core

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(binding = TEX_HDR) uniform sampler2D u_hdr;
layout(location = 0) uniform float u_exposure;

void main() {
    vec3 c = texelFetch(u_hdr, ivec2(gl_FragCoord.xy), 0).rgb;
    c *= u_exposure;

#ifdef TONEMAP_PASSTHROUGH
    c = clamp(c, 0.0, 1.0);
#else
    c = max(c, vec3(0.0));
    c = c / (vec3(1.0) + c);
#endif

    // Exact sRGB encode
    vec3 srgb;
    srgb.r = (c.r <= 0.0031308) ? (12.92 * c.r) : (1.055 * pow(c.r, 1.0 / 2.4) - 0.055);
    srgb.g = (c.g <= 0.0031308) ? (12.92 * c.g) : (1.055 * pow(c.g, 1.0 / 2.4) - 0.055);
    srgb.b = (c.b <= 0.0031308) ? (12.92 * c.b) : (1.055 * pow(c.b, 1.0 / 2.4) - 0.055);

    o_color = vec4(srgb, 1.0);
}
