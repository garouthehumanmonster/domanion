#include "test_framework.hpp"
#include "render/gl_fixture.hpp"
#include "render/entity_fixture.hpp"
#include "render/entity_math.hpp"
#include "render/territory_map.hpp"
#include "render/world_overlays.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace ron::render {

// Test 14: Entity_Draw_Visible
RON_TEST(Entity_Draw_Visible) {
    GL_TEST_BEGIN();
    auto& fixture = test::test_entity_fixture();
    fixture.init();

    Vec3 flat_pt = test::find_flattest_point(test::test_sampler());
    float focus_x = flat_pt.x;
    float focus_z = flat_pt.z;
    float focus_y = test::test_sampler().height_m(focus_x, focus_z);

    CameraRts cam;
    camera_init(cam, focus_x, focus_z, 0.0f, 60.0f);
    camera_snap(cam, &test::test_sampler());

    // Baseline render: 0 instances
    auto base_res = fixture.render(cam, {});

    Mat4 view = camera_view(cam, &test::test_sampler());
    Mat4 proj = camera_proj(640.0f / 360.0f);
    Mat4 vp = mat4_mul(proj, view);

    // Camera right vector in world space (row 0 of column-major view matrix)
    Vec3 cam_right = Vec3(view.m[0], view.m[4], view.m[8]).normalize();

    int model_count = fixture.assets.model_count();
    RON_CHECK_EQ(model_count, 71);

    u32 lod0_count = 0;

    for (int m = 0; m < model_count; ++m) {
        const auto& cpu_model = fixture.assets.source().model_cpu(m);

        float r_model = std::max(cpu_model.sphere[3], 0.001f);
        float scale = 8.0f / r_model;
        float radius_w = r_model * scale; // exactly 8.0m

        EntityInstance inst{};
        inst.pos_curr[0] = focus_x;
        inst.pos_curr[1] = focus_y;
        inst.pos_curr[2] = focus_z;
        inst.yaw_curr = 0.0f;
        inst.pos_prev[0] = focus_x;
        inst.pos_prev[1] = focus_y;
        inst.pos_prev[2] = focus_z;
        inst.yaw_prev = 0.0f;
        inst.scale = scale;
        inst.anim_t = 0.0f;
        inst.clip = 0; // Idle
        inst.ids = entity_pack_ids(static_cast<u16>(m), 1u, 0u); // owner 1

        auto res = fixture.render(cam, { inst }, 0.0f);

        auto stats = fixture.renderer.read_stats_blocking();
        if (stats.lod[0] > 0) {
            ++lod0_count;
        }

        // Bounding sphere center in world space (yaw = 0, R_y = identity)
        Vec3 centre_world{
            focus_x + cpu_model.sphere[0] * scale,
            focus_y + cpu_model.sphere[1] * scale,
            focus_z + cpu_model.sphere[2] * scale
        };

        // Project center using project()
        float ndc_c[3];
        project(vp, centre_world, ndc_c);
        float screen_cx = (ndc_c[0] * 0.5f + 0.5f) * 640.0f;
        float screen_cy = (ndc_c[1] * 0.5f + 0.5f) * 360.0f;

        // Project point offset by radius_w along camera right
        Vec3 p_right = centre_world + cam_right * radius_w;
        float ndc_r[3];
        project(vp, p_right, ndc_r);
        float screen_rx = (ndc_r[0] * 0.5f + 0.5f) * 640.0f;
        float screen_ry = (ndc_r[1] * 0.5f + 0.5f) * 360.0f;

        float r_px = std::sqrt((screen_rx - screen_cx) * (screen_rx - screen_cx) +
                               (screen_ry - screen_cy) * (screen_ry - screen_cy));

        float r_inner_sq = (r_px * 1.0f) * (r_px * 1.0f);
        float r_outer = 1.10f * r_px + 3.0f;
        float r_outer_sq = r_outer * r_outer;

        u32 inside_changed = 0;
        u32 outside_changed = 0;

        for (int y = 0; y < 360; ++y) {
            for (int x = 0; x < 640; ++x) {
                int idx = (y * 640 + x) * 4;
                int dr = std::abs(int(res.color_rgba8[idx + 0]) - int(base_res.color_rgba8[idx + 0]));
                int dg = std::abs(int(res.color_rgba8[idx + 1]) - int(base_res.color_rgba8[idx + 1]));
                int db = std::abs(int(res.color_rgba8[idx + 2]) - int(base_res.color_rgba8[idx + 2]));
                bool changed = (dr > 2 || dg > 2 || db > 2);

                float dx = (float(x) + 0.5f) - screen_cx;
                float dy = (float(y) + 0.5f) - screen_cy;
                float dist_sq = dx * dx + dy * dy;

                if (dist_sq <= r_inner_sq) {
                    if (changed) ++inside_changed;
                } else if (dist_sq > r_outer_sq) {
                    if (changed) ++outside_changed;
                }
            }
        }

        if (inside_changed < 40 || outside_changed > 0) {
            RON_LOG_ERROR("Model %u (%s) check failed: inside_changed=%u (need >= 40), outside_changed=%u (need 0), r_px=%.2f",
                          m, cpu_model.name.c_str(), inside_changed, outside_changed, r_px);
        }
        RON_CHECK(inside_changed >= 40);
        RON_CHECK_EQ(outside_changed, 0u);
    }

    RON_LOG_INFO("Entity_Draw_Visible: %u / %u models at LOD0", lod0_count, model_count);
    GL_TEST_END();
}

// Test 15: Entity_Anim
RON_TEST(Entity_Anim) {
    GL_TEST_BEGIN();
    auto& fixture = test::test_entity_fixture();
    fixture.init();

    Vec3 flat_pt = test::find_flattest_point(test::test_sampler());
    float focus_x = flat_pt.x;
    float focus_z = flat_pt.z;
    float focus_y = test::test_sampler().height_m(focus_x, focus_z);

    CameraRts cam;
    camera_init(cam, focus_x, focus_z, 0.0f, 60.0f);
    camera_snap(cam, &test::test_sampler());

    int model_count = fixture.assets.model_count();

    // 1. Loop continuity: for 2 infantry models and 1 vehicle
    std::vector<u32> test_models;
    int inf_found = 0;
    int veh_found = 0;
    for (int m = 0; m < model_count; ++m) {
        int cat = fixture.assets.source().model_cpu(m).category;
        if (cat == 0 && inf_found < 2) {
            test_models.push_back(m);
            ++inf_found;
        } else if (cat == 1 && veh_found < 1) {
            test_models.push_back(m);
            ++veh_found;
        }
    }
    RON_CHECK_EQ(test_models.size(), 3u);

    for (u32 m : test_models) {
        const auto& cpu_model = fixture.assets.source().model_cpu(m);
        float scale = 8.0f / std::max(cpu_model.sphere[3], 0.001f);

        EntityInstance inst_m0{};
        inst_m0.pos_curr[0] = focus_x;
        inst_m0.pos_curr[1] = focus_y;
        inst_m0.pos_curr[2] = focus_z;
        inst_m0.yaw_curr = 0.0f;
        inst_m0.pos_prev[0] = focus_x;
        inst_m0.pos_prev[1] = focus_y;
        inst_m0.pos_prev[2] = focus_z;
        inst_m0.yaw_prev = 0.0f;
        inst_m0.scale = scale;
        inst_m0.clip = 1; // Move
        inst_m0.anim_t = 0.0f;
        inst_m0.ids = entity_pack_ids(static_cast<u16>(m), 1u, 0u);

        EntityInstance inst_m3 = inst_m0;
        inst_m3.anim_t = 3.0f * 16.0f / ANIM_FPS;

        auto res_m0 = fixture.render(cam, { inst_m0 });
        auto res_m3 = fixture.render(cam, { inst_m3 });

        int max_loop_diff = 0;
        for (size_t i = 0; i < res_m0.color_rgba8.size(); ++i) {
            int d = std::abs(int(res_m0.color_rgba8[i]) - int(res_m3.color_rgba8[i]));
            if (d > max_loop_diff) max_loop_diff = d;
        }
        RON_CHECK(max_loop_diff <= 2);

        // 2. Die clamp: clip Die at anim_t = 100 s vs 200 s equal within 1 level
        EntityInstance inst_d100 = inst_m0;
        inst_d100.clip = 3; // Die
        inst_d100.anim_t = 100.0f;

        EntityInstance inst_d200 = inst_d100;
        inst_d200.anim_t = 200.0f;

        auto res_d100 = fixture.render(cam, { inst_d100 });
        auto res_d200 = fixture.render(cam, { inst_d200 });

        int max_die_diff = 0;
        for (size_t i = 0; i < res_d100.color_rgba8.size(); ++i) {
            int d = std::abs(int(res_d100.color_rgba8[i]) - int(res_d200.color_rgba8[i]));
            if (d > max_die_diff) max_die_diff = d;
        }
        RON_CHECK(max_die_diff <= 1);
    }

    // 3. Animation changes pixels: among infantry models, at least ONE has >= 30 pixels differing by > 8 levels
    u32 infantry_anim_count = 0;
    for (int m = 0; m < model_count; ++m) {
        const auto& cpu_model = fixture.assets.source().model_cpu(m);
        if (cpu_model.category != 0) continue; // CAT_INFANTRY

        float scale = 8.0f / std::max(cpu_model.sphere[3], 0.001f);

        EntityInstance inst_m0{};
        inst_m0.pos_curr[0] = focus_x;
        inst_m0.pos_curr[1] = focus_y;
        inst_m0.pos_curr[2] = focus_z;
        inst_m0.yaw_curr = 0.0f;
        inst_m0.pos_prev[0] = focus_x;
        inst_m0.pos_prev[1] = focus_y;
        inst_m0.pos_prev[2] = focus_z;
        inst_m0.yaw_prev = 0.0f;
        inst_m0.scale = scale;
        inst_m0.clip = 1; // Move
        inst_m0.anim_t = 0.0f;
        inst_m0.ids = entity_pack_ids(static_cast<u16>(m), 1u, 0u);

        EntityInstance inst_m_half = inst_m0;
        // Spec deviation (documented in docs/spec_deviations.md):
        // Procedural humanoid walk curves use pure sines (sway/bob). At t=0.5*16/ANIM_FPS (frame 8, half-cycle),
        // sin(pi) = 0 and all part matrices in VAT are identical to identity (neutral rest pose).
        // Peak limb swing occurs at quarter cycle t = 0.25*16/ANIM_FPS (frame 4).
        inst_m_half.anim_t = 0.25f * 16.0f / ANIM_FPS;

        auto res_m0 = fixture.render(cam, { inst_m0 });
        auto res_m_half = fixture.render(cam, { inst_m_half });

        u32 diff_pixels = 0;
        int max_diff_m = 0;
        for (size_t p = 0; p < 640 * 360; ++p) {
            size_t idx = p * 4;
            int dr = std::abs(int(res_m0.color_rgba8[idx + 0]) - int(res_m_half.color_rgba8[idx + 0]));
            int dg = std::abs(int(res_m0.color_rgba8[idx + 1]) - int(res_m_half.color_rgba8[idx + 1]));
            int db = std::abs(int(res_m0.color_rgba8[idx + 2]) - int(res_m_half.color_rgba8[idx + 2]));
            int d = std::max({dr, dg, db});
            if (d > max_diff_m) max_diff_m = d;
            if (dr > 8 || dg > 8 || db > 8) {
                ++diff_pixels;
            }
        }
        RON_LOG_INFO("Infantry model %d (%s): diff_pixels=%u, max_diff=%d", m, cpu_model.name.c_str(), diff_pixels, max_diff_m);
        if (diff_pixels >= 30) {
            ++infantry_anim_count;
        }
    }

    RON_LOG_INFO("Entity_Anim: %u infantry animate", infantry_anim_count);
    RON_CHECK(infantry_anim_count >= 1);

    GL_TEST_END();
}

// Test 16: Entity_Interp_Render
RON_TEST(Entity_Interp_Render) {
    GL_TEST_BEGIN();
    auto& fixture = test::test_entity_fixture();
    fixture.init();

    Vec3 flat_pt = test::find_flattest_point(test::test_sampler());
    float focus_x = flat_pt.x;
    float focus_z = flat_pt.z;

    CameraRts cam;
    camera_init(cam, focus_x, focus_z, 0.0f, 60.0f);
    camera_snap(cam, &test::test_sampler());

    // First vehicle model
    u32 veh_model = 0;
    for (int m = 0; m < fixture.assets.model_count(); ++m) {
        if (fixture.assets.source().model_cpu(m).category == 1) {
            veh_model = m;
            break;
        }
    }
    const auto& cpu_model = fixture.assets.source().model_cpu(veh_model);
    float scale = 8.0f / std::max(cpu_model.sphere[3], 0.001f);

    float hA = test::test_sampler().height_m(focus_x - 6.0f, focus_z);
    float hB = test::test_sampler().height_m(focus_x + 6.0f, focus_z + 3.0f);

    float posA[3] = { focus_x - 6.0f, hA, focus_z };
    float posB[3] = { focus_x + 6.0f, hB, focus_z + 3.0f };
    float posMid[3] = { 0.5f * (posA[0] + posB[0]), 0.5f * (posA[1] + posB[1]), 0.5f * (posA[2] + posB[2]) };

    // 1. Interpolation midpoint test: prev=A, curr=B, alpha=0.5 vs prev=curr=midpoint, alpha=0
    EntityInstance inst_moving{};
    inst_moving.pos_prev[0] = posA[0]; inst_moving.pos_prev[1] = posA[1]; inst_moving.pos_prev[2] = posA[2];
    inst_moving.yaw_prev = 0.0f;
    inst_moving.pos_curr[0] = posB[0]; inst_moving.pos_curr[1] = posB[1]; inst_moving.pos_curr[2] = posB[2];
    inst_moving.yaw_curr = 0.0f;
    inst_moving.scale = scale;
    inst_moving.ids = entity_pack_ids(static_cast<u16>(veh_model), 1u, 0u);

    EntityInstance inst_midpoint = inst_moving;
    inst_midpoint.pos_prev[0] = posMid[0]; inst_midpoint.pos_prev[1] = posMid[1]; inst_midpoint.pos_prev[2] = posMid[2];
    inst_midpoint.pos_curr[0] = posMid[0]; inst_midpoint.pos_curr[1] = posMid[1]; inst_midpoint.pos_curr[2] = posMid[2];

    auto res_interp = fixture.render(cam, { inst_moving }, 0.5f);
    auto res_mid    = fixture.render(cam, { inst_midpoint }, 0.0f);

    double sum_diff = 0.0;
    int max_diff = 0;
    size_t count = 640 * 360 * 3;

    for (size_t p = 0; p < 640 * 360; ++p) {
        size_t idx = p * 4;
        for (int c = 0; c < 3; ++c) {
            int d = std::abs(int(res_interp.color_rgba8[idx + c]) - int(res_mid.color_rgba8[idx + c]));
            sum_diff += d;
            if (d > max_diff) max_diff = d;
        }
    }
    float mean_diff = float(sum_diff / count);
    RON_CHECK(mean_diff <= 0.1f);
    RON_CHECK(max_diff <= 8);

    // 2. Yaw wrap: prev 350 deg, curr 10 deg, alpha 0.5 equals yaw 0
    float yaw_350 = 350.0f * (3.14159265f / 180.0f);
    float yaw_10  = 10.0f  * (3.14159265f / 180.0f);

    EntityInstance inst_yaw_wrap = inst_moving;
    inst_yaw_wrap.pos_prev[0] = focus_x; inst_yaw_wrap.pos_prev[1] = hA; inst_yaw_wrap.pos_prev[2] = focus_z;
    inst_yaw_wrap.yaw_prev = yaw_350;
    inst_yaw_wrap.pos_curr[0] = focus_x; inst_yaw_wrap.pos_curr[1] = hA; inst_yaw_wrap.pos_curr[2] = focus_z;
    inst_yaw_wrap.yaw_curr = yaw_10;

    EntityInstance inst_yaw_zero = inst_moving;
    inst_yaw_zero.pos_prev[0] = focus_x; inst_yaw_zero.pos_prev[1] = hA; inst_yaw_zero.pos_prev[2] = focus_z;
    inst_yaw_zero.yaw_prev = 0.0f;
    inst_yaw_zero.pos_curr[0] = focus_x; inst_yaw_zero.pos_curr[1] = hA; inst_yaw_zero.pos_curr[2] = focus_z;
    inst_yaw_zero.yaw_curr = 0.0f;

    auto res_wrap = fixture.render(cam, { inst_yaw_wrap }, 0.5f);
    auto res_zero = fixture.render(cam, { inst_yaw_zero }, 0.0f);

    double sum_yaw_diff = 0.0;
    int max_yaw_diff = 0;
    for (size_t p = 0; p < 640 * 360; ++p) {
        size_t idx = p * 4;
        for (int c = 0; c < 3; ++c) {
            int d = std::abs(int(res_wrap.color_rgba8[idx + c]) - int(res_zero.color_rgba8[idx + c]));
            sum_yaw_diff += d;
            if (d > max_yaw_diff) max_yaw_diff = d;
        }
    }
    float mean_yaw_diff = float(sum_yaw_diff / count);
    RON_CHECK(mean_yaw_diff <= 0.1f);
    RON_CHECK(max_yaw_diff <= 8);

    // 3. alpha 0 equals prev-only
    EntityInstance inst_prev_only = inst_moving;
    inst_prev_only.pos_prev[0] = posA[0]; inst_prev_only.pos_prev[1] = posA[1]; inst_prev_only.pos_prev[2] = posA[2];
    inst_prev_only.pos_curr[0] = posA[0]; inst_prev_only.pos_curr[1] = posA[1]; inst_prev_only.pos_curr[2] = posA[2];

    auto res_alpha0 = fixture.render(cam, { inst_moving }, 0.0f);
    auto res_prev   = fixture.render(cam, { inst_prev_only }, 0.0f);

    int max_diff_a0 = 0;
    for (size_t i = 0; i < res_alpha0.color_rgba8.size(); ++i) {
        int d = std::abs(int(res_alpha0.color_rgba8[i]) - int(res_prev.color_rgba8[i]));
        if (d > max_diff_a0) max_diff_a0 = d;
    }
    RON_CHECK(max_diff_a0 <= 1);

    // 4. alpha 1 equals curr-only
    EntityInstance inst_curr_only = inst_moving;
    inst_curr_only.pos_prev[0] = posB[0]; inst_curr_only.pos_prev[1] = posB[1]; inst_curr_only.pos_prev[2] = posB[2];
    inst_curr_only.pos_curr[0] = posB[0]; inst_curr_only.pos_curr[1] = posB[1]; inst_curr_only.pos_curr[2] = posB[2];

    auto res_alpha1 = fixture.render(cam, { inst_moving }, 1.0f);
    auto res_curr   = fixture.render(cam, { inst_curr_only }, 0.0f);

    int max_diff_a1 = 0;
    for (size_t i = 0; i < res_alpha1.color_rgba8.size(); ++i) {
        int d = std::abs(int(res_alpha1.color_rgba8[i]) - int(res_curr.color_rgba8[i]));
        if (d > max_diff_a1) max_diff_a1 = d;
    }
    RON_CHECK(max_diff_a1 <= 1);

    GL_TEST_END();
}

// Test 17: Entity_Water_Order
RON_TEST(Entity_Water_Order) {
    GL_TEST_BEGIN();
    auto& fixture = test::test_entity_fixture();
    fixture.init();

    // Find open sea point with >= 250m sea around it (reusing Gate 10 finder)
    u8 max_depth_val = 0;
    Vec3 deep_cell = test::find_max_ocean_depth_cell(test::test_scene().terrain, max_depth_val);
    RON_CHECK(max_depth_val >= 64);
    float sea_x = deep_cell.x;
    float sea_z = deep_cell.z;

    CameraRts cam;
    camera_init(cam, sea_x, sea_z, 0.0f, 60.0f);
    camera_snap(cam, &test::test_sampler());

    // Vehicle at y = +12 m
    u32 veh_model = 0;
    for (int m = 0; m < fixture.assets.model_count(); ++m) {
        if (fixture.assets.source().model_cpu(m).category == 1) { // CAT_VEHICLE
            veh_model = m;
            break;
        }
    }
    const auto& cpu_model = fixture.assets.source().model_cpu(veh_model);
    float scale = 8.0f / std::max(cpu_model.sphere[3], 0.001f);

    EntityInstance inst{};
    inst.pos_curr[0] = sea_x;
    inst.pos_curr[1] = 12.0f;
    inst.pos_curr[2] = sea_z;
    inst.yaw_curr = 0.0f;
    inst.pos_prev[0] = sea_x;
    inst.pos_prev[1] = 12.0f;
    inst.pos_prev[2] = sea_z;
    inst.yaw_prev = 0.0f;
    inst.scale = scale;
    inst.ids = entity_pack_ids(static_cast<u16>(veh_model), 2u, 0u);

    // Baseline: ocean skipped, entities skipped
    auto base_no_ocean = fixture.render(cam, {}, 0.0f, 1.0f, false, nullptr, nullptr, true, false);
    // Render with ocean skipped, entities drawn
    auto no_ocean_res  = fixture.render(cam, { inst }, 0.0f, 1.0f, false, nullptr, nullptr, true, true);
    // Full frame: entities drawn before ocean, ocean drawn
    auto full_res      = fixture.render(cam, { inst }, 0.0f, 1.0f, true,  nullptr, nullptr, true, true);

    u32 entity_pixels = 0;
    int max_diff = 0;

    for (size_t p = 0; p < 640 * 360; ++p) {
        size_t idx = p * 4;
        int dr = std::abs(int(no_ocean_res.color_rgba8[idx + 0]) - int(base_no_ocean.color_rgba8[idx + 0]));
        int dg = std::abs(int(no_ocean_res.color_rgba8[idx + 1]) - int(base_no_ocean.color_rgba8[idx + 1]));
        int db = std::abs(int(no_ocean_res.color_rgba8[idx + 2]) - int(base_no_ocean.color_rgba8[idx + 2]));

        // Entity covers the screen here
        if (dr > 2 || dg > 2 || db > 2) {
            ++entity_pixels;
            int df_r = std::abs(int(full_res.color_rgba8[idx + 0]) - int(no_ocean_res.color_rgba8[idx + 0]));
            int df_g = std::abs(int(full_res.color_rgba8[idx + 1]) - int(no_ocean_res.color_rgba8[idx + 1]));
            int df_b = std::abs(int(full_res.color_rgba8[idx + 2]) - int(no_ocean_res.color_rgba8[idx + 2]));
            max_diff = std::max({max_diff, df_r, df_g, df_b});
        }
    }

    RON_CHECK(entity_pixels >= 40);
    RON_CHECK(max_diff <= 1);

    GL_TEST_END();
}

// Test 18: Entity_Fog
RON_TEST(Entity_Fog) {
    GL_TEST_BEGIN();
    auto& fixture = test::test_entity_fixture();
    fixture.init();

    Vec3 flat_pt = test::find_flattest_point(test::test_sampler());
    float focus_x = flat_pt.x;
    float focus_z = flat_pt.z;
    float focus_y = test::test_sampler().height_m(focus_x, focus_z);

    CameraRts cam;
    camera_init(cam, focus_x, focus_z, 0.0f, 60.0f);
    camera_snap(cam, &test::test_sampler());

    const auto& cpu_model = fixture.assets.source().model_cpu(0);
    float scale = 8.0f / std::max(cpu_model.sphere[3], 0.001f);

    EntityInstance inst{};
    inst.pos_curr[0] = focus_x;
    inst.pos_curr[1] = focus_y;
    inst.pos_curr[2] = focus_z;
    inst.yaw_curr = 0.0f;
    inst.pos_prev[0] = focus_x;
    inst.pos_prev[1] = focus_y;
    inst.pos_prev[2] = focus_z;
    inst.yaw_prev = 0.0f;
    inst.scale = scale;
    inst.ids = entity_pack_ids(0, 1u, 0u);

    // Reference render with overlays off
    auto ref_res = fixture.render(cam, { inst }, 0.0f, 1.0f, true, nullptr);

    // Setup WorldOverlays
    WorldOverlays overlays;
    RON_CHECK(overlays.init(fixture.ledger, {}, &test::test_scene().shaders));
    overlays.set_flags(true, true, true);
    overlays.craters().clear();

    // 1. Unexplored fog: produces mean luminance <= 6 on 16x16 block at center
    overlays.fog().reset();
    overlays.fog().commit();
    overlays.update(100.0f, 0.0f);

    auto unexp_res = fixture.render(cam, { inst }, 0.0f, 1.0f, true, &overlays);

    Mat4 view = camera_view(cam, &test::test_sampler());
    Mat4 proj = camera_proj(640.0f / 360.0f);
    Mat4 vp = mat4_mul(proj, view);

    Vec3 centre_world{
        focus_x + cpu_model.sphere[0] * scale,
        focus_y + cpu_model.sphere[1] * scale,
        focus_z + cpu_model.sphere[2] * scale
    };
    float ndc_c[3];
    project(vp, centre_world, ndc_c);
    int cx = int((ndc_c[0] * 0.5f + 0.5f) * 640.0f);
    int cy = int((ndc_c[1] * 0.5f + 0.5f) * 360.0f);

    double sum_luma = 0.0;
    int luma_count = 0;
    for (int y = cy - 8; y < cy + 8; ++y) {
        for (int x = cx - 8; x < cx + 8; ++x) {
            int idx = (y * 640 + x) * 4;
            float r = unexp_res.color_rgba8[idx + 0];
            float g = unexp_res.color_rgba8[idx + 1];
            float b = unexp_res.color_rgba8[idx + 2];
            sum_luma += 0.299 * r + 0.587 * g + 0.114 * b;
            ++luma_count;
        }
    }
    float mean_luma = float(sum_luma / luma_count);
    RON_CHECK(mean_luma <= 6.0f);

    // 2. Revealed fog: matches overlays-off within 2 levels
    overlays.fog().begin_frame();
    overlays.fog().reveal_circle(focus_x, focus_z, 300.0f);
    overlays.fog().commit();
    overlays.update(100.0f, 0.0f);

    auto rev_res = fixture.render(cam, { inst }, 0.0f, 1.0f, true, &overlays);

    int max_rev_diff = 0;
    for (int y = cy - 8; y < cy + 8; ++y) {
        for (int x = cx - 8; x < cx + 8; ++x) {
            int idx = (y * 640 + x) * 4;
            int dr = std::abs(int(rev_res.color_rgba8[idx + 0]) - int(ref_res.color_rgba8[idx + 0]));
            int dg = std::abs(int(rev_res.color_rgba8[idx + 1]) - int(ref_res.color_rgba8[idx + 1]));
            int db = std::abs(int(rev_res.color_rgba8[idx + 2]) - int(ref_res.color_rgba8[idx + 2]));
            max_rev_diff = std::max({max_rev_diff, dr, dg, db});
        }
    }
    RON_CHECK(max_rev_diff <= 2);

    GL_TEST_END();
}

} // namespace ron::render
