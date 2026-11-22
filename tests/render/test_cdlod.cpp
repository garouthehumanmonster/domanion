#include "test_framework.hpp"
#include "render/scene_fixture.hpp"
#include "render/terrain_quadtree.hpp"
#include "render/camera_rts.hpp"
#include "render/rmath.hpp"
#include "core/clock.hpp"
#include "core/assert_log.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace ron::render {

RON_TEST(Cdlod_Cover) {
    const auto& qt = test::test_quadtree();
    const auto& sampler = test::test_sampler();

    TerrainNodeGpu nodes[TERRAIN_MAX_SELECTED];

    // (a) No culling, six cameras
    Vec3 cameras[6] = {
        Vec3(2048.0f, 100.0f, 2048.0f),
        Vec3(10.0f, 50.0f, 10.0f),
        Vec3(4000.0f, 300.0f, 100.0f),
        Vec3(-500.0f, 800.0f, 2000.0f),
        Vec3(2048.0f, 2000.0f, 2048.0f),
        Vec3(1000.0f, 5.0f, 3000.0f)
    };

    for (int c = 0; c < 6; ++c) {
        SelectResult res = cdlod_select(qt, nullptr, cameras[c], nodes, TERRAIN_MAX_SELECTED);
        RON_CHECK(!res.overflow);
        RON_CHECK(res.count >= 1 && res.count <= TERRAIN_MAX_SELECTED);

        // Rasterise every node onto 128x128 leaf grid
        u8 grid[128 * 128];
        std::memset(grid, 0, sizeof(grid));

        for (u32 n = 0; n < res.count; ++n) {
            u32 L = static_cast<u32>(nodes[n].level);
            u32 span = 1u << L;
            u32 gx = static_cast<u32>(std::round(nodes[n].x / 32.0f));
            u32 gz = static_cast<u32>(std::round(nodes[n].z / 32.0f));

            for (u32 dz = 0; dz < span; ++dz) {
                for (u32 dx = 0; dx < span; ++dx) {
                    u32 idx = (gz + dz) * 128 + (gx + dx);
                    RON_CHECK(idx < 128 * 128);
                    grid[idx]++;
                }
            }
        }

        // Check every cell is marked exactly once
        for (int i = 0; i < 128 * 128; ++i) {
            RON_CHECK(grid[i] == 1);
        }
    }

    // (b) With culling
    // Find flattest land point by scanning land texels at 32m stride
    Vec3 flattest_pt(2048.0f, 0.0f, 2048.0f);
    float best_flatness = 1e9f;
    u32 tex_stride = static_cast<u32>(std::round(32.0f / sampler.cell_m()));
    u32 tex_step = static_cast<u32>(std::round(8.0f / sampler.cell_m()));

    for (u32 j = tex_step * 4; j < sampler.n() - tex_step * 4; j += tex_stride) {
        for (u32 i = tex_step * 4; i < sampler.n() - tex_step * 4; i += tex_stride) {
            float x = (static_cast<float>(i) + 0.5f) * sampler.cell_m();
            float z = (static_cast<float>(j) + 0.5f) * sampler.cell_m();
            float h_center = sampler.height_m(x, z);
            if (h_center <= 5.0f) continue;

            float sum_h = 0.0f;
            float h_vals[81];
            int idx = 0;
            for (int dz = -4; dz <= 4; ++dz) {
                for (int dx = -4; dx <= 4; ++dx) {
                    float h = sampler.height_m(x + static_cast<float>(dx) * 8.0f, z + static_cast<float>(dz) * 8.0f);
                    h_vals[idx++] = h;
                    sum_h += h;
                }
            }
            float mean_h = sum_h / 81.0f;
            float max_diff = 0.0f;
            for (int k = 0; k < 81; ++k) {
                max_diff = std::max(max_diff, std::abs(h_vals[k] - mean_h));
            }
            if (max_diff < best_flatness) {
                best_flatness = max_diff;
                flattest_pt = Vec3(x, h_center, z);
            }
        }
    }

    Vec3 focus_pts[2] = {
        Vec3(2048.0f, 0.0f, 2048.0f),
        flattest_pt
    };
    float dists[3] = { 100.0f, 400.0f, 1200.0f };
    float yaws[2] = { 0.0f, 2.3f };

    for (int f = 0; f < 2; ++f) {
        for (int d = 0; d < 3; ++d) {
            for (int y = 0; y < 2; ++y) {
                CameraRts cam;
                camera_init(cam, focus_pts[f].x, focus_pts[f].z, yaws[y], dists[d]);
                camera_snap(cam, &sampler);

                Vec3 eye = camera_eye(cam, &sampler);
                Mat4 view = camera_view(cam, &sampler);
                Mat4 proj = camera_proj(16.0f / 9.0f);
                Mat4 view_proj = mat4_mul(proj, view);
                Mat4 inv_view_proj{};
                RON_CHECK(mat4_inverse(view_proj, inv_view_proj));

                Frustum frust = frustum_from_view_proj(view_proj);

                SelectResult unculled_res = cdlod_select(qt, nullptr, eye, nodes, TERRAIN_MAX_SELECTED);
                TerrainNodeGpu culled_nodes[TERRAIN_MAX_SELECTED];
                SelectResult res = cdlod_select(qt, &frust, eye, culled_nodes, TERRAIN_MAX_SELECTED);
                RON_CHECK(!res.overflow);
                RON_CHECK(res.count <= unculled_res.count);

                // Pairwise non-overlap
                for (u32 i = 0; i < res.count; ++i) {
                    for (u32 j = i + 1; j < res.count; ++j) {
                        bool overlap = !(culled_nodes[i].x + culled_nodes[i].size <= culled_nodes[j].x ||
                                         culled_nodes[j].x + culled_nodes[j].size <= culled_nodes[i].x ||
                                         culled_nodes[i].z + culled_nodes[i].size <= culled_nodes[j].z ||
                                         culled_nodes[j].z + culled_nodes[j].size <= culled_nodes[i].z);
                        RON_CHECK(!overlap);
                    }
                }

                // 32x18 grid of pixel centres
                int in_world_hits = 0;
                for (int py_step = 0; py_step < 18; ++py_step) {
                    for (int px_step = 0; px_step < 32; ++px_step) {
                        float px = (static_cast<float>(px_step) + 0.5f) * (1600.0f / 32.0f);
                        float py = (static_cast<float>(py_step) + 0.5f) * (900.0f / 18.0f);
                        float ndc_x = (px + 0.5f) / 1600.0f * 2.0f - 1.0f;
                        float ndc_y = 1.0f - (py + 0.5f) / 900.0f * 2.0f;

                        Vec3 ray_orig{}, ray_dir{};
                        unproject_ray(inv_view_proj, ndc_x, ndc_y, ray_orig, ray_dir);

                        Vec3 hit{};
                        if (pick_terrain(sampler, ray_orig, ray_dir, 16000.0f, hit)) {
                            if (hit.x >= 0.0f && hit.x < 4096.0f && hit.z >= 0.0f && hit.z < 4096.0f) {
                                in_world_hits++;
                                int match_count = 0;
                                for (u32 n = 0; n < res.count; ++n) {
                                    if (hit.x >= culled_nodes[n].x && hit.x < culled_nodes[n].x + culled_nodes[n].size &&
                                        hit.z >= culled_nodes[n].z && hit.z < culled_nodes[n].z + culled_nodes[n].size) {
                                        match_count++;
                                    }
                                }
                                RON_CHECK(match_count == 1);
                            }
                        }
                    }
                }
                RON_CHECK(in_world_hits >= static_cast<int>(576 * 0.95f));
            }
        }
    }
}

RON_TEST(Cdlod_Invariants) {
    const auto& qt = test::test_quadtree();
    const auto& sampler = test::test_sampler();

    TerrainNodeGpu nodes[TERRAIN_MAX_SELECTED];

    // (a) For unculled selections: every node with level k > 0 has dist > range[k-1]^2
    Vec3 cameras[6] = {
        Vec3(2048.0f, 100.0f, 2048.0f),
        Vec3(10.0f, 50.0f, 10.0f),
        Vec3(4000.0f, 300.0f, 100.0f),
        Vec3(-500.0f, 800.0f, 2000.0f),
        Vec3(2048.0f, 2000.0f, 2048.0f),
        Vec3(1000.0f, 5.0f, 3000.0f)
    };

    for (int c = 0; c < 6; ++c) {
        SelectResult res = cdlod_select(qt, nullptr, cameras[c], nodes, TERRAIN_MAX_SELECTED);
        for (u32 n = 0; n < res.count; ++n) {
            u32 k = static_cast<u32>(nodes[n].level);
            if (k > 0) {
                float minh = 0.0f, maxh = 0.0f;
                u32 ix = static_cast<u32>(std::round(nodes[n].x / nodes[n].size));
                u32 iz = static_cast<u32>(std::round(nodes[n].z / nodes[n].size));
                qt.bounds(k, ix, iz, minh, maxh);

                Vec3 mn(nodes[n].x, minh, nodes[n].z);
                Vec3 mx(nodes[n].x + nodes[n].size, maxh, nodes[n].z + nodes[n].size);

                float r_prev = terrain_lod_range(k - 1);
                RON_CHECK(aabb_dist_sq(cameras[c], mn, mx) > r_prev * r_prev);
            }
        }
    }

    // (b) Camera ascending above highest texel
    float H = sampler.max_m();
    float px = 0.0f, pz = 0.0f;
    float cell = sampler.cell_m();
    bool found_peak = false;
    for (u32 j = 0; j < sampler.n() && !found_peak; ++j) {
        for (u32 i = 0; i < sampler.n() && !found_peak; ++i) {
            float h = height_code_to_m(sampler.code(i, j));
            if (std::abs(h - H) < 1e-3f) {
                px = (static_cast<float>(i) + 0.5f) * cell;
                pz = (static_cast<float>(j) + 0.5f) * cell;
                found_peak = true;
            }
        }
    }
    RON_CHECK(found_peak);

    u32 prev_min_level = 0;
    for (int s = 0; s <= 11; ++s) {
        float cam_y = H + 20.0f + static_cast<float>(s) * (3000.0f / 11.0f);
        Vec3 cam(px, cam_y, pz);
        SelectResult res = cdlod_select(qt, nullptr, cam, nodes, TERRAIN_MAX_SELECTED);
        u32 min_level = 7;
        for (u32 n = 0; n < res.count; ++n) {
            min_level = std::min(min_level, static_cast<u32>(nodes[n].level));
        }
        RON_CHECK(min_level >= prev_min_level);
        if (s == 0) {
            RON_CHECK(min_level == 0);
        }
        if (s == 11) {
            RON_CHECK(min_level >= 5);
        }
        prev_min_level = min_level;
    }

    // (c) Determinism
    TerrainNodeGpu nodesA[TERRAIN_MAX_SELECTED];
    TerrainNodeGpu nodesB[TERRAIN_MAX_SELECTED];
    SelectResult resA = cdlod_select(qt, nullptr, cameras[0], nodesA, TERRAIN_MAX_SELECTED);
    SelectResult resB = cdlod_select(qt, nullptr, cameras[0], nodesB, TERRAIN_MAX_SELECTED);
    RON_CHECK(resA.count == resB.count);
    RON_CHECK(resA.overflow == resB.overflow);
    RON_CHECK(std::memcmp(nodesA, nodesB, sizeof(TerrainNodeGpu) * resA.count) == 0);

    // (d) 20 pseudo-random cameras with culling
    u32 lcg_state = 98765;
    auto lcg = [&]() -> float {
        lcg_state = lcg_state * 1664525u + 1013904223u;
        return static_cast<float>(lcg_state & 0xFFFF) / 65535.0f;
    };

    for (int r = 0; r < 20; ++r) {
        float fx = 500.0f + lcg() * 3000.0f;
        float fz = 500.0f + lcg() * 3000.0f;
        float dist = 50.0f + lcg() * 1400.0f;
        float yaw = lcg() * 6.28f;

        CameraRts cam;
        camera_init(cam, fx, fz, yaw, dist);
        camera_snap(cam, &sampler);

        Vec3 eye = camera_eye(cam, &sampler);
        Mat4 view = camera_view(cam, &sampler);
        Mat4 proj = camera_proj(16.0f / 9.0f);
        Frustum frust = frustum_from_view_proj(mat4_mul(proj, view));

        SelectResult res_rand = cdlod_select(qt, &frust, eye, nodes, TERRAIN_MAX_SELECTED);
        RON_CHECK(!res_rand.overflow);
        RON_CHECK(res_rand.count <= TERRAIN_MAX_SELECTED);
    }

    // (e) Capacity guard
    TerrainNodeGpu canary{ 999.0f, 999.0f, 999.0f, 999.0f };
    nodes[10] = canary;
    SelectResult res_cap = cdlod_select(qt, nullptr, Vec3(2048.0f, 100.0f, 2048.0f), nodes, 10);
    RON_CHECK(res_cap.count == 10);
    RON_CHECK(res_cap.overflow);
    RON_CHECK(std::memcmp(&nodes[10], &canary, sizeof(TerrainNodeGpu)) == 0);

    // (f) Perf: median of 200 timed cdlod_select calls (after 20 warm-ups) < 2.0 ms
    Vec3 perf_cam(2048.0f, 300.0f, 2048.0f);
    for (int w = 0; w < 20; ++w) {
        cdlod_select(qt, nullptr, perf_cam, nodes, TERRAIN_MAX_SELECTED);
    }

    float times_ms[200];
    for (int i = 0; i < 200; ++i) {
        u64 t0 = ron::clock_now_ns();
        cdlod_select(qt, nullptr, perf_cam, nodes, TERRAIN_MAX_SELECTED);
        u64 t1 = ron::clock_now_ns();
        times_ms[i] = static_cast<float>(ron::clock_ns_to_ms(t1 - t0));
    }
    std::sort(times_ms, times_ms + 200);
    float median_ms = 0.5f * (times_ms[99] + times_ms[100]);
    RON_LOG_INFO("CDLOD perf median: %.3f ms", median_ms);
    RON_CHECK(median_ms < 2.0f);
}

} // namespace ron::render
