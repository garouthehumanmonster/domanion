#include "test_framework.hpp"
#include "render/gl_fixture.hpp"
#include "render/scene_fixture.hpp"
#include "render/overlay_types.hpp"
#include "render/territory_map.hpp"
#include "render/crater_math.hpp"
#include "render/world_overlays.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace ron::render {

namespace {

Vec3 unproject_point(const Mat4& inv_vp, float ndc_x, float ndc_y, float ndc_z) {
    float x = inv_vp.m[0] * ndc_x + inv_vp.m[4] * ndc_y + inv_vp.m[8]  * ndc_z + inv_vp.m[12];
    float y = inv_vp.m[1] * ndc_x + inv_vp.m[5] * ndc_y + inv_vp.m[9]  * ndc_z + inv_vp.m[13];
    float z = inv_vp.m[2] * ndc_x + inv_vp.m[6] * ndc_y + inv_vp.m[10] * ndc_z + inv_vp.m[14];
    float w = inv_vp.m[3] * ndc_x + inv_vp.m[7] * ndc_y + inv_vp.m[11] * ndc_z + inv_vp.m[15];
    float inv_w = (std::abs(w) > 1e-15f) ? (1.0f / w) : 1.0f;
    return Vec3(x * inv_w, y * inv_w, z * inv_w);
}

float block_mean_luma(const std::vector<u8>& img, int cx, int cy, int half_w) {
    double sum = 0.0;
    int count = 0;
    for (int y = cy - half_w; y < cy + half_w; ++y) {
        for (int x = cx - half_w; x < cx + half_w; ++x) {
            int idx = (y * 640 + x) * 4;
            float r = img[idx + 0];
            float g = img[idx + 1];
            float b = img[idx + 2];
            sum += 0.299 * r + 0.587 * g + 0.114 * b;
            ++count;
        }
    }
    return static_cast<float>(sum / count);
}

} // namespace

RON_TEST(Overlay_Neutral) {
    GL_TEST_BEGIN();
    auto& scene = test::test_scene();
    VramLedger rt_ledger;
    RenderTarget rt;
    RON_CHECK(rt.create(rt_ledger, VramCat::Staging, VramCat::Staging, "rt_scene", 640, 360, TexFmt::RGBA8, true));

    Site sites[8];
    int site_count = pick_land_sites(test::test_sampler(), 8, 400.0f, sites);
    RON_CHECK(site_count >= 1);

    CameraRts cam;
    camera_init(cam, sites[0].x, sites[0].z, 0.0f, 300.0f);
    camera_snap(cam, &test::test_sampler());
    const float clear_rgba[4] = {0.50f, 0.62f, 0.80f, 1.0f};

    // 1. Reference render with flags off (null overlays)
    scene.terrain.set_overlays(nullptr);
    scene.render_view(rt, cam, ProgramId::Terrain, true, clear_rgba);

    std::vector<u8> ref_img(640 * 360 * 4);
    glGetTextureSubImage(rt.color_tex(), 0, 0, 0, 0, 640, 360, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(ref_img.size()), ref_img.data());

    // 2. Test render: flags ON but neutral maps
    WorldOverlays overlays;
    RON_CHECK(overlays.init(scene.ledger, {}, &scene.shaders));
    overlays.set_flags(true, true, true);
    overlays.craters().clear();
    overlays.fog().reveal_all();
    overlays.fog().commit();
    overlays.update(100.0f, 0.0f);

    scene.terrain.set_overlays(&overlays);
    scene.render_view(rt, cam, ProgramId::Terrain, true, clear_rgba);
    scene.terrain.set_overlays(nullptr);

    std::vector<u8> test_img(640 * 360 * 4);
    glGetTextureSubImage(rt.color_tex(), 0, 0, 0, 0, 640, 360, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(test_img.size()), test_img.data());

    // 3. Compare: mean diff <= 0.5 per channel, max <= 6
    double sum_diff_r = 0.0;
    double sum_diff_g = 0.0;
    double sum_diff_b = 0.0;
    int max_diff = 0;
    int total_pixels = 640 * 360;

    for (int i = 0; i < total_pixels; ++i) {
        int idx = i * 4;
        int dr = std::abs(static_cast<int>(test_img[idx + 0]) - static_cast<int>(ref_img[idx + 0]));
        int dg = std::abs(static_cast<int>(test_img[idx + 1]) - static_cast<int>(ref_img[idx + 1]));
        int db = std::abs(static_cast<int>(test_img[idx + 2]) - static_cast<int>(ref_img[idx + 2]));

        sum_diff_r += dr;
        sum_diff_g += dg;
        sum_diff_b += db;

        max_diff = std::max({max_diff, dr, dg, db});
    }

    RON_CHECK(sum_diff_r / total_pixels <= 0.5);
    RON_CHECK(sum_diff_g / total_pixels <= 0.5);
    RON_CHECK(sum_diff_b / total_pixels <= 0.5);
    RON_CHECK(max_diff <= 6);

    GL_TEST_END();
}

RON_TEST(Territory_Visible) {
    GL_TEST_BEGIN();
    auto& scene = test::test_scene();
    VramLedger rt_ledger;
    RenderTarget rt;
    RON_CHECK(rt.create(rt_ledger, VramCat::Staging, VramCat::Staging, "rt_scene", 640, 360, TexFmt::RGBA8, true));

    Site sites[8];
    int site_count = pick_land_sites(test::test_sampler(), 8, 400.0f, sites);
    RON_CHECK(site_count >= 1);

    CameraRts cam;
    camera_init(cam, sites[0].x, sites[0].z, 0.0f, 300.0f);
    camera_snap(cam, &test::test_sampler());
    const float clear_rgba[4] = {0.50f, 0.62f, 0.80f, 1.0f};

    // Reference render with flags off
    scene.terrain.set_overlays(nullptr);
    scene.render_view(rt, cam, ProgramId::Terrain, true, clear_rgba);

    std::vector<u8> neutral_img(640 * 360 * 4);
    glGetTextureSubImage(rt.color_tex(), 0, 0, 0, 0, 640, 360, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(neutral_img.size()), neutral_img.data());

    // Rasterize territory
    TerritorySource sources[8];
    for (int i = 0; i < site_count; ++i) {
        sources[i].x = sites[i].x;
        sources[i].z = sites[i].z;
        sources[i].radius = 380.0f;
        sources[i].owner = (i == 0) ? 2 : ((i == 1) ? 1 : static_cast<u8>(i + 1));
    }
    std::vector<u8> owner_map(512 * 512, 0);
    territory_rasterize(sources, site_count, &test::test_sampler(), owner_map.data());

    WorldOverlays overlays;
    RON_CHECK(overlays.init(scene.ledger, {}, &scene.shaders));
    overlays.set_territory_owner(owner_map.data());
    overlays.set_flags(true, false, false);
    overlays.update(0.016f, 0.0f);

    scene.terrain.set_overlays(&overlays);
    scene.render_view(rt, cam, ProgramId::Terrain, true, clear_rgba);
    scene.terrain.set_overlays(nullptr);

    std::vector<u8> test_img(640 * 360 * 4);
    glGetTextureSubImage(rt.color_tex(), 0, 0, 0, 0, 640, 360, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(test_img.size()), test_img.data());

    // (a) Fill check: 32x32 block around center
    double sum_delta_br = 0.0;
    int block_pixels = 0;
    for (int y = 180 - 16; y < 180 + 16; ++y) {
        for (int x = 320 - 16; x < 320 + 16; ++x) {
            int idx = (y * 640 + x) * 4;
            int neut_b_r = static_cast<int>(neutral_img[idx + 2]) - static_cast<int>(neutral_img[idx + 0]);
            int test_b_r = static_cast<int>(test_img[idx + 2]) - static_cast<int>(test_img[idx + 0]);
            sum_delta_br += (test_b_r - neut_b_r);
            ++block_pixels;
        }
    }
    RON_CHECK(sum_delta_br / block_pixels >= 8.0);

    // (b) Border check
    float aspect = 640.0f / 360.0f;
    bool site_accepted = false;

    for (int s = 0; s < site_count; ++s) {
        u8 site_owner = (s == 0) ? 2 : ((s == 1) ? 1 : static_cast<u8>(s + 1));
        float cell_m = 8.0f;
        int cz = static_cast<int>(sites[s].z / cell_m);
        float z_row = (cz + 0.5f) * cell_m;
        int cx_start = static_cast<int>(sites[s].x / cell_m);
        if (cx_start < 0 || cx_start >= 512 || cz < 0 || cz >= 512) continue;

        u8 start_owner = owner_map[cz * 512 + cx_start];
        if (start_owner != site_owner) continue;

        int edge_cx = -1;
        for (int cx = cx_start; cx < 512; ++cx) {
            if (owner_map[cz * 512 + cx] != site_owner) {
                edge_cx = cx;
                break;
            }
        }
        if (edge_cx < 0) continue;

        float E = edge_cx * cell_m;
        float test_x = E - 1.5f;
        Vec3 P(test_x, test::test_sampler().height_m(test_x, z_row), z_row);

        CameraRts site_cam;
        camera_init(site_cam, sites[s].x, sites[s].z, 0.0f, 300.0f);
        camera_snap(site_cam, &test::test_sampler());

        Mat4 s_view = camera_view(site_cam, &test::test_sampler());
        Mat4 s_proj = camera_proj(aspect);
        Mat4 s_vp = mat4_mul(s_proj, s_view);

        float ndc[3];
        project(s_vp, P, ndc);
        int px = static_cast<int>(std::floor((ndc[0] * 0.5f + 0.5f) * 640.0f));
        int py = static_cast<int>(std::floor((ndc[1] * 0.5f + 0.5f) * 360.0f));
        if (px < 4 || px >= 636 || py < 4 || py >= 356) continue;

        // Render neutral and test for this site camera
        scene.terrain.set_overlays(nullptr);
        scene.render_view(rt, site_cam, ProgramId::Terrain, true, clear_rgba);
        std::vector<u8> s_neutral_img(640 * 360 * 4);
        glGetTextureSubImage(rt.color_tex(), 0, 0, 0, 0, 640, 360, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(s_neutral_img.size()), s_neutral_img.data());

        scene.terrain.set_overlays(&overlays);
        scene.render_view(rt, site_cam, ProgramId::Terrain, true, clear_rgba);
        scene.terrain.set_overlays(nullptr);
        std::vector<u8> s_test_img(640 * 360 * 4);
        glGetTextureSubImage(rt.color_tex(), 0, 0, 0, 0, 640, 360, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(s_test_img.size()), s_test_img.data());

        Mat4 s_inv_vp;
        RON_CHECK(mat4_inverse(s_vp, s_inv_vp));

        float gpu_depth = 0.0f;
        glGetTextureSubImage(rt.depth_tex(), 0, px, py, 0, 1, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, sizeof(float), &gpu_depth);
        Vec3 p_world = unproject_point(s_inv_vp, ndc[0], ndc[1], gpu_depth);

        if ((p_world - P).length() > 2.0f) {
            continue; // Occluded
        }

        site_accepted = true;
        int idx = (py * 640 + px) * 4;

        Vec4 pal = overlay_palette(site_owner);
        int dom_ch = 0;
        if (pal.y > pal.x && pal.y > pal.z) dom_ch = 1;
        else if (pal.z > pal.x && pal.z > pal.y) dom_ch = 2;

        auto get_metric = [&](const std::vector<u8>& img, int pixel_idx) -> int {
            int r = img[pixel_idx + 0];
            int g = img[pixel_idx + 1];
            int b = img[pixel_idx + 2];
            if (dom_ch == 2) {
                return b - std::max(r, g);
            } else if (dom_ch == 0) {
                return r - std::max(g, b);
            } else {
                return g - std::max(r, b);
            }
        };

        int delta = get_metric(s_test_img, idx) - get_metric(s_neutral_img, idx);
        RON_CHECK(delta >= 30);

        float in_x = E - 40.0f;
        Vec3 P_in(in_x, test::test_sampler().height_m(in_x, z_row), z_row);
        float ndc_in[3];
        project(s_vp, P_in, ndc_in);
        int px_in = static_cast<int>(std::floor((ndc_in[0] * 0.5f + 0.5f) * 640.0f));
        int py_in = static_cast<int>(std::floor((ndc_in[1] * 0.5f + 0.5f) * 360.0f));
        if (px_in >= 0 && px_in < 640 && py_in >= 0 && py_in < 360) {
            int idx_in = (py_in * 640 + px_in) * 4;
            int delta_in = get_metric(s_test_img, idx_in) - get_metric(s_neutral_img, idx_in);
            RON_CHECK(delta_in < 30);
        }
        break;
    }
    RON_CHECK(site_accepted);

    GL_TEST_END();
}

RON_TEST(Crater_Visible) {
    GL_TEST_BEGIN();
    auto& scene = test::test_scene();
    VramLedger rt_ledger;
    RenderTarget rt;
    RON_CHECK(rt.create(rt_ledger, VramCat::Staging, VramCat::Staging, "rt_scene", 640, 360, TexFmt::RGBA8, true));

    Site sites[8];
    int site_count = pick_land_sites(test::test_sampler(), 8, 400.0f, sites);
    RON_CHECK(site_count >= 1);

    CameraRts cam;
    camera_init(cam, sites[0].x, sites[0].z, 0.0f, 300.0f);
    camera_snap(cam, &test::test_sampler());
    const float clear_rgba[4] = {0.50f, 0.62f, 0.80f, 1.0f};

    // Neutral render
    scene.terrain.set_overlays(nullptr);
    scene.render_view(rt, cam, ProgramId::Terrain, true, clear_rgba);

    std::vector<u8> neutral_img(640 * 360 * 4);
    glGetTextureSubImage(rt.color_tex(), 0, 0, 0, 0, 640, 360, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(neutral_img.size()), neutral_img.data());

    // Test render with crater at site 0 center
    CraterSplat s{ sites[0].x, sites[0].z, 30.0f, 6.0f, 1.0f, 1.5f, 4242, 0 };
    WorldOverlays overlays;
    RON_CHECK(overlays.init(scene.ledger, {}, &scene.shaders));
    overlays.craters().enqueue(s);
    overlays.set_flags(false, false, true); // craters on
    overlays.update(0.016f, 0.0f);

    scene.terrain.set_overlays(&overlays);
    scene.render_view(rt, cam, ProgramId::Terrain, true, clear_rgba);
    scene.terrain.set_overlays(nullptr);

    std::vector<u8> test_img(640 * 360 * 4);
    glGetTextureSubImage(rt.color_tex(), 0, 0, 0, 0, 640, 360, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(test_img.size()), test_img.data());

    // (a) 16x16 block at screen center
    float neut_luma = block_mean_luma(neutral_img, 320, 180, 8);
    float test_luma = block_mean_luma(test_img, 320, 180, 8);
    RON_CHECK(test_luma <= 0.75f * neut_luma);

    // (b) Screen corners (0,0) and (639, 359)
    int c0 = 0;
    int c1 = (359 * 640 + 639) * 4;
    for (int ch = 0; ch < 3; ++ch) {
        RON_CHECK(std::abs(static_cast<int>(test_img[c0 + ch]) - static_cast<int>(neutral_img[c0 + ch])) <= 1);
        RON_CHECK(std::abs(static_cast<int>(test_img[c1 + ch]) - static_cast<int>(neutral_img[c1 + ch])) <= 1);
    }

    // (c) Depth check
    float aspect = 640.0f / 360.0f;
    Mat4 view = camera_view(cam, &test::test_sampler());
    Mat4 proj = camera_proj(aspect);
    Mat4 view_proj = mat4_mul(proj, view);
    Mat4 inv_vp;
    RON_CHECK(mat4_inverse(view_proj, inv_vp));

    float center_depth = 0.0f;
    glGetTextureSubImage(rt.depth_tex(), 0, 320, 180, 0, 1, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, sizeof(float), &center_depth);

    float ndc_x = (320.5f / 640.0f) * 2.0f - 1.0f;
    float ndc_y = (180.5f / 360.0f) * 2.0f - 1.0f;
    Vec3 P_c = unproject_point(inv_vp, ndc_x, ndc_y, center_depth);

    float cpu_h = test::test_sampler().height_m(P_c.x, P_c.z);
    float off = crater_offset_m(crater_sanitize(s), P_c.x, P_c.z);
    RON_CHECK(std::abs(P_c.y - (cpu_h + off)) <= 0.8f);
    RON_CHECK(off < -1.0f);

    GL_TEST_END();
}

RON_TEST(Fog_Visible) {
    GL_TEST_BEGIN();
    auto& scene = test::test_scene();
    VramLedger rt_ledger;
    RenderTarget rt;
    RON_CHECK(rt.create(rt_ledger, VramCat::Staging, VramCat::Staging, "rt_scene", 640, 360, TexFmt::RGBA8, true));

    Site sites[8];
    int site_count = pick_land_sites(test::test_sampler(), 8, 400.0f, sites);
    RON_CHECK(site_count >= 1);

    Site focus = sites[0];
    float dist = 300.0f;
    const float clear_rgba[4] = {0.50f, 0.62f, 0.80f, 1.0f};

    for (int attempt = 0; attempt < 2; ++attempt) {
        CameraRts cam;
        camera_init(cam, focus.x, focus.z, 0.0f, dist);
        camera_snap(cam, &test::test_sampler());

        float aspect = 640.0f / 360.0f;
        Mat4 view = camera_view(cam, &test::test_sampler());
        Mat4 proj = camera_proj(aspect);
        Mat4 view_proj = mat4_mul(proj, view);

        Vec3 P_L(focus.x - 100.0f, test::test_sampler().height_m(focus.x - 100.0f, focus.z), focus.z);
        Vec3 P_V(focus.x + 50.0f,  test::test_sampler().height_m(focus.x + 50.0f,  focus.z), focus.z);
        Vec3 P_D(focus.x + 170.0f, test::test_sampler().height_m(focus.x + 170.0f, focus.z), focus.z);

        float ndc_L[3], ndc_V[3], ndc_D[3];
        project(view_proj, P_L, ndc_L);
        project(view_proj, P_V, ndc_V);
        project(view_proj, P_D, ndc_D);

        int px_L = static_cast<int>(std::floor((ndc_L[0] * 0.5f + 0.5f) * 640.0f));
        int py_L = static_cast<int>(std::floor((ndc_L[1] * 0.5f + 0.5f) * 360.0f));
        int px_V = static_cast<int>(std::floor((ndc_V[0] * 0.5f + 0.5f) * 640.0f));
        int py_V = static_cast<int>(std::floor((ndc_V[1] * 0.5f + 0.5f) * 360.0f));
        int px_D = static_cast<int>(std::floor((ndc_D[0] * 0.5f + 0.5f) * 640.0f));
        int py_D = static_cast<int>(std::floor((ndc_D[1] * 0.5f + 0.5f) * 360.0f));

        auto in_bounds = [](int px, int py) {
            return px >= 8 && px <= 632 && py >= 8 && py <= 352;
        };

        if (!in_bounds(px_L, py_L) || !in_bounds(px_V, py_V) || !in_bounds(px_D, py_D)) {
            if (attempt == 0) {
                dist = 250.0f;
                continue;
            } else {
                RON_CHECK(false); // Off-screen after retry
            }
        }

        // Neutral render
        scene.terrain.set_overlays(nullptr);
        scene.render_view(rt, cam, ProgramId::Terrain, true, clear_rgba);
        std::vector<u8> neutral_img(640 * 360 * 4);
        glGetTextureSubImage(rt.color_tex(), 0, 0, 0, 0, 640, 360, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(neutral_img.size()), neutral_img.data());

        // Fog map setup
        WorldOverlays overlays;
        RON_CHECK(overlays.init(scene.ledger, {}, &scene.shaders));
        int cx_focus = static_cast<int>(focus.x / 8.0f);
        int cx_100 = static_cast<int>((focus.x + 100.0f) / 8.0f);
        overlays.fog().test_fill(0, 0, 0, cx_focus - 1);
        overlays.fog().test_fill(255, 255, cx_focus, cx_100 - 1);
        overlays.fog().test_fill(0, 255, cx_100, 511);
        overlays.fog().commit();
        overlays.set_flags(false, true, false); // fog on
        overlays.update(100.0f, 0.0f); // snap

        scene.terrain.set_overlays(&overlays);
        scene.render_view(rt, cam, ProgramId::Terrain, true, clear_rgba);
        scene.terrain.set_overlays(nullptr);

        std::vector<u8> test_img(640 * 360 * 4);
        glGetTextureSubImage(rt.color_tex(), 0, 0, 0, 0, 640, 360, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(test_img.size()), test_img.data());

        float luma_L = block_mean_luma(test_img, px_L, py_L, 8);
        float luma_V_test = block_mean_luma(test_img, px_V, py_V, 8);
        float luma_V_neut = block_mean_luma(neutral_img, px_V, py_V, 8);
        float luma_D_test = block_mean_luma(test_img, px_D, py_D, 8);
        float luma_D_neut = block_mean_luma(neutral_img, px_D, py_D, 8);

        RON_CHECK(luma_L <= 6.0f);
        RON_CHECK(std::abs(luma_V_test - luma_V_neut) <= 2.0f);
        RON_CHECK(luma_D_neut > 0.0f);
        float ratio_D = luma_D_test / luma_D_neut;
        RON_CHECK(ratio_D >= 0.40f);
        RON_CHECK(ratio_D <= 0.85f);
        break;
    }

    GL_TEST_END();
}

} // namespace ron::render
