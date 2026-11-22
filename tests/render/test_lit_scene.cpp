#include "test_framework.hpp"
#include "render/gl_fixture.hpp"
#include "render/lit_fixture.hpp"
#include "render/entity_fixture.hpp"
#include "render/shader_preproc.hpp"
#include "render/shadow_ref.hpp"
#include "render/rmath.hpp"
#include "render/camera_rts.hpp"
#include "render/territory_map.hpp"
#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace ron::render {

// Helper for dot product
static inline float dot3(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static bool mem_reader(void* user, const char* rel_path, std::string& out) {
    auto* map = static_cast<const std::unordered_map<std::string, std::string>*>(user);
    if (!map || !rel_path) return false;
    auto it = map->find(rel_path);
    if (it != map->end()) {
        out = it->second;
        return true;
    }
    return false;
}

// Test 13: Lit_Programs
RON_TEST(Lit_Programs) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    auto& fixture = test::test_lit_fixture();
    auto& shaders = test::test_scene().shaders;

    // 1. Verify every new ProgramId compiles and links
    const ProgramId new_progs[] = {
        ProgramId::ShadowEntity,
        ProgramId::TerrainLit,
        ProgramId::TerrainLitOvl,
        ProgramId::EntityLit,
        ProgramId::EntityLitOvl,
        ProgramId::MarksCull,
        ProgramId::IconDraw,
        ProgramId::BarDraw,
        ProgramId::ShadowSelfTest,
        ProgramId::PbrSelfTest
    };

    for (ProgramId pid : new_progs) {
        u32 prog = shaders.program(pid);
        RON_CHECK(prog != 0);
    }

    // 2. Binding defines appear in shader preprocessed preamble
    std::unordered_map<std::string, std::string> dummy_files = {
        { "test.glsl", "#version 460 core\nvoid main(){}\n" }
    };
    ShaderSource src;
    bool ok = preprocess_shader("test.glsl", mem_reader, &dummy_files, nullptr, 0, src);
    RON_CHECK(ok);
    RON_CHECK(src.text.find("#define BIND_UBO_LIGHT 5") != std::string::npos);
    RON_CHECK(src.text.find("#define BIND_UBO_MARKS 6") != std::string::npos);
    RON_CHECK(src.text.find("#define TEX_SHADOW_MAP 19") != std::string::npos);
    RON_CHECK(src.text.find("#define TEX_HEIGHT_LINEAR 20") != std::string::npos);
    RON_CHECK(src.text.find("#define TEX_MSDF 21") != std::string::npos);

    // 3. Plain Gate 10 render (lighting null) is byte-identical to lit fixture with lighting null and marks off
    test::EntityDrawFixture gate10_fixture;
    gate10_fixture.init();

    Site sites[8];
    pick_land_sites(test::test_sampler(), 8, 400.0f, sites);

    CameraRts cam{};
    camera_init(cam, sites[0].x, sites[0].z, 0.0f, 200.0f);
    camera_snap(cam, &test::test_sampler());

    auto res_gate10 = gate10_fixture.render(cam, {}, 1.0f, 1.0f, true, nullptr, nullptr, true, false);

    test::LitOptions opt{};
    opt.focus = Vec3{sites[0].x, test::test_sampler().height_m(sites[0].x, sites[0].z), sites[0].z};
    opt.dist = 200.0f;
    opt.yaw = 0.0f;
    opt.enable_lighting = false;
    opt.enable_marks = false;
    opt.enable_overlays = false;
    opt.draw_entities = false;
    opt.draw_ocean = true;
    opt.draw_terrain = true;

    auto res_lit_null = fixture.render(opt);

    int diff_count = 0;
    for (size_t i = 0; i < res_gate10.color_rgba8.size(); ++i) {
        if (res_gate10.color_rgba8[i] != res_lit_null.color_rgba8[i]) {
            if (diff_count < 10) {
                size_t p = i / 4;
                int ch = i % 4;
                int x = p % 640;
                int y = p / 640;
                std::printf("Diff at pixel (%d, %d) ch %d: gate10=%d lit_null=%d\n",
                            x, y, ch, res_gate10.color_rgba8[i], res_lit_null.color_rgba8[i]);
            }
            diff_count++;
        }
    }
    std::printf("Total diff pixels: %d\n", diff_count);

    RON_CHECK_EQ(res_gate10.color_rgba8.size(), res_lit_null.color_rgba8.size());
    RON_CHECK(res_gate10.color_rgba8 == res_lit_null.color_rgba8);

    GL_TEST_END();
}

// Test 14: Shadow_Entity_Cascades
RON_TEST(Shadow_Entity_Cascades) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    auto& fixture = test::test_lit_fixture();
    const auto& sampler = test::test_sampler();

    const float D_c[4] = {40.0f, 130.0f, 300.0f, 800.0f};

    // Pick first land site as focus
    Site sites[8];
    pick_land_sites(sampler, 8, 400.0f, sites);
    Vec3 focus{sites[0].x, sampler.height_m(sites[0].x, sites[0].z), sites[0].z};

    // Find first vehicle model (category == 1)
    int vehicle_id = -1;
    for (int i = 0; i < fixture.assets.model_count(); ++i) {
        if (fixture.assets.model(i).category == 1) {
            vehicle_id = i;
            break;
        }
    }
    RON_CHECK(vehicle_id >= 0);
    const ModelCpu& model = fixture.assets.model(vehicle_id);

    // Choose from LOD0 triangles the triangle of MAXIMUM area
    u32 first_idx = model.lod_first_index[0];
    u32 idx_count = model.lod_index_count[0];
    i32 base_vtx = model.lod_base_vertex[0];
    const u16* indices = fixture.assets.source().index_blob();
    const u8* vtx_blob = fixture.assets.source().vertex_blob();

    float max_area = -1.0f;
    Vec3 best_centroid_model{0.0f, 0.0f, 0.0f};

    for (u32 t = 0; t + 2 < idx_count; t += 3) {
        u16 i0 = indices[first_idx + t + 0];
        u16 i1 = indices[first_idx + t + 1];
        u16 i2 = indices[first_idx + t + 2];

        const float* p0 = reinterpret_cast<const float*>(vtx_blob + (base_vtx + i0) * 24);
        const float* p1 = reinterpret_cast<const float*>(vtx_blob + (base_vtx + i1) * 24);
        const float* p2 = reinterpret_cast<const float*>(vtx_blob + (base_vtx + i2) * 24);

        Vec3 v0{p0[0], p0[1], p0[2]};
        Vec3 v1{p1[0], p1[1], p1[2]};
        Vec3 v2{p2[0], p2[1], p2[2]};

        Vec3 e1{v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
        Vec3 e2{v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};

        Vec3 cross{e1.y * e2.z - e1.z * e2.y,
                   e1.z * e2.x - e1.x * e2.z,
                   e1.x * e2.y - e1.y * e2.x};
        float area = 0.5f * std::sqrt(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
        if (area > max_area) {
            max_area = area;
            best_centroid_model = Vec3{(v0.x + v1.x + v2.x) / 3.0f,
                                       (v0.y + v1.y + v2.y) / 3.0f,
                                       (v0.z + v1.z + v2.z) / 3.0f};
        }
    }
    RON_CHECK(max_area > 0.0f);

    // Sun: elevation 45 deg, azimuth 135 deg
    FrameUniforms fu{};
    set_sun(fu, 45.0f, 135.0f);
    float L[3];
    light_to_sun(fu, L);

    const float yaws[4] = {0.0f, 1.5707963f, 3.14159265f, 4.712389f}; // {0, 90, 180, 270} deg

    for (int c = 0; c < 4; ++c) {
        float dist = D_c[c];
        float radius_w = 0.06f * dist;
        float scale = radius_w / std::max(0.001f, model.sphere[3]);

        EntityInstance inst{};
        inst.pos_prev[0] = focus.x; inst.pos_prev[1] = focus.y; inst.pos_prev[2] = focus.z;
        inst.pos_curr[0] = focus.x; inst.pos_curr[1] = focus.y; inst.pos_curr[2] = focus.z;
        inst.yaw_prev = 0.0f; inst.yaw_curr = 0.0f;
        inst.scale = scale;
        inst.ids = entity_pack_ids(vehicle_id, 1, 0);
        inst.clip = 0;
        inst.anim_t = 0.0f;
        inst.hp = 0.0f;

        // Centroid G in world space
        Vec3 G{focus.x + best_centroid_model.x * scale,
               focus.y + best_centroid_model.y * scale,
               focus.z + best_centroid_model.z * scale};

        // Shadow point S = ray from G along -L marched down onto terrain
        Vec3 S = G;
        for (float t = 0.0f; t <= 600.0f; t += 0.25f) {
            Vec3 p{G.x - L[0] * t, G.y - L[1] * t, G.z - L[2] * t};
            float h = sampler.height_m(p.x, p.z);
            if (p.y <= h) {
                S = Vec3{p.x, h, p.z};
                break;
            }
        }

        bool matched_yaw = false;
        for (float yaw : yaws) {
            test::LitOptions opt_a{};
            opt_a.focus = focus;
            opt_a.dist = dist;
            opt_a.pitch = 45.0f;
            opt_a.yaw = yaw;
            opt_a.sun_elev = 45.0f;
            opt_a.sun_az = 135.0f;
            opt_a.enable_lighting = true;
            opt_a.light_flags = LIGHT_FLAG_CSM; // entity shadows ON, terrain shadows OFF
            opt_a.enable_marks = false;
            opt_a.entities = { inst };

            auto res_a = fixture.render(opt_a);

            // Project S
            float ndc[3];
            project(res_a.view_proj, S, ndc);
            if (ndc[0] < -0.95f || ndc[0] > 0.95f || ndc[1] < -0.95f || ndc[1] > 0.95f) continue;

            int px = static_cast<int>((ndc[0] * 0.5f + 0.5f) * 640.0f);
            int py = static_cast<int>((ndc[1] * 0.5f + 0.5f) * 360.0f);
            px = std::clamp(px, 2, 637);
            py = std::clamp(py, 2, 357);

            // Depth check at S's pixel
            size_t depth_idx = static_cast<size_t>(py * 640 + px);
            float d_buf = res_a.depth[depth_idx];
            if (d_buf <= 1e-6f) continue;

            // In reverse-Z: z_ndc = -A + B / z_v => z_v = B / (z_ndc + A)
            const float n_cam = CAM_NEAR;
            const float f_cam = CAM_FAR;
            const float A_proj = n_cam / (f_cam - n_cam);
            const float B_proj = (n_cam * f_cam) / (f_cam - n_cam);
            float view_depth_readback = B_proj / (d_buf + A_proj);
            float view_depth_S = dot3(Vec3{S.x - res_a.eye.x, S.y - res_a.eye.y, S.z - res_a.eye.z}, res_a.fwd);

            float depth_tol = std::max(2.0f, 0.005f * dist);
            if (std::abs(view_depth_readback - view_depth_S) > depth_tol) {
                // Occluded by entity or wrong depth
                std::printf("  Cascade %d yaw %.2f: depth mismatch readback=%.2f vs S=%.2f (tol=%.2f)\n",
                            c, yaw, view_depth_readback, view_depth_S, depth_tol);
                continue;
            }

            // Render B = entity shadows OFF
            test::LitOptions opt_b = opt_a;
            opt_b.light_flags = 0;
            auto res_b = fixture.render(opt_b);

            float lum_a = test::calc_mean_luminance(res_a.color_rgba8, px - 1, py - 1, 3, 3);
            float lum_b = test::calc_mean_luminance(res_b.color_rgba8, px - 1, py - 1, 3, 3);

            std::printf("  Cascade %d yaw %.2f: Lum A=%.2f B=%.2f (ratio=%.3f, req <= 0.80)\n",
                        c, yaw, lum_a, lum_b, lum_a / std::max(0.001f, lum_b));

            // 3x3 block mean luminance of A <= 0.80 * that of B
            if (lum_a <= 0.80f * lum_b) {
                // Check 4 image corners: |A - B| <= 1 per channel
                const int corner_coords[4][2] = {{0, 0}, {639, 0}, {0, 359}, {639, 359}};
                bool corners_ok = true;
                for (int ci = 0; ci < 4; ++ci) {
                    size_t c_idx = static_cast<size_t>((corner_coords[ci][1] * 640 + corner_coords[ci][0]) * 4);
                    int dr = std::abs(static_cast<int>(res_a.color_rgba8[c_idx])     - static_cast<int>(res_b.color_rgba8[c_idx]));
                    int dg = std::abs(static_cast<int>(res_a.color_rgba8[c_idx + 1]) - static_cast<int>(res_b.color_rgba8[c_idx + 1]));
                    int db = std::abs(static_cast<int>(res_a.color_rgba8[c_idx + 2]) - static_cast<int>(res_b.color_rgba8[c_idx + 2]));
                    if (dr > 1 || dg > 1 || db > 1) {
                        corners_ok = false;
                        break;
                    }
                }
                if (corners_ok) {
                    int c_selected = cascade_select(fixture.lighting.cascades(), view_depth_S);
                    std::printf("[Test 14] Cascade %d: S_px=(%d, %d), Lum A=%.1f B=%.1f, ratio=%.3f, CPU-selected cascade=%d (expected %d)\n",
                                c, px, py, lum_a, lum_b, lum_a / std::max(0.001f, lum_b), c_selected, c);
                    matched_yaw = true;
                    break;
                }
            }
        }
        RON_CHECK(matched_yaw);
    }

    GL_TEST_END();
}

// Test 15: Shadow_NoAcne
RON_TEST(Shadow_NoAcne) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    auto& fixture = test::test_lit_fixture();
    const auto& sampler = test::test_sampler();

    Site sites[8];
    pick_land_sites(sampler, 8, 400.0f, sites);
    Vec3 focus{sites[0].x, sampler.height_m(sites[0].x, sites[0].z), sites[0].z};

    // 6 models: first infantry, first vehicle, first air, first naval, first building, last vehicle
    int first_inf = -1, first_veh = -1, first_air = -1, first_nav = -1, first_bld = -1, last_veh = -1;
    for (int i = 0; i < fixture.assets.model_count(); ++i) {
        u8 cat = fixture.assets.model(i).category;
        if (cat == 0 && first_inf < 0) first_inf = i;
        if (cat == 1) {
            if (first_veh < 0) first_veh = i;
            last_veh = i;
        }
        if (cat == 2 && first_air < 0) first_air = i;
        if (cat == 3 && first_nav < 0) first_nav = i;
        if (cat == 4 && first_bld < 0) first_bld = i;
    }

    const int models[6] = {first_inf, first_veh, first_air, first_nav, first_bld, last_veh};
    for (int mid : models) {
        RON_CHECK(mid >= 0);
    }

    const float dist = 150.0f;
    const float yaw = 0.4f;

    // Render baseline with NO entities and shadows OFF
    test::LitOptions opt_base{};
    opt_base.focus = focus;
    opt_base.dist = dist;
    opt_base.pitch = 45.0f;
    opt_base.yaw = yaw;
    opt_base.sun_elev = 55.0f;
    opt_base.sun_az = 135.0f;
    opt_base.enable_lighting = true;
    opt_base.light_flags = 0; // both shadows OFF
    opt_base.draw_entities = false;
    opt_base.entities.clear();

    auto res_base = fixture.render(opt_base);

    for (int mid : models) {
        const ModelCpu& model = fixture.assets.model(mid);
        float radius_w = 0.05f * dist;
        float scale = radius_w / std::max(0.001f, model.sphere[3]);

        EntityInstance inst{};
        inst.pos_prev[0] = focus.x; inst.pos_prev[1] = focus.y; inst.pos_prev[2] = focus.z;
        inst.pos_curr[0] = focus.x; inst.pos_curr[1] = focus.y; inst.pos_curr[2] = focus.z;
        inst.yaw_prev = 0.0f; inst.yaw_curr = 0.0f;
        inst.scale = scale;
        inst.ids = entity_pack_ids(mid, 1, 0);
        inst.clip = 0;
        inst.anim_t = 0.0f;
        inst.hp = 0.0f;

        // Render with Entity ON, shadows OFF
        test::LitOptions opt_off = opt_base;
        opt_off.draw_entities = true;
        opt_off.entities = { inst };
        auto res_off = fixture.render(opt_off);

        // Render with Entity ON, entity shadows ON
        test::LitOptions opt_on = opt_off;
        opt_on.light_flags = LIGHT_FLAG_CSM; // entity shadows ON, terrain shadows OFF
        auto res_on = fixture.render(opt_on);

        // Identify entity pixels: differ > 2 levels between entity-on and entity-off
        u32 entity_pixels = 0;
        double sum_on = 0.0;
        double sum_off = 0.0;

        for (size_t p = 0; p < 640 * 360; ++p) {
            size_t idx = p * 4;
            int dr = std::abs(static_cast<int>(res_off.color_rgba8[idx])     - static_cast<int>(res_base.color_rgba8[idx]));
            int dg = std::abs(static_cast<int>(res_off.color_rgba8[idx + 1]) - static_cast<int>(res_base.color_rgba8[idx + 1]));
            int db = std::abs(static_cast<int>(res_off.color_rgba8[idx + 2]) - static_cast<int>(res_base.color_rgba8[idx + 2]));

            if (dr > 2 || dg > 2 || db > 2) {
                float lum_on  = test::calc_luminance(res_on.color_rgba8[idx], res_on.color_rgba8[idx + 1], res_on.color_rgba8[idx + 2]);
                float lum_off = test::calc_luminance(res_off.color_rgba8[idx], res_off.color_rgba8[idx + 1], res_off.color_rgba8[idx + 2]);
                sum_on += lum_on;
                sum_off += lum_off;
                ++entity_pixels;
            }
        }

        RON_CHECK(entity_pixels >= 40);
        float mean_on  = static_cast<float>(sum_on / entity_pixels);
        float mean_off = static_cast<float>(sum_off / entity_pixels);
        float ratio = mean_on / std::max(0.001f, mean_off);

        std::printf("[Test 15] Model %d (%s): entity_pixels=%u, Mean ON=%.2f OFF=%.2f, Ratio=%.3f\n",
                    mid, model.name.c_str(), entity_pixels, mean_on, mean_off, ratio);

        RON_CHECK(mean_on >= 0.75f * mean_off);
    }

    GL_TEST_END();
}

// Test 16: Shadow_Terrain_Scene
RON_TEST(Shadow_Terrain_Scene) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    auto& fixture = test::test_lit_fixture();
    const auto& sampler = test::test_sampler();

    const float elevations[4] = {10.0f, 15.0f, 22.0f, 30.0f};

    bool found = false;
    float selected_elev = 0.0f;
    Vec3 P{0.0f, 0.0f, 0.0f};
    Vec3 Q{0.0f, 0.0f, 0.0f};

    for (float elev : elevations) {
        FrameUniforms fu{};
        set_sun(fu, elev, 135.0f);
        float L[3];
        light_to_sun(fu, L);

        // Search land points on 128m lattice with height in [5, 250]
        for (float z = 128.0f; z < 3968.0f && !found; z += 128.0f) {
            for (float x = 128.0f; x < 3968.0f && !found; x += 128.0f) {
                float y = sampler.height_m(x, z);
                if (y < 5.0f || y > 250.0f) continue;

                // Normal via central differences over +-4 m
                float hr = sampler.height_m(x + 4.0f, z);
                float hl = sampler.height_m(x - 4.0f, z);
                float hu = sampler.height_m(x, z + 4.0f);
                float hd = sampler.height_m(x, z - 4.0f);
                Vec3 norm{-(hr - hl), 8.0f, -(hu - hd)};
                float nlen = std::sqrt(norm.x * norm.x + norm.y * norm.y + norm.z * norm.z);
                if (nlen < 1e-4f) continue;
                norm.x /= nlen; norm.y /= nlen; norm.z /= nlen;

                if (dot3(norm, Vec3{L[0], L[1], L[2]}) < 0.15f) continue;

                float shadow_p = terrain_shadow_ref(sampler, x, y, z, L, 48);
                if (shadow_p > 0.05f) continue;

                // Look for Q within 600m
                for (float qz = z - 600.0f; qz <= z + 600.0f && !found; qz += 128.0f) {
                    if (qz < 128.0f || qz > 3968.0f) continue;
                    for (float qx = x - 600.0f; qx <= x + 600.0f && !found; qx += 128.0f) {
                        if (qx < 128.0f || qx > 3968.0f) continue;
                        float qy = sampler.height_m(qx, qz);
                        if (qy < 5.0f || qy > 250.0f) continue;

                        float qhr = sampler.height_m(qx + 4.0f, qz);
                        float qhl = sampler.height_m(qx - 4.0f, qz);
                        float qhu = sampler.height_m(qx, qz + 4.0f);
                        float qhd = sampler.height_m(qx, qz - 4.0f);
                        Vec3 qnorm{-(qhr - qhl), 8.0f, -(qhu - qhd)};
                        float qnlen = std::sqrt(qnorm.x * qnorm.x + qnorm.y * qnorm.y + qnorm.z * qnorm.z);
                        if (qnlen < 1e-4f) continue;
                        qnorm.x /= qnlen; qnorm.y /= qnlen; qnorm.z /= qnlen;

                        if (dot3(qnorm, Vec3{L[0], L[1], L[2]}) < 0.15f) continue;

                        float shadow_q = terrain_shadow_ref(sampler, qx, qy, qz, L, 48);
                        if (shadow_q >= 0.99f) {
                            found = true;
                            selected_elev = elev;
                            P = Vec3{x, y, z};
                            Q = Vec3{qx, qy, qz};
                            break;
                        }
                    }
                }
            }
        }
        if (found) break;
    }

    RON_CHECK(found);
    std::printf("[Test 16] Found pair at elev=%.1f: P=(%.1f, %.1f, %.1f), Q=(%.1f, %.1f, %.1f)\n",
                selected_elev, P.x, P.y, P.z, Q.x, Q.y, Q.z);

    // Test P: focus P, dist 150
    test::LitOptions opt_p_on{};
    opt_p_on.focus = P;
    opt_p_on.dist = 150.0f;
    opt_p_on.pitch = 45.0f;
    opt_p_on.yaw = 0.0f;
    opt_p_on.sun_elev = selected_elev;
    opt_p_on.sun_az = 135.0f;
    opt_p_on.enable_lighting = true;
    opt_p_on.light_flags = LIGHT_FLAG_TERRAIN_SHADOW; // terrain shadows ON, entity shadows OFF
    opt_p_on.draw_entities = false;

    auto res_p_on = fixture.render(opt_p_on);

    test::LitOptions opt_p_off = opt_p_on;
    opt_p_off.light_flags = 0; // terrain shadows OFF
    auto res_p_off = fixture.render(opt_p_off);

    float p_lum_on  = test::calc_mean_luminance(res_p_on.color_rgba8, 318, 178, 5, 5);
    float p_lum_off = test::calc_mean_luminance(res_p_off.color_rgba8, 318, 178, 5, 5);

    std::printf("[Test 16] Point P screen center 5x5: ON=%.2f, OFF=%.2f, diff=%.2f\n",
                p_lum_on, p_lum_off, p_lum_off - p_lum_on);

    RON_CHECK(p_lum_on <= 0.97f * p_lum_off);
    RON_CHECK((p_lum_off - p_lum_on) >= 2.0f);

    // Test Q: focus Q, dist 150
    test::LitOptions opt_q_on = opt_p_on;
    opt_q_on.focus = Q;
    auto res_q_on = fixture.render(opt_q_on);

    test::LitOptions opt_q_off = opt_p_off;
    opt_q_off.focus = Q;
    auto res_q_off = fixture.render(opt_q_off);

    float q_lum_on  = test::calc_mean_luminance(res_q_on.color_rgba8, 318, 178, 5, 5);
    float q_lum_off = test::calc_mean_luminance(res_q_off.color_rgba8, 318, 178, 5, 5);

    std::printf("[Test 16] Point Q screen center 5x5: ON=%.2f, OFF=%.2f, abs_diff=%.2f\n",
                q_lum_on, q_lum_off, std::abs(q_lum_on - q_lum_off));

    RON_CHECK(std::abs(q_lum_on - q_lum_off) <= 2.0f);

    GL_TEST_END();
}

// Test 17: Shadow_FlagsNeutral
RON_TEST(Shadow_FlagsNeutral) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    auto& fixture = test::test_lit_fixture();
    const auto& sampler = test::test_sampler();

    Site sites[8];
    pick_land_sites(sampler, 8, 400.0f, sites);
    Vec3 focus{sites[0].x, sampler.height_m(sites[0].x, sites[0].z), sites[0].z};

    // Render ON: entity shadows ON (terrain shadows OFF), NO entities
    test::LitOptions opt_on{};
    opt_on.focus = focus;
    opt_on.dist = 200.0f;
    opt_on.pitch = 45.0f;
    opt_on.yaw = 0.0f;
    opt_on.sun_elev = 40.0f;
    opt_on.sun_az = 135.0f;
    opt_on.enable_lighting = true;
    opt_on.light_flags = LIGHT_FLAG_CSM; // entity shadows ON
    opt_on.draw_entities = false;
    opt_on.entities.clear();

    auto res_on = fixture.render(opt_on);

    // Render OFF: both shadows OFF, NO entities
    test::LitOptions opt_off = opt_on;
    opt_off.light_flags = 0; // both shadows OFF

    auto res_off = fixture.render(opt_off);

    // Compare all 640x360 pixels
    const size_t total_pixels = 640 * 360;
    double sum_diff_r = 0.0, sum_diff_g = 0.0, sum_diff_b = 0.0;
    int max_diff = 0;
    double sum_lum = 0.0;
    double sum_lum_sq = 0.0;

    for (size_t i = 0; i < total_pixels; ++i) {
        size_t idx = i * 4;
        int dr = std::abs(static_cast<int>(res_on.color_rgba8[idx])     - static_cast<int>(res_off.color_rgba8[idx]));
        int dg = std::abs(static_cast<int>(res_on.color_rgba8[idx + 1]) - static_cast<int>(res_off.color_rgba8[idx + 1]));
        int db = std::abs(static_cast<int>(res_on.color_rgba8[idx + 2]) - static_cast<int>(res_off.color_rgba8[idx + 2]));

        sum_diff_r += dr;
        sum_diff_g += dg;
        sum_diff_b += db;
        max_diff = std::max({max_diff, dr, dg, db});

        float lum = test::calc_luminance(res_on.color_rgba8[idx], res_on.color_rgba8[idx + 1], res_on.color_rgba8[idx + 2]);
        sum_lum += lum;
        sum_lum_sq += lum * lum;
    }

    double mean_diff_r = sum_diff_r / total_pixels;
    double mean_diff_g = sum_diff_g / total_pixels;
    double mean_diff_b = sum_diff_b / total_pixels;

    double mean_lum = sum_lum / total_pixels;
    double variance = (sum_lum_sq / total_pixels) - (mean_lum * mean_lum);
    double std_lum = std::sqrt(std::max(0.0, variance));

    std::printf("[Test 17] mean diffs: R=%.4f, G=%.4f, B=%.4f (req <= 0.25), max_diff=%d (req <= 3)\n",
                mean_diff_r, mean_diff_g, mean_diff_b, max_diff);
    std::printf("[Test 17] mean_lum=%.2f (req in [8, 247]), std_lum=%.2f (req >= 6)\n",
                mean_lum, std_lum);

    RON_CHECK(mean_diff_r <= 0.25);
    RON_CHECK(mean_diff_g <= 0.25);
    RON_CHECK(mean_diff_b <= 0.25);
    RON_CHECK(max_diff <= 3);
    RON_CHECK(mean_lum >= 8.0 && mean_lum <= 247.0);
    RON_CHECK(std_lum >= 6.0);

    GL_TEST_END();
}

} // namespace ron::render
