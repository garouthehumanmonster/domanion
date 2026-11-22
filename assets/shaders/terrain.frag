#version 460 core

#include "common/frame.glsl"
#include "common/terrain_common.glsl"
#include "common/world_overlay.glsl"

#if RON_LIT
#include "common/shadow_common.glsl"
#include "common/pbr_lite.glsl"
#endif

layout(location = 0) in vec3 v_world;
layout(location = 1) flat in float v_lod;

layout(location = 0) out vec4 out_color;

const float PI = 3.14159265358979323846;

void sample_layer(uint layer_idx, vec2 uv_det, vec2 uv_mac, out vec3 out_alb, out vec2 out_norm) {
    vec3 alb_det = texture(u_albedo_arr, vec3(uv_det, float(layer_idx))).rgb;
    vec3 alb_mac = texture(u_albedo_arr, vec3(uv_mac, float(layer_idx))).rgb;
    float luma_mac = dot(alb_mac, vec3(0.2126, 0.7152, 0.0722));
    out_alb = alb_det * (0.65 + 0.7 * luma_mac);
    out_norm = texture(u_normal_arr, vec3(uv_det, float(layer_idx))).rg;
}

void main() {
#if defined(TERRAIN_DEBUG_FLAT)
    out_color = vec4(1.0, 1.0, 1.0, 1.0);
    return;
#endif

    // Geometric normal via central differences
    float inv_n = u_tparams0.w;
    float cell = RON_WORLD_SIZE * inv_n;
    vec2 uv = v_world.xz / RON_WORLD_SIZE;

    float h_left  = height_at_uv(uv + vec2(-inv_n, 0.0));
    float h_right = height_at_uv(uv + vec2( inv_n, 0.0));
    float h_down  = height_at_uv(uv + vec2(0.0, -inv_n));
    float h_up    = height_at_uv(uv + vec2(0.0,  inv_n));

    vec3 g_norm = vec3(-(h_right - h_left), 2.0 * cell, -(h_up - h_down));
    float g_len = length(g_norm);
    vec3 Ng = (g_len > 1e-6) ? (g_norm / g_len) : vec3(0.0, 1.0, 0.0);

    // Biome 4-corner blend
    vec2 f = uv * vec2(u_tparams1.x, u_tparams1.y) - 0.5;
    vec2 i0 = floor(f);
    vec2 w = fract(f);
    ivec2 max_coord = ivec2(int(u_tparams1.x) - 1, int(u_tparams1.y) - 1);

    ivec2 c00 = clamp(ivec2(i0), ivec2(0), max_coord);
    ivec2 c10 = clamp(ivec2(i0.x + 1.0, i0.y), ivec2(0), max_coord);
    ivec2 c01 = clamp(ivec2(i0.x, i0.y + 1.0), ivec2(0), max_coord);
    ivec2 c11 = clamp(ivec2(i0.x + 1.0, i0.y + 1.0), ivec2(0), max_coord);

    uint b00 = uint(round(texelFetch(u_biome, c00, 0).r * 255.0));
    uint b10 = uint(round(texelFetch(u_biome, c10, 0).r * 255.0));
    uint b01 = uint(round(texelFetch(u_biome, c01, 0).r * 255.0));
    uint b11 = uint(round(texelFetch(u_biome, c11, 0).r * 255.0));

    uint l00 = biome_to_layer(b00);
    uint l10 = biome_to_layer(b10);
    uint l01 = biome_to_layer(b01);
    uint l11 = biome_to_layer(b11);

    vec2 uv_detail = v_world.xz / 12.0;
    vec2 uv_macro  = v_world.xz / 71.0;

    vec3 albedo = vec3(0.0);
    vec2 normal_rg = vec2(0.0);

    float w00 = (1.0 - w.x) * (1.0 - w.y);
    float w10 = w.x * (1.0 - w.y);
    float w01 = (1.0 - w.x) * w.y;
    float w11 = w.x * w.y;

    if (l00 == l10 && l00 == l01 && l00 == l11) {
        sample_layer(l00, uv_detail, uv_macro, albedo, normal_rg);
    } else {
        vec3 a00, a10, a01, a11;
        vec2 n00, n10, n01, n11;
        sample_layer(l00, uv_detail, uv_macro, a00, n00);
        sample_layer(l10, uv_detail, uv_macro, a10, n10);
        sample_layer(l01, uv_detail, uv_macro, a01, n01);
        sample_layer(l11, uv_detail, uv_macro, a11, n11);

        albedo = a00 * w00 + a10 * w10 + a01 * w01 + a11 * w11;
        normal_rg = n00 * w00 + n10 * w10 + n01 * w01 + n11 * w11;
    }

    // Slope override to rock layer
    float slope_rock_w = 1.0 - smoothstep(0.70, 0.82, clamp(Ng.y, 0.0, 1.0));
    if (slope_rock_w > 0.0) {
        uint rock_layer = uint(round(u_tparams0.y));
        vec3 rock_alb;
        vec2 rock_norm;
        sample_layer(rock_layer, uv_detail, uv_macro, rock_alb, rock_norm);
        albedo = mix(albedo, rock_alb, slope_rock_w);
        normal_rg = mix(normal_rg, rock_norm, slope_rock_w);
    }

#if defined(TERRAIN_DEBUG_LOD)
    const vec3 K_HUES[8] = vec3[8](
        vec3(1.0, 0.1, 0.1), // L0 - Red
        vec3(1.0, 0.5, 0.0), // L1 - Orange
        vec3(1.0, 0.9, 0.1), // L2 - Yellow
        vec3(0.1, 0.9, 0.1), // L3 - Green
        vec3(0.1, 0.9, 0.9), // L4 - Cyan
        vec3(0.1, 0.3, 1.0), // L5 - Blue
        vec3(0.6, 0.1, 0.9), // L6 - Purple
        vec3(1.0, 0.1, 0.7)  // L7 - Magenta
    );
    int lod_idx = clamp(int(round(v_lod)), 0, 7);
    albedo = mix(albedo, K_HUES[lod_idx], 0.85);
#endif

#if RON_OVERLAYS
    albedo = ovl_crater_albedo(albedo, v_world.xz);
#endif

    // Whiteout normal blend
    vec2 t_xy = normal_rg * 2.0 - 1.0;
    float t_z = sqrt(max(0.0, 1.0 - dot(t_xy, t_xy)));
    vec3 blended_n = vec3(Ng.x + t_xy.x, Ng.y * t_z, Ng.z + t_xy.y);
    float b_len = length(blended_n);
    vec3 n = (b_len > 1e-6) ? (blended_n / b_len) : vec3(0.0, 1.0, 0.0);

#if RON_OVERLAYS
    n = ovl_crater_normal(n, v_world.xz);
#endif

    // Lighting
#if RON_LIT
    vec3 L = u_sun_dir.xyz;
    float NdotL = max(dot(n, L), 0.0);
    float view_depth = dot(v_world - u_cam_pos.xyz, u_cam_fwd.xyz);
    float vis = calculate_shadow_visibility(v_world, n, L, view_depth, NdotL);
    vec3 V = normalize(u_cam_pos.xyz - v_world);
    vec3 sun_rgb = u_sun_color.rgb * u_sun_dir.w;
    float r00 = texture(u_rh_arr, vec3(uv_detail, float(l00))).g;
    float r10 = texture(u_rh_arr, vec3(uv_detail, float(l10))).g;
    float r01 = texture(u_rh_arr, vec3(uv_detail, float(l01))).g;
    float r11 = texture(u_rh_arr, vec3(uv_detail, float(l11))).g;
    float rough = r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11;
    if (slope_rock_w > 0.0) {
        uint rock_layer = uint(round(u_tparams0.y));
        float rock_rough = texture(u_rh_arr, vec3(uv_detail, float(rock_layer))).g;
        rough = mix(rough, rock_rough, slope_rock_w);
    }
    vec3 radiance = pbr_shade(albedo, rough, 0.0, 1.0, n, V, L, sun_rgb, vis, u_ambient_sky.rgb, u_ambient_ground.rgb, u_ambient_sky.w);
#else
    vec3 L = u_sun_dir.xyz;
    float NdotL = max(dot(n, L), 0.0);
    vec3 direct = u_sun_color.rgb * u_sun_dir.w * (NdotL / PI);
    vec3 ambient = mix(vec3(0.15, 0.13, 0.10), vec3(0.35, 0.45, 0.60), n.y * 0.5 + 0.5) * 0.6;
    vec3 radiance = albedo * (direct + ambient);
#endif

    // Fog
    float d = length(v_world - u_cam_pos.xyz);
    float fog = 1.0 - exp(-d * 0.0003);
    vec3 color = mix(radiance, vec3(0.50, 0.62, 0.80), fog);

#if RON_OVERLAYS
    color = ovl_territory(color, v_world.xz, length(u_cam_pos.xyz - v_world));
    color = ovl_fog(color, v_world.xz);
#endif

    out_color = vec4(color, 1.0);
}
