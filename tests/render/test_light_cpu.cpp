#include "test_framework.hpp"
#include "render/gl_fixture.hpp"
#include "render/scene_fixture.hpp"
#include "render/light_types.hpp"
#include "render/cascade_math.hpp"
#include "render/shadow_ref.hpp"
#include "render/pbr_ref.hpp"
#include "render/marks_math.hpp"
#include "render/icon_map.hpp"
#include "render/ui_source.hpp"
#include "render/entity_source.hpp"
#include "render/overlay_types.hpp"
#include "render/rmath.hpp"
#include "render/camera_rts.hpp"
#include "render/frame_fill.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>

namespace ron::render {

inline Vec3 mul3(const Vec3& a, const Vec3& b) noexcept {
    return Vec3(a.x * b.x, a.y * b.y, a.z * b.z);
}

// -----------------------------------------------------------------------------
// Test 1: Lit_Layout
// -----------------------------------------------------------------------------
RON_TEST(Lit_Layout) {
    RON_CHECK_EQ(sizeof(LightingUbo), 368u);
    RON_CHECK_EQ(alignof(LightingUbo), 16u);
    RON_CHECK_EQ(sizeof(MarksUbo), 832u);
    RON_CHECK_EQ(alignof(MarksUbo), 16u);

    // Offsets of LightingUbo
    RON_CHECK_EQ(offsetof(LightingUbo, light_vp), 0u);
    RON_CHECK_EQ(offsetof(LightingUbo, splits), 256u);
    RON_CHECK_EQ(offsetof(LightingUbo, texel), 272u);
    RON_CHECK_EQ(offsetof(LightingUbo, inv_range), 288u);
    RON_CHECK_EQ(offsetof(LightingUbo, cam_fwd), 304u);
    RON_CHECK_EQ(offsetof(LightingUbo, ambient_sky), 320u);
    RON_CHECK_EQ(offsetof(LightingUbo, ambient_ground), 336u);
    RON_CHECK_EQ(offsetof(LightingUbo, shadow_p), 352u);

    // Offsets of MarksUbo
    RON_CHECK_EQ(offsetof(MarksUbo, icon_p), 0u);
    RON_CHECK_EQ(offsetof(MarksUbo, bar_p), 16u);
    RON_CHECK_EQ(offsetof(MarksUbo, screen), 32u);
    RON_CHECK_EQ(offsetof(MarksUbo, atlas), 48u);
    RON_CHECK_EQ(offsetof(MarksUbo, owner_color), 64u);
    RON_CHECK_EQ(offsetof(MarksUbo, icon_uv), 320u);

    // Constants verification
    RON_CHECK_EQ(SHADOW_CASCADES, 4u);
    RON_CHECK_EQ(SPLIT_LAMBDA, 0.80f);
    RON_CHECK_EQ(SHADOW_NEAR, 1.0f);
    RON_CHECK_EQ(SHADOW_DISTANCE, 1500.0f);
    RON_CHECK_EQ(CASCADE_Z_UP, 700.0f);
    RON_CHECK_EQ(CASCADE_Z_DOWN, 400.0f);
    RON_CHECK_EQ(TERRAIN_SHADOW_REACH, 1100.0f);
    RON_CHECK_EQ(TERRAIN_SHADOW_BIAS, 0.5f);
    RON_CHECK_EQ(ENTITY_FRUSTUM_MARGIN, 48.0f);

    // New binding defines verification
    RON_CHECK_EQ(BIND_UBO_LIGHT, 5u);
    RON_CHECK_EQ(BIND_UBO_MARKS, 6u);
    RON_CHECK_EQ(BIND_SSBO_ICONS, 11u);
    RON_CHECK_EQ(BIND_SSBO_BARS, 12u);
    RON_CHECK_EQ(BIND_SSBO_MARK_CMDS, 13u);
    RON_CHECK_EQ(BIND_SSBO_MODEL_MARKS, 14u);
    RON_CHECK_EQ(TEX_SHADOW_MAP, 19u);
    RON_CHECK_EQ(TEX_HEIGHT_LINEAR, 20u);
    RON_CHECK_EQ(TEX_MSDF, 21u);

    // GL Context checks
    GL_TEST_BEGIN();
    GLint max_ssbo = 0;
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &max_ssbo);
    RON_CHECK(max_ssbo >= 15);
    GL_TEST_END();
}

// -----------------------------------------------------------------------------
// Test 2: Cascade_Splits
// -----------------------------------------------------------------------------
RON_TEST(Cascade_Splits) {
    float splits[5] = {0.0f};
    cascade_splits(1.0f, 1500.0f, 0.80f, splits);

    RON_CHECK(std::abs(splits[0] - 1.0f) < 1e-4f);
    RON_CHECK(std::abs(splits[4] - 1500.0f) < 1e-4f);

    // Strictly increasing
    for (int i = 0; i < 4; ++i) {
        RON_CHECK(splits[i] < splits[i + 1]);
    }

    // Verify against formula: z_i = lerp(n + (f - n)*i/4, n * (f/n)^(i/4), 0.80)
    for (int i = 0; i <= 4; ++i) {
        float fi = static_cast<float>(i) / 4.0f;
        float linear = 1.0f + 1499.0f * fi;
        float log_val = 1.0f * std::pow(1500.0f, fi);
        float expected = linear * 0.20f + log_val * 0.80f;
        RON_CHECK(std::abs(splits[i] - expected) < 1e-4f);
    }

    // Cascade select boundary checks
    CascadeSet cs{};
    std::memcpy(cs.split, splits, sizeof(splits));

    RON_CHECK_EQ(cascade_select(cs, 0.5f), 0);
    RON_CHECK_EQ(cascade_select(cs, splits[1] - 0.001f), 0);
    RON_CHECK_EQ(cascade_select(cs, splits[1]), 1);
    RON_CHECK_EQ(cascade_select(cs, splits[2] - 0.001f), 1);
    RON_CHECK_EQ(cascade_select(cs, splits[2]), 2);
    RON_CHECK_EQ(cascade_select(cs, splits[3] - 0.001f), 2);
    RON_CHECK_EQ(cascade_select(cs, splits[3]), 3);
    RON_CHECK_EQ(cascade_select(cs, 1499.9f), 3);
    RON_CHECK_EQ(cascade_select(cs, splits[4]), -1);
    RON_CHECK_EQ(cascade_select(cs, 1500.1f), -1);
}

// -----------------------------------------------------------------------------
// Test 3: Cascade_Fit
// -----------------------------------------------------------------------------
RON_TEST(Cascade_Fit) {
    auto& scene = test::test_scene();
    scene.init();

    // Camera 1: focus = flattest point, dist 300, yaw 0.4
    Vec3 site0 = test::find_flattest_point(test::test_sampler());

    CameraRts cam1;
    camera_init(cam1, site0.x, site0.z, 0.4f, 300.0f);
    camera_snap(cam1, &test::test_sampler());

    float aspect = 640.0f / 360.0f;
    Mat4 view1 = camera_view(cam1, &test::test_sampler());
    Mat4 proj = camera_proj(aspect);
    Vec3 eye1 = camera_eye(cam1, &test::test_sampler());

    FrameUniforms fu1{};
    fill_frame_uniforms(fu1, view1, proj, eye1, 640, 360, 0.0f, 0.0f, 1.0f,
                        Vec3{0.30f, 0.70f, 0.65f}, 3.0f, Vec3{1.0f, 0.95f, 0.85f});

    set_sun(fu1, 40.0f, 135.0f);
    float L[3];
    light_to_sun(fu1, L);

    CascadeSet cs1{};
    cascade_fit(fu1, L, 2048, &cs1);

    // View basis components to rebuild corners
    Vec3 right(fu1.view[0], fu1.view[4], fu1.view[8]); right = right.normalize();
    Vec3 up(fu1.view[1], fu1.view[5], fu1.view[9]);       up = up.normalize();
    Vec3 fwd(-fu1.view[2], -fu1.view[6], -fu1.view[10]); fwd = fwd.normalize();
    float tx = 1.0f / fu1.proj[0];
    float ty = 1.0f / fu1.proj[5];

    // For each cascade: check slice corners NDC and texel snapping
    for (int c = 0; c < 4; ++c) {
        // texel_world == 2*radius / SHADOW_RES
        float expected_T = 2.0f * cs1.radius[c] / 2048.0f;
        RON_CHECK(std::abs(cs1.texel_world[c] - expected_T) < 1e-5f);

        // centre_ls x and y are integer multiples of texel_world (within 1e-3 texel)
        float rx = cs1.centre_ls[c][0] / cs1.texel_world[c];
        float ry = cs1.centre_ls[c][1] / cs1.texel_world[c];
        RON_CHECK(std::abs(rx - std::round(rx)) < 1e-3f);
        RON_CHECK(std::abs(ry - std::round(ry)) < 1e-3f);

        // 8 slice corners
        float z_near = cs1.split[c];
        float z_far  = cs1.split[c + 1];
        float depths[2] = { z_near, z_far };
        for (float z : depths) {
            float hx = tx * z;
            float hy = ty * z;
            Vec3 p_c = eye1 + fwd * z;
            Vec3 corners[4] = {
                p_c - right * hx - up * hy,
                p_c + right * hx - up * hy,
                p_c - right * hx + up * hy,
                p_c + right * hx + up * hy
            };
            for (const auto& corner : corners) {
                // Transform with light_vp column-major:
                // x_ndc = m[0]*p.x + m[4]*p.y + m[8]*p.z + m[12]
                // y_ndc = m[1]*p.x + m[5]*p.y + m[9]*p.z + m[13]
                // z_ndc = m[2]*p.x + m[6]*p.y + m[10]*p.z + m[14]
                const float* m = cs1.light_vp[c];
                float x_ndc = m[0] * corner.x + m[4] * corner.y + m[8]  * corner.z + m[12];
                float y_ndc = m[1] * corner.x + m[5] * corner.y + m[9]  * corner.z + m[13];
                float z_ndc = m[2] * corner.x + m[6] * corner.y + m[10] * corner.z + m[14];

                float tol_xy = 1.0f + 1.5f / 2048.0f + 1e-3f;
                RON_CHECK(std::abs(x_ndc) <= tol_xy);
                RON_CHECK(std::abs(y_ndc) <= tol_xy);
                RON_CHECK(z_ndc >= -1e-3f && z_ndc <= 1.0f + 1e-3f);
            }
        }
    }

    // radius strictly increasing
    for (int c = 0; c < 3; ++c) {
        RON_CHECK(cs1.radius[c] < cs1.radius[c + 1]);
    }

    // Camera 2: focus moved 777 m, dist 90, yaw 2.0
    CameraRts cam2;
    camera_init(cam2, site0.x + 777.0f, site0.z + 777.0f, 2.0f, 90.0f);
    camera_snap(cam2, &test::test_sampler());
    Mat4 view2 = camera_view(cam2, &test::test_sampler());
    Vec3 eye2 = camera_eye(cam2, &test::test_sampler());

    FrameUniforms fu2{};
    fill_frame_uniforms(fu2, view2, proj, eye2, 640, 360, 0.0f, 0.0f, 1.0f,
                        Vec3{0.30f, 0.70f, 0.65f}, 3.0f, Vec3{1.0f, 0.95f, 0.85f});
    set_sun(fu2, 40.0f, 135.0f);

    CascadeSet cs2{};
    cascade_fit(fu2, L, 2048, &cs2);

    // radius identical within 1e-4, light_vp differs
    bool vp_differs = false;
    for (int c = 0; c < 4; ++c) {
        RON_CHECK(std::abs(cs1.radius[c] - cs2.radius[c]) < 1e-4f);
        for (int k = 0; k < 16; ++k) {
            if (std::abs(cs1.light_vp[c][k] - cs2.light_vp[c][k]) > 1e-3f) {
                vp_differs = true;
            }
        }
    }
    RON_CHECK(vp_differs);
}

// -----------------------------------------------------------------------------
// Test 4: Shadow_Ref
// -----------------------------------------------------------------------------
RON_TEST(Shadow_Ref) {
    const auto& hs = test::test_sampler();

    // 3000 deterministic land points (height >= 2.0m, y = height)
    std::vector<Vec3> pts;
    pts.reserve(3000);
    for (int ix = 0; ix < 100 && pts.size() < 3000; ++ix) {
        for (int iz = 0; iz < 100 && pts.size() < 3000; ++iz) {
            float x = 200.0f + static_cast<float>(ix) * 37.0f;
            float z = 200.0f + static_cast<float>(iz) * 37.0f;
            float h = hs.height_m(x, z);
            if (h >= 2.0f) {
                pts.push_back(Vec3{x, h, z});
            }
        }
    }
    RON_CHECK(pts.size() >= 3000);

    // (a) L=(0,1,0): all results == 1
    float L_up[3] = {0.0f, 1.0f, 0.0f};
    for (size_t i = 0; i < 3000; ++i) {
        float s = terrain_shadow_ref(hs, pts[i].x, pts[i].y, pts[i].z, L_up, 48);
        RON_CHECK_EQ(s, 1.0f);
    }

    // (b) L.y = 0.005: all == 0
    float L_low[3] = {0.7071f, 0.005f, 0.7071f};
    for (size_t i = 0; i < 3000; ++i) {
        float s = terrain_shadow_ref(hs, pts[i].x, pts[i].y, pts[i].z, L_low, 48);
        RON_CHECK_EQ(s, 0.0f);
    }

    // (c) Sun elevations {60, 30, 15, 8, 4} deg: shadowed fraction non-decreasing
    const float elevs[5] = {60.0f, 30.0f, 15.0f, 8.0f, 4.0f};
    float frac_shadowed[5] = {0.0f};

    for (int e = 0; e < 5; ++e) {
        FrameUniforms fu{};
        set_sun(fu, elevs[e], 135.0f);
        float L_e[3];
        light_to_sun(fu, L_e);

        int count_shadowed = 0;
        for (size_t i = 0; i < 3000; ++i) {
            float s = terrain_shadow_ref(hs, pts[i].x, pts[i].y, pts[i].z, L_e, 48);

            // (d) Result always in [0, 1] and finite
            RON_CHECK(s >= 0.0f && s <= 1.0f);
            RON_CHECK(std::isfinite(s));

            if (s < 0.5f) count_shadowed++;

            // (e) Raising y by 50 m never decreases the result
            float s_raised = terrain_shadow_ref(hs, pts[i].x, pts[i].y + 50.0f, pts[i].z, L_e, 48);
            RON_CHECK(s_raised >= s - 1e-5f);
        }
        frac_shadowed[e] = static_cast<float>(count_shadowed) / 3000.0f;
    }

    for (int e = 0; e < 4; ++e) {
        RON_CHECK(frac_shadowed[e] <= frac_shadowed[e + 1] + 1e-4f);
    }
    // At 4 deg, shadowed fraction is >= 3% and <= 95%
    RON_CHECK(frac_shadowed[4] >= 0.03f && frac_shadowed[4] <= 0.95f);
}

// -----------------------------------------------------------------------------
// Test 5: Pbr_Ref
// -----------------------------------------------------------------------------
RON_TEST(Pbr_Ref) {
    constexpr float PI = 3.14159265358979323846f;

    // (a) ggx_d integrates: sum over fine hemisphere grid within 1% of 1 for rough 0.3, 0.6, 1.0
    const float test_roughs[3] = {0.3f, 0.6f, 1.0f};
    for (float rough : test_roughs) {
        float a = rough * rough;
        float a2 = a * a;
        double sum = 0.0;
        int n_theta = 500;
        double d_theta = (PI * 0.5) / n_theta;

        for (int it = 0; it < n_theta; ++it) {
            double theta = (it + 0.5) * d_theta;
            float cos_th = static_cast<float>(std::cos(theta));
            float sin_th = static_cast<float>(std::sin(theta));
            float D = ggx_d(cos_th, a2);
            sum += D * cos_th * sin_th * d_theta * (2.0 * PI);
        }
        RON_CHECK(std::abs(sum - 1.0) < 0.01);
    }

    // (b) Fresnel at VH=1 equals F0, at VH=0 equals 1
    Vec3 F0(0.04f, 0.04f, 0.04f);
    Vec3 f1 = fresnel_schlick(F0, 1.0f);
    Vec3 f0 = fresnel_schlick(F0, 0.0f);
    RON_CHECK(std::abs(f1.x - 0.04f) < 1e-5f);
    RON_CHECK(std::abs(f0.x - 1.0f) < 1e-5f);

    // (c) pbr_shade is finite and >= 0 over 20000 random inputs
    Vec3 sky(0.38f, 0.52f, 0.78f);
    Vec3 ground(0.20f, 0.18f, 0.15f);
    Vec3 sun(3.0f, 2.85f, 2.55f);

    u32 rng = 202613;
    auto next_f = [&rng]() -> float {
        rng = rng * 1664525u + 1013904223u;
        return static_cast<float>(rng) / 4294967296.0f;
    };

    for (int i = 0; i < 20000; ++i) {
        Vec3 alb(next_f(), next_f(), next_f());
        float r = next_f();
        float m = next_f();
        float ao = next_f();
        Vec3 N = Vec3(next_f() * 2.0f - 1.0f, next_f(), next_f() * 2.0f - 1.0f).normalize();
        Vec3 V = Vec3(next_f() * 2.0f - 1.0f, next_f(), next_f() * 2.0f - 1.0f).normalize();
        Vec3 L = Vec3(next_f() * 2.0f - 1.0f, next_f(), next_f() * 2.0f - 1.0f).normalize();
        float vis = next_f();

        Vec3 col = pbr_shade(alb, r, m, ao, N, V, L, sun, vis, sky, ground, 0.55f);
        RON_CHECK(std::isfinite(col.x) && std::isfinite(col.y) && std::isfinite(col.z));
        RON_CHECK(col.x >= 0.0f && col.y >= 0.0f && col.z >= 0.0f);
    }

    // (d) Doubling sun_rgb exactly doubles direct part; vis=0 removes direct part
    Vec3 N(0.0f, 1.0f, 0.0f);
    Vec3 V(0.0f, 0.7071f, 0.7071f);
    Vec3 L(0.0f, 0.7071f, -0.7071f);
    Vec3 alb(0.5f, 0.5f, 0.5f);

    Vec3 dir1 = pbr_shade(alb, 0.5f, 0.0f, 1.0f, N, V, L, sun, 1.0f, sky, ground, 0.0f);
    Vec3 dir2 = pbr_shade(alb, 0.5f, 0.0f, 1.0f, N, V, L, sun * 2.0f, 1.0f, sky, ground, 0.0f);
    RON_CHECK(std::abs(dir2.x - dir1.x * 2.0f) < 1e-4f);
    RON_CHECK(std::abs(dir2.y - dir1.y * 2.0f) < 1e-4f);

    Vec3 dir0 = pbr_shade(alb, 0.5f, 0.0f, 1.0f, N, V, L, sun, 0.0f, sky, ground, 0.0f);
    RON_CHECK(dir0.x < 1e-6f && dir0.y < 1e-6f && dir0.z < 1e-6f);

    // (e) ao scales ambient part linearly
    Vec3 amb1 = pbr_shade(alb, 0.5f, 0.0f, 1.0f, N, V, L, sun, 0.0f, sky, ground, 0.55f);
    Vec3 amb_half = pbr_shade(alb, 0.5f, 0.0f, 0.5f, N, V, L, sun, 0.0f, sky, ground, 0.55f);
    RON_CHECK(std::abs(amb_half.x - amb1.x * 0.5f) < 1e-5f);

    // (f) metal=1 gives zero diffuse term
    // pbr_shade with metal=1, albedo=(1,1,1), sky_i=0 -> only specular direct
    Vec3 spec_only = pbr_shade(Vec3(1.0f, 1.0f, 1.0f), 0.5f, 1.0f, 1.0f, N, V, L, sun, 1.0f, sky, ground, 0.0f);
    // Expected specular: spec = D * Vis * F * NL * PI * sun_rgb * vis
    float rc = 0.5f; float a = rc*rc; float a2 = a*a;
    float NL = std::clamp(N.dot(L), 0.0f, 1.0f);
    float NV = std::max(N.dot(V), 1e-4f);
    Vec3 H = (L + V).normalize();
    float NH = std::clamp(N.dot(H), 0.0f, 1.0f);
    float VH = std::clamp(V.dot(H), 0.0f, 1.0f);
    float D_val = ggx_d(NH, a2);
    float Vis_val = smith_vis(NL, NV, a2);
    Vec3 F_val = fresnel_schlick(Vec3(1.0f, 1.0f, 1.0f), VH);
    Vec3 exp_spec = mul3(F_val * (D_val * Vis_val * NL * PI), sun);
    RON_CHECK(std::abs(spec_only.x - exp_spec.x) < 1e-5f);

    // (g) Monotone non-decreasing in NL for 64-step sweep
    float prev_val = -1.0f;
    for (int step = 0; step < 64; ++step) {
        float angle = static_cast<float>(step) * (PI * 0.5f / 63.0f);
        Vec3 L_sweep(std::cos(angle), std::sin(angle), 0.0f);
        Vec3 res = pbr_shade(Vec3(0.5f, 0.5f, 0.5f), 1.0f, 0.0f, 1.0f, N, V, L_sweep, sun, 1.0f, sky, ground, 0.0f);
        RON_CHECK(res.x >= prev_val - 1e-5f);
        prev_val = res.x;
    }
}

// -----------------------------------------------------------------------------
// Test 6: Marks_Math
// -----------------------------------------------------------------------------
RON_TEST(Marks_Math) {
    // 1. icon_alpha
    RON_CHECK_EQ(icon_alpha(20.0f), 0.0f);
    RON_CHECK_EQ(icon_alpha(25.0f), 0.0f);
    RON_CHECK_EQ(icon_alpha(8.0f), 1.0f);
    RON_CHECK_EQ(icon_alpha(5.0f), 1.0f);
    // Monotone between 8 and 20
    float prev_a = 1.0f;
    for (float p = 8.5f; p < 20.0f; p += 0.5f) {
        float a = icon_alpha(p);
        RON_CHECK(a <= prev_a);
        prev_a = a;
    }

    // 2. bar_color
    float col_g[3], col_y[3], col_r[3];
    bar_color(1.0f, col_g);
    bar_color(0.5f, col_y);
    bar_color(0.0f, col_r);
    RON_CHECK(std::abs(col_g[0] - 0.20f) < 1e-6f && std::abs(col_g[1] - 0.85f) < 1e-6f && std::abs(col_g[2] - 0.25f) < 1e-6f);
    RON_CHECK(std::abs(col_y[0] - 0.95f) < 1e-6f && std::abs(col_y[1] - 0.85f) < 1e-6f && std::abs(col_y[2] - 0.15f) < 1e-6f);
    RON_CHECK(std::abs(col_r[0] - 0.90f) < 1e-6f && std::abs(col_r[1] - 0.15f) < 1e-6f && std::abs(col_r[2] - 0.12f) < 1e-6f);

    // 3. bar_layout width clamps to 14 and 64
    BarRect r_min = bar_layout(100.0f, 100.0f, 2.0f, 1.0f); // 2*2=4 -> clamps to 14
    RON_CHECK_EQ(r_min.w, 14.0f);
    BarRect r_max = bar_layout(100.0f, 100.0f, 50.0f, 1.0f); // 2*50=100 -> clamps to 64
    RON_CHECK_EQ(r_max.w, 64.0f);

    // 4. marks_cull_reference on hand-built instances
    EntityUbo ubo{};
    ubo.counts[1] = 71; // model_count
    ubo.cull_p[0] = 600.0f; // proj_scale_px
    ubo.cam_alpha[0] = 0.0f; ubo.cam_alpha[1] = 0.0f; ubo.cam_alpha[2] = 0.0f; ubo.cam_alpha[3] = 1.0f;
    // Set 6 frustum planes (looking along -Z)
    // Reverse-Z ZERO_TO_ONE: planes normal pointing inward
    // L (+X), R (-X), B (+Y), T (-Y), N (-Z), F (+Z)
    ubo.planes[0][0] =  1.0f; ubo.planes[0][1] = 0.0f; ubo.planes[0][2] = 0.0f; ubo.planes[0][3] = 1000.0f; // L
    ubo.planes[1][0] = -1.0f; ubo.planes[1][1] = 0.0f; ubo.planes[1][2] = 0.0f; ubo.planes[1][3] = 1000.0f; // R
    ubo.planes[2][0] = 0.0f;  ubo.planes[2][1] =  1.0f; ubo.planes[2][2] = 0.0f; ubo.planes[2][3] = 1000.0f; // B
    ubo.planes[3][0] = 0.0f;  ubo.planes[3][1] = -1.0f; ubo.planes[3][2] = 0.0f; ubo.planes[3][3] = 1000.0f; // T
    ubo.planes[4][0] = 0.0f;  ubo.planes[4][1] = 0.0f;  ubo.planes[4][2] = -1.0f; ubo.planes[4][3] = -1.0f;   // Near
    ubo.planes[5][0] = 0.0f;  ubo.planes[5][1] = 0.0f;  ubo.planes[5][2] =  1.0f; ubo.planes[5][3] = 2000.0f; // Far

    ModelCpu models[71]{};
    models[0].sphere[0] = 0.0f; models[0].sphere[1] = 0.0f; models[0].sphere[2] = 0.0f; models[0].sphere[3] = 1.0f;
    models[54].category = 5; // prop

    ModelMark marks[71]{};
    marks[0].icon = 4; marks[0].shape = 0;
    marks[54].icon = 0xFFFFu; marks[54].shape = 0;

    // (i) Tiny far visible unit -> icon, no bar (px = 1 * 600 / 100 = 6 < 20)
    EntityInstance inst1{};
    inst1.ids = entity_pack_ids(0, 1, 0);
    inst1.scale = 1.0f;
    inst1.pos_prev[2] = -100.0f; inst1.pos_curr[2] = -100.0f;
    inst1.hp = 1.0f;
    auto res1 = marks_cull_reference(inst1, models, marks, ubo);
    RON_CHECK(res1.icon);
    RON_CHECK(!res1.bar);

    // (ii) Damaged (hp 0.4) mid unit px 30 -> bar, no icon (dist = 1 * 600 / 30 = 20)
    EntityInstance inst2{};
    inst2.ids = entity_pack_ids(0, 1, 0);
    inst2.scale = 1.0f;
    inst2.pos_prev[2] = -20.0f; inst2.pos_curr[2] = -20.0f;
    inst2.hp = 0.4f;
    auto res2 = marks_cull_reference(inst2, models, marks, ubo);
    RON_CHECK(!res2.icon);
    RON_CHECK(res2.bar);

    // (iii) Selected hp 1 -> bar (px = 30)
    EntityInstance inst3 = inst2;
    inst3.hp = 1.0f;
    inst3.ids = entity_pack_ids(0, 1, FLAG_SELECTED);
    auto res3 = marks_cull_reference(inst3, models, marks, ubo);
    RON_CHECK(res3.bar);

    // (iv) hp 0 unselected -> no bar
    EntityInstance inst4 = inst2;
    inst4.hp = 0.0f;
    inst4.ids = entity_pack_ids(0, 1, 0);
    auto res4 = marks_cull_reference(inst4, models, marks, ubo);
    RON_CHECK(!res4.bar);

    // (v) hp 1 unselected -> no bar
    EntityInstance inst5 = inst2;
    inst5.hp = 1.0f;
    inst5.ids = entity_pack_ids(0, 1, 0);
    auto res5 = marks_cull_reference(inst5, models, marks, ubo);
    RON_CHECK(!res5.bar);

    // (vi) Behind the camera -> neither (z = +50, camera looks at -Z)
    EntityInstance inst6 = inst2;
    inst6.pos_prev[2] = 50.0f; inst6.pos_curr[2] = 50.0f;
    auto res6 = marks_cull_reference(inst6, models, marks, ubo);
    RON_CHECK(!res6.icon && !res6.bar);

    // (vii) Invalid model -> neither
    EntityInstance inst7 = inst2;
    inst7.ids = entity_pack_ids(100, 1, 0);
    auto res7 = marks_cull_reference(inst7, models, marks, ubo);
    RON_CHECK(!res7.icon && !res7.bar);

    // (viii) Prop model -> never an icon
    EntityInstance inst8 = inst1;
    inst8.ids = entity_pack_ids(54, 1, 0);
    auto res8 = marks_cull_reference(inst8, models, marks, ubo);
    RON_CHECK(!res8.icon);

    // 5. icon_map & ui_source verification
    EntitySource src;
    RON_CHECK(src.init());
    RON_CHECK_EQ(src.model_count(), 71u);

    ModelMark all_marks[71];
    build_model_marks(src.models_cpu().data(), 71, all_marks);

    int count_per_icon[32] = {0};
    u32 marked_count = 0;
    u32 excluded_count = 0;

    for (int i = 0; i < 71; ++i) {
        RON_CHECK(all_marks[i].icon < 32 || all_marks[i].icon == 0xFFFFu);
        RON_CHECK(all_marks[i].shape <= 4);

        u8 cat = src.models_cpu()[i].category;
        if (cat >= 5) {
            RON_CHECK_EQ(all_marks[i].icon, 0xFFFFu);
            excluded_count++;
        } else {
            marked_count++;
            count_per_icon[all_marks[i].icon]++;
        }
        if (cat == 2) RON_CHECK_EQ(all_marks[i].shape, 1);
        if (cat == 3) RON_CHECK_EQ(all_marks[i].shape, 2);
        if (cat == 4) RON_CHECK_EQ(all_marks[i].shape, 3);
    }
    RON_CHECK_EQ(marked_count, 54u);
    RON_CHECK_EQ(excluded_count, 17u);

    UiSource ui;
    RON_CHECK(ui.init());
    RON_CHECK(ui.is_valid());
    RON_CHECK(ui.atlas_pixels() != nullptr);
    RON_CHECK_EQ(ui.atlas_width(), 1024u);
    RON_CHECK_EQ(ui.atlas_height(), 512u);

    // Check all 32 icon UV rects
    for (u32 i = 0; i < 32; ++i) {
        const auto& uv_i = ui.icon_uv(i);
        RON_CHECK(uv_i.u0 >= 0.0f && uv_i.u0 < uv_i.u1 && uv_i.u1 <= 1.0f);
        RON_CHECK(uv_i.v0 >= 0.0f && uv_i.v0 < uv_i.v1 && uv_i.v1 <= 1.0f);
        RON_CHECK(std::abs((uv_i.u1 - uv_i.u0) - (64.0f / 1024.0f)) < 0.001f);
        RON_CHECK(std::abs((uv_i.v1 - uv_i.v0) - (64.0f / 512.0f)) < 0.001f);

        // No two rects overlap
        for (u32 j = i + 1; j < 32; ++j) {
            const auto& uv_j = ui.icon_uv(j);
            bool no_overlap = (uv_i.u1 <= uv_j.u0 || uv_j.u1 <= uv_i.u0 ||
                               uv_i.v1 <= uv_j.v0 || uv_j.v1 <= uv_i.v0);
            RON_CHECK(no_overlap);
        }
    }

    // Print the icon table and model counts
    std::printf("\n================ ICON MAPPING TABLE ================\n");
    for (u32 i = 0; i < 32; ++i) {
        if (count_per_icon[i] > 0) {
            std::printf("Icon %2u [%-18s]: %d models\n", i, ui.icon_name(i), count_per_icon[i]);
        }
    }
    std::printf("Excluded [None]: %u models\n", excluded_count);
    std::printf("====================================================\n");
}

} // namespace ron::render
