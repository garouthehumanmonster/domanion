#ifndef RON_OVERLAYS
#define RON_OVERLAYS 0
#endif

#if RON_OVERLAYS

layout(std140, binding = BIND_UBO_OVERLAY) uniform OverlayBlock {
    vec4 ov_owner_color[16];
    vec4 ov_p0; // territory_on, fill_alpha, line_w0_m, line_w1_m
    vec4 ov_p1; // fog_on, fog_unexplored, fog_explored_dim, crater_on
    vec4 ov_p2; // world_size, inv_world, reserved, time_s
    vec4 ov_p3; // local_player, reserved, reserved, reserved
};

layout(binding = TEX_TERRITORY_SDF) uniform sampler2D u_territory_sdf;
layout(binding = TEX_TERRITORY_OWNER) uniform sampler2D u_territory_owner;
layout(binding = TEX_CRATER) uniform sampler2D u_crater;
layout(binding = TEX_FOG) uniform sampler2D u_fog;

float ovl_crater_height(vec2 xz) {
    if (ov_p1.w < 0.5) return 0.0;
    float v = textureLod(u_crater, xz * ov_p2.y, 0.0).r;
    return (v * 255.0 - 128.0) * 0.0625;
}

float ovl_scorch(vec2 xz) {
    if (ov_p1.w < 0.5) return 0.0;
    return textureLod(u_crater, xz * ov_p2.y, 0.0).g;
}

vec3 ovl_crater_normal(vec3 n, vec2 xz) {
    if (ov_p1.w < 0.5) return n;
    float e = 2.0;
    float hx = ovl_crater_height(xz + vec2(e, 0.0)) - ovl_crater_height(xz - vec2(e, 0.0));
    float hz = ovl_crater_height(xz + vec2(0.0, e)) - ovl_crater_height(xz - vec2(0.0, e));
    return normalize(n - vec3(hx, 0.0, hz) / (2.0 * e));
}

vec3 ovl_crater_albedo(vec3 a, vec2 xz) {
    float s = ovl_scorch(xz);
    return mix(a, a * vec3(0.22, 0.20, 0.18), s * 0.9);
}

vec3 ovl_territory(vec3 col, vec2 xz, float view_dist) {
    if (ov_p0.x < 0.5) return col;
    vec2 uv = xz * ov_p2.y;
    int owner = min(int(texelFetch(u_territory_owner, ivec2(clamp(floor(uv * 512.0), vec2(0.0), vec2(511.0))), 0).r * 255.0 + 0.5), 15);
    if (owner == 0) return col;
    vec3 oc = ov_owner_color[owner].rgb;
    col = mix(col, oc, ov_p0.y);
    float d = abs(texture(u_territory_sdf, uv).x);
    float s = clamp(view_dist / 400.0, 1.0, 6.0);
    float line = 1.0 - smoothstep(ov_p0.z * s, ov_p0.w * s, d);
    float glow = 0.30 * exp(-d / (14.0 * s));
    col = mix(col, oc * 1.25, clamp(line, 0.0, 1.0));
    return col + oc * glow * 0.35;
}

vec3 ovl_fog(vec3 col, vec2 xz) {
    if (ov_p1.x < 0.5) return col;
    vec2 f = texture(u_fog, xz * ov_p2.y).rg;
    float vis = f.x;
    float ex = max(f.y, f.x);
    float l = dot(col, vec3(0.299, 0.587, 0.114));
    vec3 dim = mix(vec3(l), col, 0.6) * ov_p1.z;
    return mix(vec3(0.0), mix(dim, col, vis), ex);
}

#endif // RON_OVERLAYS
