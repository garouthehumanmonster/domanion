#include "test_framework.hpp"
#include "render/gl_fixture.hpp"
#include "render/scene_fixture.hpp"
#include "core/assert_log.hpp"
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace ron::render {

RON_TEST(Ocean_Colour) {
    GL_TEST_BEGIN();
    auto& scene = test::test_scene();
    u8 max_depth_val = 0;
    Vec3 deep_cell = test::find_max_ocean_depth_cell(scene.terrain, max_depth_val);
    if (max_depth_val < 64) {
        RON_LOG_ERROR("Ocean max depth val %u < 64", max_depth_val);
    }
    RON_CHECK(max_depth_val >= 64);

    CameraRts cam;
    camera_init(cam, deep_cell.x, deep_cell.z, 0.0f, 150.0f);
    camera_snap(cam, &test::test_sampler());

    VramLedger rt_ledger;
    RenderTarget rt;
    RON_CHECK(rt.create(rt_ledger, VramCat::Staging, VramCat::Staging, "rt_ocean_col", 129, 73, TexFmt::RGBA16F, true));

    float clear_col[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    scene.render_view(rt, cam, ProgramId::Terrain, true, clear_col, 1.0f);

    float px1[4];
    glGetTextureSubImage(rt.color_tex(), 0, 64, 36, 0, 1, 1, 1, GL_RGBA, GL_FLOAT, sizeof(px1), px1);
    RON_CHECK(std::isfinite(px1[0]) && std::isfinite(px1[1]) &&
              std::isfinite(px1[2]) && std::isfinite(px1[3]));

    // Blue dominant: b > 1.5 * r and b > 0.004
    RON_CHECK(px1[2] > 1.5f * px1[0]);
    RON_CHECK(px1[2] > 0.004f);

    // Render with different time (4.0s)
    scene.render_view(rt, cam, ProgramId::Terrain, true, clear_col, 4.0f);

    float px2[4];
    glGetTextureSubImage(rt.color_tex(), 0, 64, 36, 0, 1, 1, 1, GL_RGBA, GL_FLOAT, sizeof(px2), px2);
    RON_CHECK(std::isfinite(px2[0]) && std::isfinite(px2[1]) &&
              std::isfinite(px2[2]) && std::isfinite(px2[3]));

    bool animated = (std::abs(px1[0] - px2[0]) > 1e-6f ||
                     std::abs(px1[1] - px2[1]) > 1e-6f ||
                     std::abs(px1[2] - px2[2]) > 1e-6f);
    RON_CHECK(animated);
    GL_TEST_END();
}

RON_TEST(Ocean_DepthRule) {
    GL_TEST_BEGIN();
    auto& scene = test::test_scene();
    Vec3 high = test::find_highest_point(test::test_sampler(), test::test_source());
    if (high.y < 25.0f) {
        RON_LOG_ERROR("Highest point height %.2f < 25.0m", high.y);
    }
    RON_CHECK(high.y >= 25.0f);

    CameraRts cam_high;
    camera_init(cam_high, high.x, high.z, 0.4f, 100.0f);
    camera_snap(cam_high, &test::test_sampler());

    VramLedger rt_ledger;
    RenderTarget rt;
    RON_CHECK(rt.create(rt_ledger, VramCat::Staging, VramCat::Staging, "rt_ocean_col", 129, 73, TexFmt::RGBA16F, true));

    float clear_col[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    // (a) Terrain only
    scene.render_view(rt, cam_high, ProgramId::Terrain, false, clear_col);

    std::vector<float> block_a(21 * 21 * 4);
    glGetTextureSubImage(rt.color_tex(), 0, 54, 26, 0, 21, 21, 1, GL_RGBA, GL_FLOAT,
                        static_cast<GLsizei>(block_a.size() * sizeof(float)), block_a.data());

    float depth_a = 0.0f;
    glGetTextureSubImage(rt.depth_tex(), 0, 64, 36, 0, 1, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, sizeof(float), &depth_a);

    // (b) Terrain + ocean
    scene.render_view(rt, cam_high, ProgramId::Terrain, true, clear_col);

    std::vector<float> block_b(21 * 21 * 4);
    glGetTextureSubImage(rt.color_tex(), 0, 54, 26, 0, 21, 21, 1, GL_RGBA, GL_FLOAT,
                        static_cast<GLsizei>(block_b.size() * sizeof(float)), block_b.data());

    float depth_b = 0.0f;
    glGetTextureSubImage(rt.depth_tex(), 0, 64, 36, 0, 1, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, sizeof(float), &depth_b);

    // 21x21 block bitwise identical
    RON_CHECK(std::memcmp(block_a.data(), block_b.data(), block_a.size() * sizeof(float)) == 0);
    RON_CHECK(depth_a == depth_b);

    // Deep-sea cell depth check
    u8 max_depth_val = 0;
    Vec3 deep_cell = test::find_max_ocean_depth_cell(scene.terrain, max_depth_val);
    CameraRts cam_deep;
    camera_init(cam_deep, deep_cell.x, deep_cell.z, 0.0f, 150.0f);
    camera_snap(cam_deep, &test::test_sampler());

    scene.render_view(rt, cam_deep, ProgramId::Terrain, false, clear_col);
    float depth_deep_a = 0.0f;
    glGetTextureSubImage(rt.depth_tex(), 0, 64, 36, 0, 1, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, sizeof(float), &depth_deep_a);

    scene.render_view(rt, cam_deep, ProgramId::Terrain, true, clear_col);
    float depth_deep_b = 0.0f;
    glGetTextureSubImage(rt.depth_tex(), 0, 64, 36, 0, 1, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, sizeof(float), &depth_deep_b);

    RON_CHECK(depth_deep_a == depth_deep_b);
    GL_TEST_END();
}

} // namespace ron::render
