#version 460 core

#include "common/frame.glsl"
#include "common/world_overlay.glsl"
#include "common/terrain_common.glsl"

layout(location = 0) in vec3 v_world;
layout(location = 0) out vec4 out_color;

const float PI = 3.14159265358979323846;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main() {
    // 1. Water depth
    float depth = 120.0;
    if (v_world.x >= 0.0 && v_world.x < RON_WORLD_SIZE &&
        v_world.z >= 0.0 && v_world.z < RON_WORLD_SIZE) {
        vec2 uv = v_world.xz / RON_WORLD_SIZE;
        float h = height_at_uv(uv);
        float depth_fine = max(0.0, -h);
        float depth_coarse = textureLod(u_ocean_depth, uv, 0.0).r * u_tparams1.z; // ocean_depth_max = 120
        depth = max(depth_fine, depth_coarse);
    }

    // 2. Base colour & alpha
    vec3 base = mix(vec3(0.02, 0.30, 0.32), vec3(0.005, 0.045, 0.11), 1.0 - exp(-depth * 0.08));
    float alpha = clamp(1.0 - exp(-depth * 1.2), 0.35, 1.0);

    // 3. Waves (4 directional sines)
    // wavelengths 23, 11, 5.3, 2.7 m
    // normal slope amplitudes 0.06, 0.04, 0.025, 0.015
    // phase speed c = sqrt(9.81*2pi/wavelength)
    // amplitude / (1 + dist * k * 0.0025)
    float dist = length(v_world - u_cam_pos.xyz);

    const float wavelengths[4] = float[4](23.0, 11.0, 5.3, 2.7);
    const float amplitudes[4]  = float[4](0.06, 0.04, 0.025, 0.015);
    const vec2 dirs[4] = vec2[4](
        vec2(0.8944, 0.4472),
        vec2(-0.5547, 0.8321),
        vec2(0.3162, -0.9487),
        vec2(0.9806, -0.1961)
    );

    vec2 slope_vec = vec2(0.0);
    for (int i = 0; i < 4; ++i) {
        float wl = wavelengths[i];
        float k = 2.0 * PI / wl;
        float c = sqrt(9.81 * 2.0 * PI / wl);
        float phi = k * dot(dirs[i], v_world.xz) - c * u_time.x;
        float amp = amplitudes[i] / (1.0 + dist * k * 0.0025);
        slope_vec += dirs[i] * (amp * cos(phi));
    }

    vec3 unnorm_n = vec3(-slope_vec.x, 1.0, -slope_vec.y);
    float n_len = length(unnorm_n);
    vec3 n = (n_len > 1e-6) ? (unnorm_n / n_len) : vec3(0.0, 1.0, 0.0);

    // 4. View & Fresnel
    vec3 V = u_cam_pos.xyz - v_world;
    float v_dist = length(V);
    V = (v_dist > 1e-6) ? (V / v_dist) : vec3(0.0, 1.0, 0.0);
    float NdotV = clamp(dot(n, V), 0.0, 1.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);

    // 5. Sky reflection
    vec3 R = reflect(-V, n);
    vec3 sky = mix(vec3(0.55, 0.68, 0.85), vec3(0.18, 0.32, 0.65), clamp(R.y, 0.0, 1.0));

    // 6. Sun glint
    vec3 L = u_sun_dir.xyz;
    vec3 H = normalize(L + V);
    float NdotH = clamp(dot(n, H), 0.0, 1.0);
    float glint = pow(NdotH, 800.0) * u_sun_dir.w;
    vec3 sun_glint = u_sun_color.rgb * glint;

    vec3 color = mix(base, sky, fresnel) + sun_glint;

    // 7. Shore foam
    if (depth < 1.2) {
        float foam = (1.0 - depth / 1.2) * (0.5 + 0.5 * sin(depth * 9.0 - u_time.x * 1.6 + 6.28 * hash21(floor(v_world.xz * 0.8))));
        foam = clamp(foam, 0.0, 1.0);
        vec3 foam_color = vec3(0.9, 0.95, 1.0);
        color = mix(color, foam_color, foam);
        alpha = mix(alpha, 1.0, foam);
    }

    // 8. Distance fog
    float fog = 1.0 - exp(-v_dist * 0.0003);
    color = mix(color, vec3(0.50, 0.62, 0.80), fog);
    alpha = mix(alpha, 1.0, fog);

    // 9. Premultiplied alpha output
    vec3 rgb = color * alpha;
#if RON_OVERLAYS
    rgb = ovl_fog(rgb, v_world.xz);
#endif
    out_color = vec4(rgb, alpha);
}
