#include "test_framework.hpp"
#include "render/overlay_types.hpp"
#include "render/territory_map.hpp"
#include "render/fog_map.hpp"
#include "render/crater_math.hpp"
#include "render/minimap_cpu.hpp"
#include "render/camera_rts.hpp"
#include "render/scene_fixture.hpp"
#include "core/hash.hpp"
#include "core/assert_log.hpp"
#include <vector>
#include <cmath>
#include <cstring>
#include <random>
#include <cstdio>

namespace ron::render {

RON_TEST(Ovl_Layout) {
    RON_CHECK_EQ(sizeof(CraterSplat), 32u);
    RON_CHECK_EQ(sizeof(Blip), 16u);
    RON_CHECK_EQ(sizeof(OverlayUbo), 320u);
    RON_CHECK_EQ(sizeof(MinimapUbo), 48u);

    RON_CHECK_EQ(offsetof(OverlayUbo, territory_on), 256u);
    RON_CHECK_EQ(offsetof(OverlayUbo, fog_on), 272u);
    RON_CHECK_EQ(offsetof(OverlayUbo, world_size), 288u);
    RON_CHECK_EQ(offsetof(OverlayUbo, local_player), 304u);

    Vec4 p1 = overlay_palette(1);
    RON_CHECK_EQ(p1.x, 0.86f);
    RON_CHECK_EQ(p1.y, 0.16f);
    RON_CHECK_EQ(p1.z, 0.16f);

    Vec4 p2 = overlay_palette(2);
    RON_CHECK_EQ(p2.x, 0.15f);
    RON_CHECK_EQ(p2.y, 0.38f);
    RON_CHECK_EQ(p2.z, 0.92f);

    RON_CHECK_EQ(BIND_UBO_OVERLAY, 2u);
    RON_CHECK_EQ(BIND_UBO_MINIMAP, 3u);
    RON_CHECK_EQ(BIND_SSBO_SPLATS, 5u);
    RON_CHECK_EQ(BIND_SSBO_BLIPS, 8u);
    RON_CHECK_EQ(TEX_TERRITORY_SDF, 7u);
    RON_CHECK_EQ(TEX_TERRITORY_OWNER, 8u);
    RON_CHECK_EQ(TEX_CRATER, 9u);
    RON_CHECK_EQ(TEX_FOG, 10u);
    RON_CHECK_EQ(TEX_MINIMAP_BASE, 11u);
    RON_CHECK_EQ(TEX_FOG_TVIS, 12u);
    RON_CHECK_EQ(TEX_FOG_TEXP, 13u);
    RON_CHECK_EQ(IMG_JFA_IN, 0u);
    RON_CHECK_EQ(IMG_JFA_OUT, 1u);
    RON_CHECK_EQ(IMG_SDF, 2u);
    RON_CHECK_EQ(IMG_CRATER, 3u);
    RON_CHECK_EQ(IMG_FOG_SMOOTH, 4u);
}

RON_TEST(Territory_Raster) {
    TerritorySource sources[2] = {
        { 1000.0f, 1000.0f, 300.0f, 1 },
        { 1400.0f, 1000.0f, 300.0f, 2 }
    };

    std::vector<u8> map1(512 * 512, 0);
    std::vector<u8> map2(512 * 512, 0);

    territory_rasterize(sources, 2, nullptr, map1.data());
    territory_rasterize(sources, 2, nullptr, map2.data());

    RON_CHECK(std::memcmp(map1.data(), map2.data(), map1.size()) == 0);

    auto cell_owner = [&](const std::vector<u8>& map, float x, float z) -> u8 {
        int i = std::clamp(static_cast<int>(x / 8.0f), 0, 511);
        int j = std::clamp(static_cast<int>(z / 8.0f), 0, 511);
        return map[j * 512 + i];
    };

    RON_CHECK_EQ(cell_owner(map1, 1000.0f, 1000.0f), 1);
    RON_CHECK_EQ(cell_owner(map1, 1400.0f, 1000.0f), 2);
    RON_CHECK_EQ(cell_owner(map1, 1100.0f, 1000.0f), 1);
    RON_CHECK_EQ(cell_owner(map1, 1300.0f, 1000.0f), 2);
    RON_CHECK_EQ(cell_owner(map1, 3000.0f, 3000.0f), 0);

    int count1 = 0, count2 = 0;
    for (u8 o : map1) {
        if (o == 1) count1++;
        if (o == 2) count2++;
    }
    RON_CHECK(count1 > 0);
    RON_CHECK(count2 > 0);

    // With the Gate 10 fixture HeightSampler as land mask: NO owned cell has height(centre) < 0.
    const auto& sampler = test::test_sampler();
    std::vector<u8> land_map(512 * 512, 0);
    territory_rasterize(sources, 2, &sampler, land_map.data());

    for (int j = 0; j < 512; ++j) {
        float cz = (static_cast<float>(j) + 0.5f) * 8.0f;
        for (int i = 0; i < 512; ++i) {
            u8 o = land_map[j * 512 + i];
            if (o != 0) {
                float cx = (static_cast<float>(i) + 0.5f) * 8.0f;
                RON_CHECK(sampler.height_m(cx, cz) >= 0.0f);
            }
        }
    }
}

RON_TEST(Territory_Sites) {
    const auto& sampler = test::test_sampler();
    Site sites1[8];
    Site sites2[8];

    int placed1 = pick_land_sites(sampler, 8, 400.0f, sites1);
    RON_CHECK_EQ(placed1, 8);

    int placed2 = pick_land_sites(sampler, 8, 400.0f, sites2);
    RON_CHECK_EQ(placed2, 8);

    RON_CHECK(std::memcmp(sites1, sites2, sizeof(sites1)) == 0);

    for (int i = 0; i < 8; ++i) {
        float h = sampler.height_m(sites1[i].x, sites1[i].z);
        RON_CHECK(h >= 5.0f);

        for (int j = i + 1; j < 8; ++j) {
            float dx = sites1[i].x - sites1[j].x;
            float dz = sites1[i].z - sites1[j].z;
            float dist = std::sqrt(dx * dx + dz * dz);
            RON_CHECK(dist >= 400.0f);
        }
    }
}

RON_TEST(Fog_Reveal) {
    FogMap fog;

    // reveal_circle(100, 100, 40)
    fog.reveal_circle(100.0f, 100.0f, 40.0f);

    // Independent brute force count
    int exp_count = 0;
    i64 r_mm = 40000;
    i64 r2 = r_mm * r_mm;
    for (int cz = 0; cz < 512; ++cz) {
        i64 cz_mm = static_cast<i64>(cz) * 8000 + 4000;
        i64 dz = cz_mm - 100000;
        for (int cx = 0; cx < 512; ++cx) {
            i64 cx_mm = static_cast<i64>(cx) * 8000 + 4000;
            i64 dx = cx_mm - 100000;
            if (dx * dx + dz * dz <= r2) {
                exp_count++;
            }
        }
    }

    int act_count = 0;
    const u8* vis = fog.visible_data();
    for (int i = 0; i < 512 * 512; ++i) {
        if (vis[i] != 0) act_count++;
    }
    RON_CHECK_EQ(act_count, exp_count);

    // centre cell (12,12) visible
    RON_CHECK_EQ(vis[12 * 512 + 12], 255);

    // r=3 m at (100,100) (exact cell centre) -> exactly 1 cell
    fog.begin_frame();
    fog.reveal_circle(100.0f, 100.0f, 3.0f);
    int count_center = 0;
    for (int i = 0; i < 512 * 512; ++i) {
        if (vis[i] != 0) count_center++;
    }
    RON_CHECK_EQ(count_center, 1);
    RON_CHECK_EQ(vis[12 * 512 + 12], 255);

    // r=3 at (96,96) (a cell corner) -> 0 cells
    fog.begin_frame();
    fog.reveal_circle(96.0f, 96.0f, 3.0f);
    int count_corner = 0;
    for (int i = 0; i < 512 * 512; ++i) {
        if (vis[i] != 0) count_corner++;
    }
    RON_CHECK_EQ(count_corner, 0);

    // reveal_circle(0,0,50) does not crash and reveals >= 1 cell
    fog.begin_frame();
    fog.reveal_circle(0.0f, 0.0f, 50.0f);
    int count_00 = 0;
    for (int i = 0; i < 512 * 512; ++i) {
        if (vis[i] != 0) count_00++;
    }
    RON_CHECK(count_00 >= 1);

    // Edge clip at (4095,4095,r50) likewise
    fog.begin_frame();
    fog.reveal_circle(4095.0f, 4095.0f, 50.0f);
    int count_edge = 0;
    for (int i = 0; i < 512 * 512; ++i) {
        if (vis[i] != 0) count_edge++;
    }
    RON_CHECK(count_edge >= 1);
}

RON_TEST(Fog_Explored) {
    FogMap fog;
    fog.reveal_circle(100.0f, 100.0f, 40.0f);
    fog.commit();

    RON_CHECK(fog.consume_dirty());
    RON_CHECK(!fog.consume_dirty());

    int explored_before = 0;
    const u8* exp_data = fog.explored_data();
    for (int i = 0; i < 512 * 512; ++i) {
        if (exp_data[i] != 0) explored_before++;
    }
    RON_CHECK(explored_before > 0);

    fog.begin_frame();
    const u8* vis = fog.visible_data();
    int vis_after = 0;
    for (int i = 0; i < 512 * 512; ++i) {
        if (vis[i] != 0) vis_after++;
    }
    RON_CHECK_EQ(vis_after, 0);

    int explored_after = 0;
    for (int i = 0; i < 512 * 512; ++i) {
        if (exp_data[i] != 0) explored_after++;
    }
    RON_CHECK_EQ(explored_after, explored_before);

    // Explored never decreases across 10 random frames
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> dist_pos(0.0f, 4096.0f);
    std::uniform_real_distribution<float> dist_r(10.0f, 100.0f);

    int prev_count = explored_after;
    for (int frame = 0; frame < 10; ++frame) {
        fog.begin_frame();
        float x = dist_pos(rng);
        float z = dist_pos(rng);
        float r = dist_r(rng);
        fog.reveal_circle(x, z, r);
        fog.commit();

        int cur_count = 0;
        for (int i = 0; i < 512 * 512; ++i) {
            if (exp_data[i] != 0) cur_count++;
        }
        RON_CHECK(cur_count >= prev_count);
        prev_count = cur_count;
    }

    // consume_dirty() true after a commit that changed explored, then false until the next change
    fog.consume_dirty(); // clear any pending dirty
    // Commit without any changes
    fog.commit();
    RON_CHECK(!fog.consume_dirty());

    // Reveal new cells and commit
    fog.reveal_circle(2000.0f, 2000.0f, 50.0f);
    fog.commit();
    RON_CHECK(fog.consume_dirty());
    RON_CHECK(!fog.consume_dirty());

    // reveal_all() sets both to 255
    fog.reveal_all();
    for (int i = 0; i < 512 * 512; ++i) {
        RON_CHECK_EQ(vis[i], 255);
        RON_CHECK_EQ(exp_data[i], 255);
    }
}

RON_TEST(Crater_Profile) {
    CraterSplat s;
    s.x = 1000.0f;
    s.z = 1000.0f;
    s.radius = 30.0f;
    s.depth = 4.0f;
    s.scorch = 1.0f;
    s.scorch_mul = 2.0f;
    s.seed = 0xBEEF1234u;
    s.pad = 0;

    // offset at r=0 equals -4 within 1e-4
    float off0 = crater_offset_m(s, s.x, s.z);
    RON_CHECK(std::abs(off0 - (-4.0f)) < 1e-4f);

    // At r=3R offset |.| < 1e-3
    float off3r = crater_offset_m(s, s.x + 3.0f * s.radius, s.z);
    RON_CHECK(std::abs(off3r) < 1e-3f);

    // For 64 angles: offset(0.25R)<offset(0.5R)<offset(0.75R) (use the same angle) and all in (-4,0)
    for (int a = 0; a < 64; ++a) {
        float ang = static_cast<float>(a) * (2.0f * 3.14159265f / 64.0f);
        float ca = std::cos(ang);
        float sa = std::sin(ang);

        float off_25 = crater_offset_m(s, s.x + 0.25f * s.radius * ca, s.z + 0.25f * s.radius * sa);
        float off_50 = crater_offset_m(s, s.x + 0.50f * s.radius * ca, s.z + 0.50f * s.radius * sa);
        float off_75 = crater_offset_m(s, s.x + 0.75f * s.radius * ca, s.z + 0.75f * s.radius * sa);

        RON_CHECK(off_25 < off_50);
        RON_CHECK(off_50 < off_75);
        RON_CHECK(off_25 > -4.0f && off_25 < 0.0f);
        RON_CHECK(off_50 > -4.0f && off_50 < 0.0f);
        RON_CHECK(off_75 > -4.0f && off_75 < 0.0f);
    }

    // There exists r in [R, 1.4R] with offset >= 0.12*depth
    bool found_rim = false;
    for (float r = s.radius; r <= 1.4f * s.radius; r += 0.1f) {
        float off_rim = crater_offset_m(s, s.x + r, s.z);
        if (off_rim >= 0.12f * s.depth) {
            found_rim = true;
            break;
        }
    }
    RON_CHECK(found_rim);

    // scorch at r=0 >= 0.85; scorch at r >= scorch_mul*R is 0
    float sc0 = crater_scorch(s, s.x, s.z);
    RON_CHECK(sc0 >= 0.85f);

    float sc_far = crater_scorch(s, s.x + s.scorch_mul * s.radius, s.z);
    RON_CHECK_EQ(sc_far, 0.0f);
    float sc_farther = crater_scorch(s, s.x + (s.scorch_mul + 0.5f) * s.radius, s.z);
    RON_CHECK_EQ(sc_farther, 0.0f);
}

RON_TEST(Crater_Apply) {
    int n = 2048;
    CraterSplat s;
    s.x = 1001.0f;
    s.z = 1001.0f;
    s.radius = 30.0f;
    s.depth = 4.0f;
    s.scorch = 1.0f;
    s.scorch_mul = 1.5f;
    s.seed = 7;
    s.pad = 0;

    std::vector<u8> img(n * n * 4, 0);
    crater_clear(img.data(), n);

    crater_apply_cpu(img.data(), n, s);

    // Centre texel (500,500) R code==64 exactly, G >= 217
    int idx_center = (500 * n + 500) * 4;
    RON_CHECK_EQ(img[idx_center + 0], 64);
    RON_CHECK(img[idx_center + 1] >= 217);

    // Every texel outside extent_m+4 m is exactly (128,0,0,0)
    float cell = 4096.0f / static_cast<float>(n);
    float extent = crater_extent_m(s);
    float r_safe = extent + 4.0f;
    float r_safe2 = r_safe * r_safe;

    for (int j = 0; j < n; ++j) {
        float pz = (static_cast<float>(j) + 0.5f) * cell;
        float dz = pz - s.z;
        for (int i = 0; i < n; ++i) {
            float px = (static_cast<float>(i) + 0.5f) * cell;
            float dx = px - s.x;
            if (dx * dx + dz * dz > r_safe2) {
                int idx = (j * n + i) * 4;
                RON_CHECK_EQ(img[idx + 0], 128);
                RON_CHECK_EQ(img[idx + 1], 0);
                RON_CHECK_EQ(img[idx + 2], 0);
                RON_CHECK_EQ(img[idx + 3], 0);
            }
        }
    }

    // Apply the same splat again: centre code==0
    crater_apply_cpu(img.data(), n, s);
    RON_CHECK_EQ(img[idx_center + 0], 0);

    // A third time: still 0 (no wrap)
    crater_apply_cpu(img.data(), n, s);
    RON_CHECK_EQ(img[idx_center + 0], 0);

    // crater_sanitize clamps radius 500 -> 64 and depth -3 -> 0
    CraterSplat bad;
    bad.radius = 500.0f;
    bad.depth = -3.0f;
    bad.scorch = 2.0f;
    bad.scorch_mul = 0.5f;
    bad.seed = 0;
    bad.pad = 0;
    CraterSplat sanitized = crater_sanitize(bad);
    RON_CHECK_EQ(sanitized.radius, 64.0f);
    RON_CHECK_EQ(sanitized.depth, 0.0f);
    RON_CHECK_EQ(sanitized.scorch, 1.0f);
    RON_CHECK_EQ(sanitized.scorch_mul, 1.0f);
}

RON_TEST(Minimap_Bake) {
    const auto& sampler = test::test_sampler();
    std::vector<u8> bake1(512 * 512 * 4, 0);
    std::vector<u8> bake2(512 * 512 * 4, 0);

    minimap_bake(sampler, bake1.data());
    minimap_bake(sampler, bake2.data());

    RON_CHECK(std::memcmp(bake1.data(), bake2.data(), bake1.size()) == 0);

    // all alpha 255
    for (int i = 0; i < 512 * 512; ++i) {
        RON_CHECK_EQ(bake1[i * 4 + 3], 255);
    }

    // find one cell with h<-20 and one with h in [5,100]
    int sea_idx = -1;
    int land_idx = -1;

    for (int j = 0; j < 512; ++j) {
        float cz = (static_cast<float>(j) + 0.5f) * 8.0f;
        for (int i = 0; i < 512; ++i) {
            float cx = (static_cast<float>(i) + 0.5f) * 8.0f;
            float h = sampler.height_m(cx, cz);
            if (h < -20.0f && sea_idx < 0) {
                sea_idx = (j * 512 + i) * 4;
            }
            if (h >= 5.0f && h <= 100.0f && land_idx < 0) {
                land_idx = (j * 512 + i) * 4;
            }
        }
        if (sea_idx >= 0 && land_idx >= 0) break;
    }

    RON_CHECK(sea_idx >= 0);
    RON_CHECK(land_idx >= 0);

    // sea pixel has b > r+20 and b > g
    u8 sea_r = bake1[sea_idx + 0];
    u8 sea_g = bake1[sea_idx + 1];
    u8 sea_b = bake1[sea_idx + 2];
    RON_CHECK(sea_b > sea_r + 20);
    RON_CHECK(sea_b > sea_g);

    // land pixel has g > b
    u8 land_g = bake1[land_idx + 1];
    u8 land_b = bake1[land_idx + 2];
    RON_CHECK(land_g > land_b);

    u64 h64 = ron::xxh64(std::span<const u8>(bake1.data(), bake1.size()));
    std::printf("Minimap_Bake xxh64: 0x%016llx\n", static_cast<unsigned long long>(h64));
}

RON_TEST(Minimap_Pick) {
    PxRect rect{ 100.0f, 50.0f, 256.0f, 256.0f };
    float wx = 0.0f, wz = 0.0f;

    // pick(100,50)->(0,0)
    RON_CHECK(minimap_pick(100.0f, 50.0f, rect, &wx, &wz));
    RON_CHECK(std::abs(wx - 0.0f) < 1e-3f);
    RON_CHECK(std::abs(wz - 0.0f) < 1e-3f);

    // pick(228,178)->(2048,2048) within 1e-3
    RON_CHECK(minimap_pick(228.0f, 178.0f, rect, &wx, &wz));
    RON_CHECK(std::abs(wx - 2048.0f) < 1e-3f);
    RON_CHECK(std::abs(wz - 2048.0f) < 1e-3f);

    // pick(99,60) false
    RON_CHECK(!minimap_pick(99.0f, 60.0f, rect, &wx, &wz));

    // pick(356.5,100) false
    RON_CHECK(!minimap_pick(356.5f, 100.0f, rect, &wx, &wz));

    // round trip world_to_px/pick for 20 deterministic points within 1e-3
    for (int k = 0; k < 20; ++k) {
        float test_wx = 100.0f + static_cast<float>(k) * 195.0f;
        float test_wz = 50.0f + static_cast<float>(k) * 180.0f;
        float px = 0.0f, py = 0.0f;
        minimap_world_to_px(test_wx, test_wz, rect, &px, &py);

        float back_wx = 0.0f, back_wz = 0.0f;
        RON_CHECK(minimap_pick(px, py, rect, &back_wx, &back_wz));
        RON_CHECK(std::abs(back_wx - test_wx) < 1e-3f);
        RON_CHECK(std::abs(back_wz - test_wz) < 1e-3f);
    }
}

RON_TEST(Minimap_Quad) {
    const auto& sampler = test::test_sampler();
    Site sites[8];
    int placed = pick_land_sites(sampler, 8, 400.0f, sites);
    RON_CHECK_EQ(placed, 8);

    CameraRts cam;
    camera_init(cam, sites[0].x, sites[0].z, 0.0f, 400.0f);
    camera_snap(cam, &sampler);

    float aspect = 1.0f;
    Mat4 view = camera_view(cam, &sampler);
    Mat4 proj = camera_proj(aspect);
    Mat4 vp = mat4_mul(proj, view);
    Mat4 inv_vp;
    RON_CHECK(mat4_inverse(vp, inv_vp));

    Vec3 eye = camera_eye(cam, &sampler);
    float cam_pos[3] = { eye.x, eye.y, eye.z };
    float quad[8];
    camera_ground_quad(inv_vp.m, cam_pos, sampler, quad);

    // All 8 numbers finite and within [-20000, 24096]
    for (int i = 0; i < 8; ++i) {
        RON_CHECK(std::isfinite(quad[i]));
        RON_CHECK(quad[i] >= -20000.0f && quad[i] <= 24096.0f);
    }

    struct Pt2 { float x, z; };
    Pt2 bl{ quad[0], quad[1] };
    Pt2 br{ quad[2], quad[3] };
    Pt2 tr{ quad[4], quad[5] };
    Pt2 tl{ quad[6], quad[7] };
    Pt2 focus{ sites[0].x, sites[0].z };

    auto dist2d = [](Pt2 a, Pt2 b) {
        float dx = a.x - b.x;
        float dz = a.z - b.z;
        return std::sqrt(dx * dx + dz * dz);
    };

    float d_bottom = 0.5f * (dist2d(focus, bl) + dist2d(focus, br));
    float d_top = 0.5f * (dist2d(focus, tr) + dist2d(focus, tl));
    RON_CHECK(d_top - d_bottom >= 10.0f);

    // Winding test: focus point is inside the quad (tolerance 1 m)
    Pt2 pts[4] = { bl, br, tr, tl };
    bool pos = false, neg = false;
    for (int i = 0; i < 4; ++i) {
        Pt2 p1 = pts[i];
        Pt2 p2 = pts[(i + 1) % 4];
        float edge_x = p2.x - p1.x;
        float edge_z = p2.z - p1.z;
        float to_q_x = focus.x - p1.x;
        float to_q_z = focus.z - p1.z;
        float cross = edge_x * to_q_z - edge_z * to_q_x;
        float edge_len = std::sqrt(edge_x * edge_x + edge_z * edge_z);
        float dist_to_line = cross / (edge_len > 1e-4f ? edge_len : 1.0f);

        if (dist_to_line > 1.0f) pos = true;
        if (dist_to_line < -1.0f) neg = true;
    }
    RON_CHECK(!(pos && neg));
}

} // namespace ron::render
