#include "test_framework.hpp"
#include "render/gl_fixture.hpp"
#include "render/scene_fixture.hpp"
#include "render/overlay_types.hpp"
#include "render/territory_sdf.hpp"
#include "render/territory_map.hpp"
#include "render/jfa_reference.hpp"
#include "render/gpu_timer.hpp"
#include "core/assert_log.hpp"
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace ron::render {

RON_TEST(Jfa_Seeds) {
    GL_TEST_BEGIN();
    int owner_n = 64;
    int g = 2 * owner_n;

    std::vector<u8> owner(owner_n * owner_n, 0);
    for (int y = 0; y < owner_n; ++y) {
        for (int x = 0; x < owner_n; ++x) {
            owner[y * owner_n + x] = static_cast<u8>(((x / 9) + (y / 7)) % 3);
        }
    }

    std::vector<float> ref_seeds(g * g * 2, 0.0f);
    jfa_seed_reference(owner.data(), owner_n, ref_seeds.data());

    VramLedger ledger;
    TerritorySdf sdf;
    RON_CHECK(sdf.init(ledger, owner_n, &test::test_scene().shaders));
    sdf.upload_owner(owner.data());
    sdf.debug_run_seed_only();

    std::vector<float> gpu_seeds(g * g * 2, 0.0f);
    glGetTextureSubImage(sdf.seed_tex(), 0, 0, 0, 0, g, g, 1,
                        GL_RG, GL_FLOAT, static_cast<GLsizei>(gpu_seeds.size() * sizeof(float)), gpu_seeds.data());

    for (int i = 0; i < g * g * 2; ++i) {
        RON_CHECK_EQ(gpu_seeds[i], ref_seeds[i]);
    }
    GL_TEST_END();
}

RON_TEST(Jfa_MatchesBrute) {
    GL_TEST_BEGIN();
    int owner_n = 128;
    int g = 2 * owner_n;

    (void)g;
    TerritorySource sources[6] = {
        {  700.0f,  800.0f, 500.0f, 1 },
        { 1900.0f,  900.0f, 450.0f, 2 },
        { 3000.0f,  700.0f, 600.0f, 3 },
        {  900.0f, 2600.0f, 700.0f, 2 },
        { 2300.0f, 2500.0f, 650.0f, 4 },
        { 3300.0f, 3300.0f, 500.0f, 1 }
    };

    std::vector<u8> owner_512(512 * 512, 0);
    territory_rasterize(sources, 6, nullptr, owner_512.data());

    // Scale down to owner_n=128
    std::vector<u8> owner_128(owner_n * owner_n, 0);
    for (int y = 0; y < owner_n; ++y) {
        for (int x = 0; x < owner_n; ++x) {
            owner_128[y * owner_n + x] = owner_512[(y * 4) * 512 + (x * 4)];
        }
    }

    std::vector<float> ref_seeds(g * g * 2, 0.0f);
    jfa_seed_reference(owner_128.data(), owner_n, ref_seeds.data());

    std::vector<float> brute_d2(g * g, 0.0f);
    jfa_brute_d2(ref_seeds.data(), g, brute_d2.data());

    VramLedger ledger;
    TerritorySdf sdf;
    RON_CHECK(sdf.init(ledger, owner_n, &test::test_scene().shaders));
    sdf.upload_owner(owner_128.data());
    sdf.rebuild();

    std::vector<float> gpu_seeds(g * g * 2, 0.0f);
    glGetTextureSubImage(sdf.seed_tex(), 0, 0, 0, 0, g, g, 1,
                        GL_RG, GL_FLOAT, static_cast<GLsizei>(gpu_seeds.size() * sizeof(float)), gpu_seeds.data());

    std::vector<float> gpu_sdf(g * g * 2, 0.0f);
    glGetTextureSubImage(sdf.sdf_tex(), 0, 0, 0, 0, g, g, 1,
                        GL_RG, GL_FLOAT, static_cast<GLsizei>(gpu_sdf.size() * sizeof(float)), gpu_sdf.data());

    int exact_matches = 0;
    float max_diff_half_units = 0.0f;
    float texel_m = 4096.0f / static_cast<float>(g);

    for (int py = 0; py < g; ++py) {
        float cy = static_cast<float>(2 * py + 1);
        for (int px = 0; px < g; ++px) {
            float cx = static_cast<float>(2 * px + 1);
            int idx = py * g + px;

            float sx = gpu_seeds[idx * 2 + 0];
            float sy = gpu_seeds[idx * 2 + 1];

            float d2_gpu = 1e30f;
            if (sx >= 0.0f) {
                float dx = sx - cx;
                float dy = sy - cy;
                d2_gpu = dx * dx + dy * dy;
            }

            float d2_brute = brute_d2[idx];
            if (d2_gpu == d2_brute) {
                exact_matches++;
            }

            float dist_gpu = std::sqrt(d2_gpu);
            float dist_brute = std::sqrt(d2_brute);
            float diff = std::abs(dist_gpu - dist_brute);
            if (diff > max_diff_half_units) {
                max_diff_half_units = diff;
            }
            RON_CHECK(diff <= 4.0f);

            // SDF check: |sdf.x| == sqrt(d2_gpu)*0.5*texel_m within 0.001*|v|+0.05
            float act_sd = std::abs(gpu_sdf[idx * 2 + 0]);
            float exp_sd = (d2_gpu > 1e20f) ? 2000.0f : std::min(dist_gpu * 0.5f * texel_m, 2000.0f);
            if (exp_sd < 2000.0f) {
                float tol = 0.001f * exp_sd + 0.05f;
                RON_CHECK(std::abs(act_sd - exp_sd) <= tol);
            } else {
                RON_CHECK_EQ(act_sd, 2000.0f);
            }
        }
    }

    double match_pct = (100.0 * exact_matches) / static_cast<double>(g * g);
    double mismatch_pct = 100.0 - match_pct;
    std::printf("Jfa_MatchesBrute: exact match = %.2f%%, mismatch = %.2f%% (max diff = %.3f half-units)\n",
                match_pct, mismatch_pct, max_diff_half_units);
    RON_CHECK(match_pct >= 99.0);

    GL_TEST_END();
}

RON_TEST(Jfa_Signs) {
    GL_TEST_BEGIN();
    int owner_n = 64;
    int g = 2 * owner_n;
    VramLedger ledger;
    TerritorySdf sdf;
    RON_CHECK(sdf.init(ledger, owner_n, &test::test_scene().shaders));

    // (a) One disc owner 1 on 0: sdf>0 inside, <0 outside, sdf.y equals owner id
    std::vector<u8> owner_disc(owner_n * owner_n, 0);
    int center_x = 32, center_y = 32, rad = 16;
    for (int y = 0; y < owner_n; ++y) {
        for (int x = 0; x < owner_n; ++x) {
            int dx = x - center_x;
            int dy = y - center_y;
            if (dx * dx + dy * dy <= rad * rad) {
                owner_disc[y * owner_n + x] = 1;
            }
        }
    }
    sdf.upload_owner(owner_disc.data());
    sdf.rebuild();

    std::vector<float> sdf_data(g * g * 2, 0.0f);
    glGetTextureSubImage(sdf.sdf_tex(), 0, 0, 0, 0, g, g, 1,
                        GL_RG, GL_FLOAT, static_cast<GLsizei>(sdf_data.size() * sizeof(float)), sdf_data.data());

    // Check inside and outside
    for (int s = 0; s < 20; ++s) {
        int tx = 10 + s * 2;
        int ty = 32;
        int idx = (ty * g + tx) * 2;
        float val = sdf_data[idx + 0];
        float owner_f = sdf_data[idx + 1];

        // Owner in owner_n coordinates:
        int ox = tx / 2;
        int oy = ty / 2;
        u8 expected_owner = owner_disc[oy * owner_n + ox];
        RON_CHECK_EQ(owner_f, static_cast<float>(expected_owner));
        if (expected_owner > 0) {
            RON_CHECK(val > 0.0f);
        } else {
            RON_CHECK(val < 0.0f);
        }
    }

    // (b) All owner 0: every texel sdf == -2000 and y == 0
    std::vector<u8> owner_zero(owner_n * owner_n, 0);
    sdf.upload_owner(owner_zero.data());
    sdf.rebuild();
    glGetTextureSubImage(sdf.sdf_tex(), 0, 0, 0, 0, g, g, 1,
                        GL_RG, GL_FLOAT, static_cast<GLsizei>(sdf_data.size() * sizeof(float)), sdf_data.data());
    for (int i = 0; i < g * g; ++i) {
        RON_CHECK_EQ(sdf_data[i * 2 + 0], -2000.0f);
        RON_CHECK_EQ(sdf_data[i * 2 + 1], 0.0f);
    }

    // (c) All owner 1: every texel sdf == +2000 and y == 1
    std::vector<u8> owner_one(owner_n * owner_n, 1);
    sdf.upload_owner(owner_one.data());
    sdf.rebuild();
    glGetTextureSubImage(sdf.sdf_tex(), 0, 0, 0, 0, g, g, 1,
                        GL_RG, GL_FLOAT, static_cast<GLsizei>(sdf_data.size() * sizeof(float)), sdf_data.data());
    for (int i = 0; i < g * g; ++i) {
        RON_CHECK_EQ(sdf_data[i * 2 + 0], 2000.0f);
        RON_CHECK_EQ(sdf_data[i * 2 + 1], 1.0f);
    }

    GL_TEST_END();
}

RON_TEST(Jfa_Perf) {
    GL_TEST_BEGIN();
    int owner_n = OWNER_N; // 512
    int g = JFA_G;         // 1024

    (void)g;
    TerritorySource sources[6] = {
        {  700.0f,  800.0f, 500.0f, 1 },
        { 1900.0f,  900.0f, 450.0f, 2 },
        { 3000.0f,  700.0f, 600.0f, 3 },
        {  900.0f, 2600.0f, 700.0f, 2 },
        { 2300.0f, 2500.0f, 650.0f, 4 },
        { 3300.0f, 3300.0f, 500.0f, 1 }
    };

    std::vector<u8> owner(owner_n * owner_n, 0);
    territory_rasterize(sources, 6, nullptr, owner.data());

    VramLedger ledger;
    TerritorySdf sdf;
    RON_CHECK(sdf.init(ledger, owner_n, &test::test_scene().shaders));
    sdf.upload_owner(owner.data());

    // 2 warm-up rebuilds
    sdf.rebuild();
    sdf.rebuild();

    GpuTimer timer;
    RON_CHECK(timer.create());

    float timings[7];
    for (int i = 0; i < 7; ++i) {
        timer.begin(0);
        sdf.rebuild();
        timer.end(0);
        glFinish();
        timings[i] = static_cast<float>(timer.elapsed_ms(0));
    }

    std::sort(timings, timings + 7);
    float median = timings[3];

    std::printf("Jfa_Perf 7 timings (ms): %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f (median = %.3f ms)\n",
                timings[0], timings[1], timings[2], timings[3], timings[4], timings[5], timings[6], median);

    RON_CHECK(median < 4.0f);
    GL_TEST_END();
}

} // namespace ron::render
