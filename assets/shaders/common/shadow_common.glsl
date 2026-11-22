#ifndef SHADOW_COMMON_GLSL
#define SHADOW_COMMON_GLSL

layout(std140, binding = BIND_UBO_LIGHT) uniform LightingBlock {
    mat4 u_light_vp[4];       // offset 0 (256 B): 4 cascade matrices
    vec4 u_cascade_split;     // offset 256 (16 B): splits z_1, z_2, z_3, z_4
    vec4 u_shadow_texel;      // offset 272 (16 B): texel_world[0..3]
    vec4 u_inv_range;         // offset 288 (16 B): inv_range[0..3]
    vec4 u_cam_fwd;           // offset 304 (16 B): xyz, flags (float-encoded int)
    vec4 u_ambient_sky;       // offset 320 (16 B): rgb, intensity
    vec4 u_ambient_ground;    // offset 336 (16 B): rgb, 0
    vec4 u_shadow_p;          // offset 352 (16 B): pcf_radius (1.0), 48.0, 1100.0, 0.0
};

layout(binding = TEX_SHADOW_MAP) uniform sampler2DArrayShadow u_shadow_map;
layout(binding = TEX_HEIGHT_LINEAR) uniform sampler2D u_height_linear;

// CSM cascade selection based on view-space depth (positive distance from camera)
int select_cascade(float depth) {
    if (depth < 1.0) return -1;
    if (depth < u_cascade_split.x) return 0;
    if (depth < u_cascade_split.y) return 1;
    if (depth < u_cascade_split.z) return 2;
    if (depth < u_cascade_split.w) return 3;
    return -1;
}

// 3x3 PCF sample on shadow map array with normal offset and cascade 3 fade
float sample_csm_shadow(vec3 world_pos, float depth, vec3 N, float NdotL) {
    uint flags = uint(round(u_cam_fwd.w));
    if ((flags & 1u) == 0u) {
        return 1.0;
    }

    int c = select_cascade(depth);
    if (c < 0) return 1.0;

    vec3 p = world_pos + N * (u_shadow_texel[c] * 2.0);
    vec4 clip = u_light_vp[c] * vec4(p, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 1.0;
    }

    // Bias (Reverse-Z): pulls receiver toward light (larger Z in Reverse-Z)
    float ref = ndc.z + u_shadow_texel[c] * 1.5 * u_inv_range[c];

    // 3x3 PCF filter
    vec2 texel_sz = vec2(1.0 / float(textureSize(u_shadow_map, 0).x));
    float vis = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            vec2 tap_uv = uv + vec2(float(dx), float(dy)) * texel_sz;
            vis += texture(u_shadow_map, vec4(tap_uv, float(c), ref));
        }
    }
    vis /= 9.0;

    // Fade on cascade 3
    if (c == 3) {
        vis = mix(vis, 1.0, smoothstep(0.85 * u_cascade_split.w, u_cascade_split.w, depth));
    }
    return vis;
}

// 3-arg overload for callers passing (world_pos, depth, NdotL)
float sample_csm_shadow(vec3 world_pos, float depth, float NdotL) {
    return sample_csm_shadow(world_pos, depth, vec3(0.0, 1.0, 0.0), NdotL);
}

// Terrain heightfield ray march matching terrain_shadow_ref
float sample_terrain_shadow(vec3 world_pos, vec3 N, vec3 L) {
    uint flags = uint(round(u_cam_fwd.w));
    if ((flags & 2u) == 0u) {
        return 1.0;
    }

    if (L.y <= 0.01) return 0.0;

    float lxz = length(L.xz);
    if (lxz < 1e-4) return 1.0;

    float tanE = L.y / lxz;
    vec2 dir_xz = L.xz / lxz;

    float occ = 0.0;
    const int steps = 48;
    for (int i = 1; i <= steps; ++i) {
        float d = 1100.0 * pow(float(i) / float(steps), 1.5);
        vec2 q = clamp(world_pos.xz + dir_xz * d, 0.0, 4096.0);
        float h = texture(u_height_linear, q / 4096.0).r * 640.0 - 128.0;
        float h_ray = world_pos.y + 0.5 + d * tanE;
        float pen = 0.05 * d + 0.75;
        float o_i = clamp((h - h_ray) / pen + 0.5, 0.0, 1.0);
        occ = max(occ, o_i);
        if (occ >= 1.0) break;
    }

    return 1.0 - occ;
}

// Combined shadow visibility
float calculate_shadow_visibility(vec3 world_pos, vec3 N, vec3 L, float view_depth, float NdotL) {
    uint flags = uint(round(u_cam_fwd.w));
    float csm = ((flags & 1u) != 0u) ? sample_csm_shadow(world_pos, view_depth, N, NdotL) : 1.0;
    float trn = ((flags & 2u) != 0u) ? sample_terrain_shadow(world_pos, N, L) : 1.0;
    return csm * trn;
}

#endif // SHADOW_COMMON_GLSL
