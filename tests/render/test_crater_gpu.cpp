#include "test_framework.hpp"
#include "render/gl_fixture.hpp"
#include "render/scene_fixture.hpp"
#include "render/overlay_types.hpp"
#include "render/crater_math.hpp"
#include "render/crater_splatter.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace ron::render {

RON_TEST(Crater_GpuMatchesCpu) {
    GL_TEST_BEGIN();
    VramLedger ledger;
    CraterSplatter splatter;
    RON_CHECK(splatter.init(ledger, 512, &test::test_scene().shaders));

    CraterSplat s1{ 1024.0f, 1024.0f, 40.0f, 3.0f, 0.8f, 1.4f, 12345, 0 };
    CraterSplat s2{ 2048.0f, 1500.0f, 60.0f, 4.0f, 0.5f, 1.2f, 67890, 0 };
    CraterSplat s3{ 3000.0f, 2500.0f, 30.0f, 2.0f, 1.0f, 1.5f, 54321, 0 };

    RON_CHECK(splatter.enqueue(s1));
    RON_CHECK(splatter.enqueue(s2));
    RON_CHECK(splatter.enqueue(s3));

    int flushed = splatter.flush(64);
    RON_CHECK_EQ(flushed, 3);

    std::vector<u8> cpu_img(512 * 512 * 4);
    crater_clear(cpu_img.data(), 512);
    crater_apply_cpu(cpu_img.data(), 512, crater_sanitize(s1));
    crater_apply_cpu(cpu_img.data(), 512, crater_sanitize(s2));
    crater_apply_cpu(cpu_img.data(), 512, crater_sanitize(s3));

    std::vector<u8> gpu_img(512 * 512 * 4);
    glGetTextureSubImage(splatter.texture(), 0, 0, 0, 0, 512, 512, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(gpu_img.size()), gpu_img.data());

    int max_diff_r = 0;
    int max_diff_g = 0;
    int within_1_count = 0;
    int total_texels = 512 * 512;

    for (int i = 0; i < total_texels; ++i) {
        int idx = i * 4;
        RON_CHECK_EQ(gpu_img[idx + 2], 0);
        RON_CHECK_EQ(gpu_img[idx + 3], 0);

        int diff_r = std::abs(static_cast<int>(gpu_img[idx + 0]) - static_cast<int>(cpu_img[idx + 0]));
        int diff_g = std::abs(static_cast<int>(gpu_img[idx + 1]) - static_cast<int>(cpu_img[idx + 1]));

        max_diff_r = std::max(max_diff_r, diff_r);
        max_diff_g = std::max(max_diff_g, diff_g);

        if (diff_r <= 1 && diff_g <= 1) {
            ++within_1_count;
        }
    }

    RON_CHECK(max_diff_r <= 2);
    RON_CHECK(max_diff_g <= 2);
    RON_CHECK(static_cast<double>(within_1_count) / total_texels >= 0.999);
    GL_TEST_END();
}

RON_TEST(Crater_Clamp) {
    GL_TEST_BEGIN();
    VramLedger ledger;
    CraterSplatter splatter;
    RON_CHECK(splatter.init(ledger, 512, &test::test_scene().shaders));

    std::vector<u8> fresh_img(512 * 512 * 4);
    glGetTextureSubImage(splatter.texture(), 0, 0, 0, 0, 512, 512, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(fresh_img.size()), fresh_img.data());
    for (int i = 0; i < 512 * 512; ++i) {
        RON_CHECK_EQ(fresh_img[i * 4 + 0], 128);
        RON_CHECK_EQ(fresh_img[i * 4 + 1], 0);
        RON_CHECK_EQ(fresh_img[i * 4 + 2], 0);
        RON_CHECK_EQ(fresh_img[i * 4 + 3], 0);
    }

    float texel_m = 4096.0f / 512.0f;
    float center_x = (62.0f + 0.5f) * texel_m;
    float center_z = (62.0f + 0.5f) * texel_m;
    CraterSplat s{ center_x, center_z, 30.0f, 3.0f, 0.8f, 1.3f, 42, 0 };

    for (int i = 0; i < 20; ++i) {
        RON_CHECK(splatter.enqueue(s));
    }
    splatter.flush(64);

    std::vector<u8> post_img(512 * 512 * 4);
    glGetTextureSubImage(splatter.texture(), 0, 0, 0, 0, 512, 512, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(post_img.size()), post_img.data());

    int center_idx = (62 * 512 + 62) * 4;
    RON_CHECK_EQ(post_img[center_idx + 0], 0);

    u8 min_r = 255;
    int r_zero_count = 0;
    for (int i = 0; i < 512 * 512; ++i) {
        u8 r = post_img[i * 4 + 0];
        if (r < min_r) min_r = r;
        if (r == 0) ++r_zero_count;
    }
    RON_CHECK_EQ(min_r, 0);

    TexelBox box = crater_box(crater_sanitize(s), 512);
    int box_area = (box.x1 - box.x0 + 1) * (box.y1 - box.y0 + 1);
    RON_CHECK(r_zero_count >= 1);
    RON_CHECK(r_zero_count <= box_area);
    GL_TEST_END();
}

RON_TEST(Crater_Queue) {
    GL_TEST_BEGIN();
    VramLedger ledger;
    CraterSplatter splatter;
    RON_CHECK(splatter.init(ledger, 512, &test::test_scene().shaders));

    CraterSplat s{ 1000.0f, 1000.0f, 20.0f, 2.0f, 0.5f, 1.2f, 1, 0 };
    int false_count = 0;
    for (int i = 0; i < 1100; ++i) {
        if (!splatter.enqueue(s)) {
            ++false_count;
        }
    }
    RON_CHECK_EQ(false_count, 76);
    RON_CHECK_EQ(splatter.dropped(), 76u);
    RON_CHECK_EQ(splatter.pending(), 1024);

    while (splatter.pending() > 0) {
        int prev_pending = splatter.pending();
        int flushed = splatter.flush(64);
        RON_CHECK_EQ(flushed, std::min(64, prev_pending));
        RON_CHECK_EQ(splatter.pending(), prev_pending - flushed);
    }
    RON_CHECK_EQ(splatter.pending(), 0);
    RON_CHECK_EQ(splatter.applied_total(), 1024u);
    GL_TEST_END();
}

} // namespace ron::render
