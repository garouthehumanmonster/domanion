#include "test_framework.hpp"
#include "render/scene_fixture.hpp"
#include "render/terrain_materials.hpp"
#include "render/terrain_quadtree.hpp"
#include "core/assert_log.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace ron::render {

RON_TEST(Terrain_RealData) {
    const auto& src = test::test_source();
    RON_CHECK(src.height != nullptr);
    RON_CHECK(src.height_n == 512 || src.height_n == 1024 || src.height_n == 2048);

    const auto& sampler = test::test_sampler();
    RON_CHECK(sampler.min_m() >= -128.0f && sampler.max_m() <= 512.0f);

    u64 land_count = 0;
    u64 total_texels = static_cast<u64>(src.height_n) * src.height_n;
    for (u64 i = 0; i < total_texels; ++i) {
        if (src.height[i] > 13107) {
            land_count++;
        }
    }
    float land_fraction = static_cast<float>(land_count) / static_cast<float>(total_texels);
    RON_CHECK(land_fraction >= 0.35f && land_fraction <= 0.55f);

    RON_LOG_INFO("Terrain_RealData: N=%u, min=%.2f, max=%.2f, land_fraction=%.4f, biome_dims=%ux%u",
                 src.height_n, sampler.min_m(), sampler.max_m(), land_fraction, src.biome_w, src.biome_h);

    u64 total_biomes = static_cast<u64>(src.biome_w) * src.biome_h;
    for (u64 i = 0; i < total_biomes; ++i) {
        u8 bid = src.biome[i];
        RON_CHECK(bid < 32);
        RON_CHECK(BIOME_TO_LAYER[bid] < 8);
    }

    RON_CHECK(ROCK_LAYER < 8);

    const auto& tex = test::test_tex26();
    RON_CHECK(tex.texture_count() >= 7);

    const auto& r4 = tex.texture(4);
    RON_CHECK(r4.layers == 8 && r4.format == 0 && (r4.flags & 1) != 0); // BC1, sRGB

    const auto& r5 = tex.texture(5);
    RON_CHECK(r5.layers == 8 && r5.format == 3); // BC5

    const auto& r6 = tex.texture(6);
    RON_CHECK(r6.layers == 8 && r6.format == 0 && (r6.flags & 1) == 0); // BC1, linear

    TerrainTexLevel lvl{};
    RON_CHECK(terrain_tex_level(tex, 4, 0, 7, lvl) && lvl.data != nullptr && lvl.bytes > 0);
    RON_CHECK(terrain_tex_level(tex, 5, 9, 0, lvl) && lvl.data != nullptr && lvl.bytes > 0);
    RON_CHECK(terrain_tex_level(tex, 6, 5, 3, lvl) && lvl.data != nullptr && lvl.bytes > 0);
}

RON_TEST(Terrain_Quadtree) {
    const auto& qt = test::test_quadtree();
    const auto& sampler = test::test_sampler();

    // 1. 300 random picks, 50 points each
    u32 lcg_state = 54321;
    auto lcg = [&]() -> u32 {
        lcg_state = lcg_state * 1664525u + 1013904223u;
        return lcg_state;
    };

    for (int pick = 0; pick < 300; ++pick) {
        u32 level = lcg() % 8;
        u32 d = qt.dim(level);
        u32 ix = lcg() % d;
        u32 iz = lcg() % d;

        float minh = 0.0f, maxh = 0.0f;
        qt.bounds(level, ix, iz, minh, maxh);

        float size = 32.0f * static_cast<float>(1u << level);
        float x0 = static_cast<float>(ix) * size;
        float z0 = static_cast<float>(iz) * size;

        for (int pt = 0; pt < 50; ++pt) {
            float fx = static_cast<float>(lcg() & 0xFFFF) / 65535.0f;
            float fz = static_cast<float>(lcg() & 0xFFFF) / 65535.0f;
            float px = x0 + fx * size;
            float pz = z0 + fz * size;
            float h = sampler.height_m(px, pz);
            RON_CHECK(h >= minh - 1e-3f && h <= maxh + 1e-3f);
        }
    }

    // 2. Root bounds equal sampler min_m / max_m
    float r_min = 0.0f, r_max = 0.0f;
    qt.bounds(7, 0, 0, r_min, r_max);
    RON_CHECK(std::abs(r_min - sampler.min_m()) < 1e-3f);
    RON_CHECK(std::abs(r_max - sampler.max_m()) < 1e-3f);

    // 3. For every node of level L > 0, minh == min children, maxh == max children
    for (u32 L = 1; L <= 7; ++L) {
        u32 d = qt.dim(L);
        for (u32 iz = 0; iz < d; ++iz) {
            for (u32 ix = 0; ix < d; ++ix) {
                float node_min = 0.0f, node_max = 0.0f;
                qt.bounds(L, ix, iz, node_min, node_max);
                float exp_min = 1e9f, exp_max = -1e9f;
                for (u32 dz = 0; dz < 2; ++dz) {
                    for (u32 dx = 0; dx < 2; ++dx) {
                        float c_min = 0.0f, c_max = 0.0f;
                        qt.bounds(L - 1, 2 * ix + dx, 2 * iz + dz, c_min, c_max);
                        exp_min = std::min(exp_min, c_min);
                        exp_max = std::max(exp_max, c_max);
                    }
                }
                RON_CHECK(std::abs(node_min - exp_min) < 1e-4f);
                RON_CHECK(std::abs(node_max - exp_max) < 1e-4f);
            }
        }
    }

    RON_CHECK(qt.dim(0) == 128);
    RON_CHECK(qt.dim(7) == 1);

    // 4. terrain_build_indices
    u16 indices[1944];
    u32 count = terrain_build_indices(indices, 1944);
    RON_CHECK(count == 1944);
    for (u32 i = 0; i < 1944; ++i) {
        RON_CHECK(indices[i] < 361);
    }

    u32 count_inner = 0;
    u32 count_outer = 0;
    for (u32 t = 0; t < 648; ++t) {
        u16 i0 = indices[t * 3 + 0];
        u16 i1 = indices[t * 3 + 1];
        u16 i2 = indices[t * 3 + 2];

        int gi0 = static_cast<int>(i0 % 19) - 1; int gj0 = static_cast<int>(i0 / 19) - 1;
        int gi1 = static_cast<int>(i1 % 19) - 1; int gj1 = static_cast<int>(i1 / 19) - 1;
        int gi2 = static_cast<int>(i2 % 19) - 1; int gj2 = static_cast<int>(i2 / 19) - 1;

        bool in0 = (gi0 >= 0 && gi0 <= 16 && gj0 >= 0 && gj0 <= 16);
        bool in1 = (gi1 >= 0 && gi1 <= 16 && gj1 >= 0 && gj1 <= 16);
        bool in2 = (gi2 >= 0 && gi2 <= 16 && gj2 >= 0 && gj2 <= 16);

        if (in0 && in1 && in2) {
            count_inner++;
            float x0 = static_cast<float>(gi0), y0 = static_cast<float>(-gj0);
            float x1 = static_cast<float>(gi1), y1 = static_cast<float>(-gj1);
            float x2 = static_cast<float>(gi2), y2 = static_cast<float>(-gj2);
            float signed_area = 0.5f * ((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0));
            RON_CHECK(std::abs(signed_area - 0.5f) < 1e-4f);
        } else {
            count_outer++;
            float cgi0 = static_cast<float>(std::clamp(gi0, 0, 16)); float cgj0 = static_cast<float>(std::clamp(gj0, 0, 16));
            float cgi1 = static_cast<float>(std::clamp(gi1, 0, 16)); float cgj1 = static_cast<float>(std::clamp(gj1, 0, 16));
            float cgi2 = static_cast<float>(std::clamp(gi2, 0, 16)); float cgj2 = static_cast<float>(std::clamp(gj2, 0, 16));
            float cx0 = cgi0, cy0 = -cgj0;
            float cx1 = cgi1, cy1 = -cgj1;
            float cx2 = cgi2, cy2 = -cgj2;
            float skirt_area = 0.5f * ((cx1 - cx0) * (cy2 - cy0) - (cx2 - cx0) * (cy1 - cy0));
            RON_CHECK(std::abs(skirt_area - 0.0f) < 1e-4f);
        }
    }
    RON_CHECK(count_inner == 512);
    RON_CHECK(count_outer == 136);
}

RON_TEST(Terrain_OceanMap) {
    // Ocean map on synthetic N=64 sampler
    u16 synth_codes[64 * 64];
    u16 c_pos = height_m_to_code(50.0f);
    u16 c_neg = height_m_to_code(-10.0f);
    for (int j = 0; j < 64; ++j) {
        for (int i = 0; i < 64; ++i) {
            synth_codes[j * 64 + i] = (i < 32) ? c_pos : c_neg;
        }
    }
    HeightSampler synth_sampler;
    synth_sampler.init(synth_codes, 64);

    std::vector<u8> ocean_map(512 * 512);
    ocean_depth_map_build(synth_sampler, ocean_map.data());

    int c0 = -1;
    for (int col = 0; col < 512; ++col) {
        float cx = (static_cast<float>(col) + 0.5f) * 8.0f;
        float cz = (100.0f + 0.5f) * 8.0f;
        if (synth_sampler.height_m(cx, cz) <= 0.0f) {
            c0 = col;
            break;
        }
    }
    RON_CHECK(c0 != -1);
    for (int col = 0; col < c0; ++col) {
        RON_CHECK(ocean_map[100 * 512 + col] == 0);
    }

    for (int k : {0, 39, 200}) {
        float exp_d = std::min(0.12f * 8.0f * static_cast<float>(k + 1), 120.0f);
        int exp_val = static_cast<int>(std::round(exp_d / 120.0f * 255.0f));
        int actual_val = ocean_map[100 * 512 + (c0 + k)];
        RON_CHECK(std::abs(actual_val - exp_val) <= 1);
    }

    u16 all_sea_codes[64 * 64];
    std::fill(std::begin(all_sea_codes), std::end(all_sea_codes), height_m_to_code(-50.0f));
    HeightSampler sea_sampler;
    sea_sampler.init(all_sea_codes, 64);

    std::vector<u8> all_sea_map(512 * 512);
    ocean_depth_map_build(sea_sampler, all_sea_map.data());
    for (int i = 0; i < 512 * 512; ++i) {
        RON_CHECK(all_sea_map[i] == 255);
    }
}

} // namespace ron::render
