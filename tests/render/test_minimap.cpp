#include "test_framework.hpp"
#include "render/gl_fixture.hpp"
#include "render/scene_fixture.hpp"
#include "render/overlay_types.hpp"
#include "render/territory_map.hpp"
#include "render/crater_math.hpp"
#include "render/world_overlays.hpp"
#include "render/minimap_renderer.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace ron::render {

RON_TEST(Minimap_Render) {
    GL_TEST_BEGIN();
    auto& scene = test::test_scene();
    const auto& sampler = test::test_sampler();

    VramLedger rt_ledger;
    WorldOverlays overlays;
    RON_CHECK(overlays.init(rt_ledger, {}, &scene.shaders));

    MinimapRenderer minimap;
    RON_CHECK(minimap.init(rt_ledger, overlays, &scene.shaders));
    minimap.bake_base(sampler);

    // Owner map: disc of owner 1 radius 600 m at (2048, 2048) via territory_rasterize WITHOUT land mask
    TerritorySource src{ 2048.0f, 2048.0f, 600.0f, 1 };
    std::vector<u8> owner_map(512 * 512, 0);
    territory_rasterize(&src, 1, nullptr, owner_map.data());
    overlays.set_territory_owner(owner_map.data());

    // Flags: territory ON, fog ON
    overlays.set_flags(true, true, false);

    // FogMap: reveal_all, then clear 400 m square (cells with x,z in [200,600]) to unexplored
    overlays.fog().reveal_all();
    // 200m / 8m = 25, 600m / 8m = 75
    overlays.fog().test_clear_rect(25, 75, 25, 75);
    overlays.fog().commit();
    overlays.fog_gpu().upload(overlays.fog());
    overlays.fog_gpu().snap();
    overlays.update(0.016f, 0.0f);

    // Quad: square (3000,3000),(3400,3000),(3400,3400),(3000,3400)
    // BL, BR, TR, TL
    MinimapRenderer::Scene sc{};
    sc.quad_xz[0] = 3000.0f; sc.quad_xz[1] = 3000.0f; // BL
    sc.quad_xz[2] = 3400.0f; sc.quad_xz[3] = 3000.0f; // BR
    sc.quad_xz[4] = 3400.0f; sc.quad_xz[5] = 3400.0f; // TR
    sc.quad_xz[6] = 3000.0f; sc.quad_xz[7] = 3400.0f; // TL
    sc.fog_on = true;

    // Two blips / one red blip: (3200, 500, RED=0xFF0000FF, radius_px 5)
    Blip blip{ 3200.0f, 500.0f, 0xFF0000FFu, 5.0f };
    sc.blips = &blip;
    sc.blip_count = 1;

    // Render pass
    minimap.render(sc, 0.0f);

    // Read back 512x512 RGBA8 from minimap.target_tex()
    std::vector<u8> pixels(512 * 512 * 4, 0);
    glGetTextureImage(minimap.target_tex(), 0, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(pixels.size()), pixels.data());

    auto get_pixel = [&](int px, int py, u8& r, u8& g, u8& b, u8& a) {
        int idx = (py * 512 + px) * 4;
        r = pixels[idx + 0];
        g = pixels[idx + 1];
        b = pixels[idx + 2];
        a = pixels[idx + 3];
    };

    // (a) pixel at world (400,400): rgb all <= 12
    {
        int px = 400 / 8;
        int py = 400 / 8;
        u8 r = 0, g = 0, b = 0, a = 0;
        get_pixel(px, py, r, g, b, a);
        RON_CHECK(r <= 12);
        RON_CHECK(g <= 12);
        RON_CHECK(b <= 12);
    }

    // (b) pixel at middle of quad's edge, world (3200, 3000): luminance >= 200
    {
        int px = 3200 / 8;
        int py = 3000 / 8;
        u8 r = 0, g = 0, b = 0, a = 0;
        get_pixel(px, py, r, g, b, a);
        float luma = 0.299f * r + 0.587f * g + 0.114f * b;
        RON_CHECK(luma >= 200.0f);
    }

    // (c) blip centre pixel (3200, 500)/8: R >= 240, G <= 30, B <= 30
    {
        int px = 3200 / 8;
        int py = 500 / 8;
        u8 r = 0, g = 0, b = 0, a = 0;
        get_pixel(px, py, r, g, b, a);
        RON_CHECK(r >= 240);
        RON_CHECK(g <= 30);
        RON_CHECK(b <= 30);
    }

    // (d) find a sea pixel (from baked CPU image, h < -20, outside disc, outside fog square, away from quad/blip >= 50 px): b > r + 15
    {
        bool found_sea = false;
        for (int py = 10; py < 500 && !found_sea; ++py) {
            float wz = (static_cast<float>(py) + 0.5f) * 8.0f;
            for (int px = 10; px < 500; ++px) {
                float wx = (static_cast<float>(px) + 0.5f) * 8.0f;
                if (sampler.height_m(wx, wz) >= -20.0f) continue;

                // Outside disc (2048, 2048) r=600
                float d_disc = std::hypot(wx - 2048.0f, wz - 2048.0f);
                if (d_disc < 650.0f) continue;

                // Outside fog square [200, 600]
                if (wx >= 150.0f && wx <= 650.0f && wz >= 150.0f && wz <= 650.0f) continue;

                // Away from quad [3000, 3400] by >= 50 px (400 m)
                if (wx >= 2600.0f && wx <= 3800.0f && wz >= 2600.0f && wz <= 3800.0f) continue;

                // Away from blip (3200, 500) by >= 50 px (400 m)
                float d_blip = std::hypot(wx - 3200.0f, wz - 500.0f);
                if (d_blip < 400.0f) continue;

                u8 r = 0, g = 0, b = 0, a = 0;
                get_pixel(px, py, r, g, b, a);
                RON_CHECK(static_cast<int>(b) > static_cast<int>(r) + 15);
                found_sea = true;
                break;
            }
        }
        RON_CHECK(found_sea);
    }

    // (e) re-render with territory flag OFF: pixel at (2048, 2048)/8 differs by >= 6 levels in R-B vs territory-on render
    {
        int px = 2048 / 8;
        int py = 2048 / 8;
        u8 r_on = 0, g_on = 0, b_on = 0, a_on = 0;
        get_pixel(px, py, r_on, g_on, b_on, a_on);
        int diff_on = static_cast<int>(r_on) - static_cast<int>(b_on);

        // Turn territory flag OFF
        overlays.set_flags(false, true, false);
        overlays.update(0.016f, 0.0f);
        minimap.render(sc, 0.0f);

        std::vector<u8> pixels_off(512 * 512 * 4, 0);
        glGetTextureImage(minimap.target_tex(), 0, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(pixels_off.size()), pixels_off.data());

        int idx = (py * 512 + px) * 4;
        u8 r_off = pixels_off[idx + 0];
        u8 b_off = pixels_off[idx + 2];
        int diff_off = static_cast<int>(r_off) - static_cast<int>(b_off);

        RON_CHECK(std::abs(diff_on - diff_off) >= 6);
    }
}

RON_TEST(Vram_Gate11) {
    GL_TEST_BEGIN();
    VramLedger ledger;

    // Run identical init sequence as ron_view
    TerrainRenderer terrain;
    RON_CHECK(terrain.init(ledger, test::test_source(), test::test_tex26(), test::test_sampler(), test::test_quadtree()));

    RenderTarget hdr_target;
    RON_CHECK(hdr_target.create(ledger, VramCat::HdrTargets, VramCat::DepthTargets, "scene_hdr", 1600, 900, TexFmt::RGBA16F, true));

    GLint ssbo_align = 0;
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &ssbo_align);
    GLint ubo_align = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_align);

    PersistentRing frame_ring;
    RON_CHECK(frame_ring.create(ledger, VramCat::Staging, "frame_uniforms_ring", 1024, ssbo_align, ubo_align));

    u64 tm_before = ledger.used(VramCat::TerrainMaps);
    u64 pfx_before = ledger.used(VramCat::PostFx);
    u64 total_before = ledger.total_used();

    WorldOverlays overlays;
    RON_CHECK(overlays.init(ledger, {}, &test::test_scene().shaders));

    MinimapRenderer minimap;
    RON_CHECK(minimap.init(ledger, overlays, &test::test_scene().shaders));

    u64 new_objects_bytes = overlays.vram_bytes() + minimap.vram_bytes();
    u64 total_after = ledger.total_used();
    u64 delta = total_after - total_before;

    RON_CHECK_EQ(new_objects_bytes, delta);

    u64 tm_delta = ledger.used(VramCat::TerrainMaps) - tm_before;
    u64 pfx_delta = ledger.used(VramCat::PostFx) - pfx_before;

    RON_CHECK(tm_delta <= 40ULL * 1024ULL * 1024ULL);
    RON_CHECK(pfx_delta <= 4ULL * 1024ULL * 1024ULL);

    ledger.freeze();
    RON_CHECK(ledger.frozen());
    RON_CHECK(ledger.total_used() <= 2304ULL * 1024ULL * 1024ULL);

    ledger.dump();
}

} // namespace ron::render
