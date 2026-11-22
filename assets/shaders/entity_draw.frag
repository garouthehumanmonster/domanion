#version 460 core

#include "common/frame.glsl"
#include "entity_common.glsl"

#if RON_OVERLAYS
#include "common/world_overlay.glsl"
#endif

#if RON_LIT
#include "common/shadow_common.glsl"
#include "common/pbr_lite.glsl"
#endif

layout(binding = TEX_TRIM_ALBEDO)   uniform sampler2D u_trim_albedo;
layout(binding = TEX_TRIM_NORMAL)   uniform sampler2D u_trim_normal;
layout(binding = TEX_TRIM_ORM)      uniform sampler2D u_trim_orm;
layout(binding = TEX_TRIM_EMISSIVE) uniform sampler2D u_trim_emissive;

layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) flat in uint v_owner;
layout(location = 4) flat in uint v_flags;

layout(location = 0) out vec4 out_color;

#if RON_ENTITY_ALBEDO_DECODE
vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}
#endif

void main() {
    // 1. Albedo
    vec4 albedo_tex = texture(u_trim_albedo, v_uv);
    vec3 albedo = albedo_tex.rgb;
#if RON_ENTITY_ALBEDO_DECODE
    albedo = srgb_to_linear(albedo);
#endif

    // 2. Team colour tinting
    vec3 owner_c = K_OWNER_PALETTE[v_owner];
    float luma_a = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    float luma_o = dot(owner_c, vec3(0.2126, 0.7152, 0.0722));
    vec3 owner_preserved = owner_c * (luma_a / max(luma_o, 0.001));
    albedo = mix(albedo, owner_preserved, 0.30);

    // 3. Normal mapping via derivative tangent frame (Q6)
    vec3 dp1 = dFdx(v_world_pos);
    vec3 dp2 = dFdy(v_world_pos);
    vec2 duv1 = dFdx(v_uv);
    vec2 duv2 = dFdy(v_uv);
    vec3 T = dp1 * duv2.y - dp2 * duv1.y;
    vec3 B = dp2 * duv1.x - dp1 * duv2.x;
    vec3 N = normalize(v_world_normal);

    float len_T = length(T);
    float len_B = length(B);
    if (len_T > 1e-6 && len_B > 1e-6) {
        T = normalize(T - N * dot(N, T));
        B = normalize(B - N * dot(N, B) - T * dot(T, B));
    } else {
        vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        T = normalize(cross(up, N));
        B = cross(N, T);
    }

    vec2 norm_rg = texture(u_trim_normal, v_uv).rg * 2.0 - 1.0;
    float norm_z = sqrt(max(0.0, 1.0 - dot(norm_rg, norm_rg)));
    vec3 world_n = normalize(T * norm_rg.x + B * norm_rg.y + N * norm_z);

    // 4. ORM & Emissive
    vec3 orm = texture(u_trim_orm, v_uv).rgb;
    float ao = orm.r;
    float rough = orm.g;
    float metal = orm.b;
    float emissive = texture(u_trim_emissive, v_uv).r;

    // 5. Lighting matching terrain shader
#if RON_LIT
    vec3 L = u_sun_dir.xyz;
    float NdotL = max(dot(world_n, L), 0.0);
    float view_depth = dot(v_world_pos - u_cam_pos.xyz, u_cam_fwd.xyz);
    float vis = calculate_shadow_visibility(v_world_pos, world_n, L, view_depth, NdotL);
    vec3 V = normalize(u_cam_pos.xyz - v_world_pos);
    vec3 sun_rgb = u_sun_color.rgb * u_sun_dir.w;
    vec3 color = pbr_shade(albedo, rough, metal, ao, world_n, V, L, sun_rgb, vis, u_ambient_sky.rgb, u_ambient_ground.rgb, u_ambient_sky.w);
    color += albedo * emissive * 2.0;
#else
    vec3 L = u_sun_dir.xyz;
    float NdotL = max(dot(world_n, L), 0.0);
    vec3 direct = u_sun_color.rgb * u_sun_dir.w * (NdotL / 3.14159265);
    vec3 ambient = mix(vec3(0.18), vec3(0.32), world_n.y * 0.5 + 0.5) * ao;
    vec3 diff = albedo * direct * (1.0 - metal);
    vec3 amb = albedo * ambient;
    vec3 V = normalize(u_cam_pos.xyz - v_world_pos);
    vec3 H = normalize(L + V);
    float NdotH = max(dot(world_n, H), 0.0);
    float spec_power = mix(8.0, 128.0, 1.0 - rough);
    vec3 F0 = mix(vec3(0.04), albedo, metal);
    vec3 spec = u_sun_color.rgb * u_sun_dir.w * pow(NdotH, spec_power) * F0;
    vec3 color = diff + amb + spec + albedo * emissive * 2.0;
#endif

    // 6. FLAG_HIT_FLASH (bit 1)
    if ((v_flags & 2u) != 0u) {
        color = mix(color, vec3(1.0), 0.5);
    }

    // 7. Distance fog
    color = entity_distance_fog(color, v_world_pos, u_cam_pos.xyz);

    // 8. Overlays
#if RON_OVERLAYS
    color = ovl_fog(color, v_world_pos.xz);
#endif

    out_color = vec4(color, 1.0);
}
