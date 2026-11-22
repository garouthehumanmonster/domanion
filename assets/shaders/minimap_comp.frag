#version 460 core

#define RON_OVERLAYS 1
#include "common/world_overlay.glsl"

layout(std140, binding = BIND_UBO_MINIMAP) uniform MinimapBlock {
    vec4 mm_quad[2]; // BL.x, BL.z, BR.x, BR.z, TR.x, TR.z, TL.x, TL.z
    vec4 mm_p;       // fog_on, blip_count, time_s, line_px
};

layout(binding = TEX_MINIMAP_BASE) uniform sampler2D u_minimap_base;

out vec4 frag_color;

float dist_to_seg(vec2 p, vec2 a, vec2 b) {
    vec2 ba = b - a;
    vec2 pa = p - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-4), 0.0, 1.0);
    return length(pa - ba * h);
}

void main() {
    ivec2 coord = ivec2(gl_FragCoord.xy);
    vec2 xz = (vec2(coord) + 0.5) / 512.0 * 4096.0;

    vec3 col = texelFetch(u_minimap_base, coord, 0).rgb;
    col = ovl_territory(col, xz, 800.0);
    if (mm_p.x > 0.5) {
        col = ovl_fog(col, xz);
    }

    vec2 bl = mm_quad[0].xy / 8.0;
    vec2 br = mm_quad[0].zw / 8.0;
    vec2 tr = mm_quad[1].xy / 8.0;
    vec2 tl = mm_quad[1].zw / 8.0;

    vec2 p = gl_FragCoord.xy;
    float d0 = dist_to_seg(p, bl, br);
    float d1 = dist_to_seg(p, br, tr);
    float d2 = dist_to_seg(p, tr, tl);
    float d3 = dist_to_seg(p, tl, bl);
    float dist = min(min(d0, d1), min(d2, d3));

    float line_val = 1.0 - smoothstep(0.6, 1.4, dist);
    col = mix(col, vec3(1.0), line_val);

    frag_color = vec4(col, 1.0);
}
