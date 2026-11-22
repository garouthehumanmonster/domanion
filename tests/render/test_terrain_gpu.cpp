#include "test_framework.hpp"
#include "render/gl_fixture.hpp"
#include "render/scene_fixture.hpp"
#include "render/terrain_materials.hpp"
#include "render/gl_bindings.hpp"
#include "core/assert_log.hpp"
#include <vector>
#include <cmath>
#include <numbers>
#include <cstdio>
#include <cstring>

namespace ron::render {

RON_TEST(Terrain_Upload) {
    GL_TEST_BEGIN();
    auto& scene = test::test_scene();
    const auto& src = test::test_source();
    const auto& tex26 = test::test_tex26();
    u32 N = src.height_n;

    // 1. Height R16 at 8 positions: 4 corners, center, 3 LCG
    u32 test_coords[8][2] = {
        { 0, 0 },
        { N - 1, 0 },
        { 0, N - 1 },
        { N - 1, N - 1 },
        { N / 2, N / 2 },
        { 0, 0 }, { 0, 0 }, { 0, 0 }
    };
    u32 lcg = 1234567;
    for (int k = 0; k < 3; ++k) {
        lcg = lcg * 1664525u + 1013904223u;
        test_coords[5 + k][0] = lcg % N;
        lcg = lcg * 1664525u + 1013904223u;
        test_coords[5 + k][1] = lcg % N;
    }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    for (int k = 0; k < 8; ++k) {
        u32 x = test_coords[k][0];
        u32 y = test_coords[k][1];
        u16 gpu_val = 0;
        glGetTextureSubImage(scene.terrain.height_tex().id(), 0,
                            static_cast<GLint>(x), static_cast<GLint>(y), 0,
                            1, 1, 1, GL_RED, GL_UNSIGNED_SHORT,
                            sizeof(u16), &gpu_val);
        u16 exp_val = src.height[y * N + x];
        RON_CHECK(gpu_val == exp_val);
    }

    // 2. Biome R8 at 8 positions
    u32 bw = src.biome_w;
    u32 bh = src.biome_h;
    u32 biome_coords[8][2] = {
        { 0, 0 },
        { bw - 1, 0 },
        { 0, bh - 1 },
        { bw - 1, bh - 1 },
        { bw / 2, bh / 2 },
        { 0, 0 }, { 0, 0 }, { 0, 0 }
    };
    lcg = 7654321;
    for (int k = 0; k < 3; ++k) {
        lcg = lcg * 1664525u + 1013904223u;
        biome_coords[5 + k][0] = lcg % bw;
        lcg = lcg * 1664525u + 1013904223u;
        biome_coords[5 + k][1] = lcg % bh;
    }

    for (int k = 0; k < 8; ++k) {
        u32 x = biome_coords[k][0];
        u32 y = biome_coords[k][1];
        u8 gpu_val = 0;
        glGetTextureSubImage(scene.terrain.biome_tex().id(), 0,
                            static_cast<GLint>(x), static_cast<GLint>(y), 0,
                            1, 1, 1, GL_RED, GL_UNSIGNED_BYTE,
                            sizeof(u8), &gpu_val);
        u8 exp_val = src.biome[y * bw + x];
        RON_CHECK(gpu_val == exp_val);
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    // 3. Terrain albedo: layer 5 level 0 (first 64 bytes) == file bytes
    {
        TerrainTexLevel lvl;
        RON_CHECK(terrain_tex_level(tex26, 4, 0, 5, lvl));
        std::vector<u8> readback(lvl.bytes);
        glGetCompressedTextureSubImage(scene.terrain.albedo_arr().id(), 0,
                                      0, 0, 5, 512, 512, 1,
                                      static_cast<GLsizei>(readback.size()), readback.data());
        RON_CHECK(std::memcmp(readback.data(), lvl.data, 64) == 0);

        // level 9 layer 7 (8 bytes) equal
        TerrainTexLevel lvl9;
        RON_CHECK(terrain_tex_level(tex26, 4, 9, 7, lvl9));
        std::vector<u8> readback9(lvl9.bytes);
        glGetCompressedTextureSubImage(scene.terrain.albedo_arr().id(), 9,
                                      0, 0, 7, 1, 1, 1,
                                      static_cast<GLsizei>(readback9.size()), readback9.data());
        RON_CHECK(readback9.size() == 8);
        RON_CHECK(std::memcmp(readback9.data(), lvl9.data, 8) == 0);
    }

    // 4. Terrain normal: BC5, layer 2 level 0 (first 64 bytes); level 9 layer 7 (16 bytes)
    {
        TerrainTexLevel lvl;
        RON_CHECK(terrain_tex_level(tex26, 5, 0, 2, lvl));
        std::vector<u8> readback(lvl.bytes);
        glGetCompressedTextureSubImage(scene.terrain.normal_arr().id(), 0,
                                      0, 0, 2, 512, 512, 1,
                                      static_cast<GLsizei>(readback.size()), readback.data());
        RON_CHECK(std::memcmp(readback.data(), lvl.data, 64) == 0);

        TerrainTexLevel lvl9;
        RON_CHECK(terrain_tex_level(tex26, 5, 9, 7, lvl9));
        std::vector<u8> readback9(lvl9.bytes);
        glGetCompressedTextureSubImage(scene.terrain.normal_arr().id(), 9,
                                      0, 0, 7, 1, 1, 1,
                                      static_cast<GLsizei>(readback9.size()), readback9.data());
        RON_CHECK(readback9.size() == 16);
        RON_CHECK(std::memcmp(readback9.data(), lvl9.data, 16) == 0);
    }

    // 5. Terrain RH: layer 0 level 4 first 64 bytes
    {
        TerrainTexLevel lvl;
        RON_CHECK(terrain_tex_level(tex26, 6, 4, 0, lvl));
        std::vector<u8> readback(lvl.bytes);
        glGetCompressedTextureSubImage(scene.terrain.rh_arr().id(), 4,
                                      0, 0, 0, 32, 32, 1,
                                      static_cast<GLsizei>(readback.size()), readback.data());
        RON_CHECK(std::memcmp(readback.data(), lvl.data, 64) == 0);
    }

    // 6. Ledger checks
    u64 exp_tex_bytes = gl_tex_bytes(TexFmt::BC1_SRGB, 512, 512, 8, 10)
                      + gl_tex_bytes(TexFmt::BC5, 512, 512, 8, 10)
                      + gl_tex_bytes(TexFmt::BC1, 512, 512, 8, 10);
    RON_CHECK(scene.ledger.used(VramCat::TerrainTextures) == exp_tex_bytes);
    RON_CHECK(scene.ledger.used(VramCat::TerrainTextures) <= 64ULL * 1024 * 1024);
    RON_CHECK(scene.ledger.used(VramCat::TerrainMaps) <= 192ULL * 1024 * 1024);
    RON_CHECK(scene.ledger.total_used() < 2304ULL * 1024 * 1024);
    GL_TEST_END();
}

RON_TEST(Terrain_DepthMatch) {
    GL_TEST_BEGIN();
    auto& scene = test::test_scene();
    Vec3 flat = test::find_flattest_point(test::test_sampler());

    CameraRts cam;
    camera_init(cam, flat.x, flat.z, 0.7f, 120.0f);
    camera_snap(cam, &test::test_sampler());

    VramLedger rt_ledger;
    RenderTarget rt;
    RON_CHECK(rt.create(rt_ledger, VramCat::Staging, VramCat::Staging, "rt_depth_match", 129, 73, TexFmt::RGBA16F, true));

    float clear_col[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    scene.render_view(rt, cam, ProgramId::Terrain, false, clear_col);

    float aspect = 129.0f / 73.0f;
    Mat4 view = camera_view(cam, &test::test_sampler());
    Mat4 proj = camera_proj(aspect);
    Mat4 vp = mat4_mul(proj, view);
    Mat4 inv_vp;
    RON_CHECK(mat4_inverse(vp, inv_vp));

    Vec3 ro, rd;
    unproject_ray(inv_vp, 0.0f, 0.0f, ro, rd);

    Vec3 hit;
    bool has_hit = pick_terrain(test::test_sampler(), ro, rd, 16000.0f, hit);
    RON_CHECK(has_hit);

    Vec3 view_hit = transform_point(view, hit);
    float d_exp = -view_hit.z;

    float gpu_depth = 0.0f;
    glGetTextureSubImage(rt.depth_tex(), 0, 64, 36, 0, 1, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, sizeof(float), &gpu_depth);

    float n = CAM_NEAR;
    float f_far = CAM_FAR;
    float A = -n / (f_far - n);
    float B = (n * f_far) / (f_far - n);
    float z_gpu = B / (gpu_depth - A);

    RON_CHECK(std::abs(z_gpu - d_exp) <= 1.0f + 0.005f * d_exp);

    float center_pixel[4];
    glGetTextureSubImage(rt.color_tex(), 0, 64, 36, 0, 1, 1, 1, GL_RGBA, GL_FLOAT, sizeof(center_pixel), center_pixel);
    RON_CHECK(std::isfinite(center_pixel[0]) && std::isfinite(center_pixel[1]) &&
              std::isfinite(center_pixel[2]) && std::isfinite(center_pixel[3]));
    RON_CHECK(std::abs(center_pixel[3] - 1.0f) < 1e-3f);
    float luma = 0.2126f * center_pixel[0] + 0.7152f * center_pixel[1] + 0.0722f * center_pixel[2];
    RON_CHECK(luma > 0.0005f);

    RON_CHECK(!scene.terrain.overflowed());
    u32 nc = scene.terrain.node_count();
    RON_CHECK(nc >= 1 && nc <= 2048);
    std::printf("DepthMatch node_count: %u, d_exp: %.3f, z_gpu: %.3f\n", nc, d_exp, z_gpu);
    GL_TEST_END();
}

RON_TEST(Terrain_NoCracks) {
    GL_TEST_BEGIN();
    auto& scene = test::test_scene();
    VramLedger rt_ledger;
    RenderTarget rt;
    RON_CHECK(rt.create(rt_ledger, VramCat::Staging, VramCat::Staging, "rt_no_cracks", 256, 144, TexFmt::RGBA16F, true));

    struct Pose {
        float dist;
        float yaw;
    };
    Pose poses[4] = {
        { 150.0f, 0.0f },
        { 500.0f, 0.58f },
        { 900.0f, std::numbers::pi_v<float> * 0.5f },
        { 1200.0f, -2.09f }
    };

    float clear_col[4] = { 1.0f, 0.0f, 1.0f, 1.0f }; // Magenta
    std::vector<float> pixels(256 * 144 * 4);

    for (int p = 0; p < 4; ++p) {
        CameraRts cam;
        camera_init(cam, 2048.0f, 2048.0f, poses[p].yaw, poses[p].dist);
        camera_snap(cam, &test::test_sampler());

        scene.render_view(rt, cam, ProgramId::TerrainDebugFlat, false, clear_col);

        u32 nc = scene.terrain.node_count();
        std::printf("NoCracks pose %d (dist=%.0f, yaw=%.2f) node_count: %u\n",
                    p, poses[p].dist, poses[p].yaw, nc);

        glGetTextureSubImage(rt.color_tex(), 0, 0, 0, 0, 256, 144, 1,
                            GL_RGBA, GL_FLOAT, static_cast<GLsizei>(pixels.size() * sizeof(float)), pixels.data());

        u32 fail_count = 0;
        for (u32 y = 0; y < 144; ++y) {
            for (u32 x = 0; x < 256; ++x) {
                size_t idx = (y * 256 + x) * 4;
                float r = pixels[idx + 0];
                float g = pixels[idx + 1];
                float b = pixels[idx + 2];
                if (r < 0.99f || g < 0.99f || b < 0.99f) {
                    if (fail_count < 5) {
                        std::printf("  Crack pixel at (%u, %u): (%.2f, %.2f, %.2f)\n", x, y, r, g, b);
                    }
                    ++fail_count;
                }
            }
        }
        if (fail_count > 0) {
            std::printf("FAIL NoCracks pose %d: %u non-white pixels\n", p, fail_count);
        }
        RON_CHECK(fail_count == 0);
    }
    GL_TEST_END();
}

} // namespace ron::render
