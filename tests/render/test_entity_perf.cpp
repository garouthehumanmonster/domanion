#include "test_framework.hpp"
#include "render/gl_fixture.hpp"
#include "render/scene_fixture.hpp"
#include "render/entity_renderer.hpp"
#include "render/entity_assets.hpp"
#include "render/entity_crowd.hpp"
#include "render/gpu_timer.hpp"
#include "render/render_target.hpp"
#include "render/persistent_ring.hpp"
#include "render/territory_map.hpp"
#include "render/world_overlays.hpp"
#include "render/minimap_renderer.hpp"
#include "render/gl_state.hpp"
#include "render/gl_bindings.hpp"
#include "render/frame_fill.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

namespace ron::render {

// Test 19: Entity_FramePerf
RON_TEST(Entity_FramePerf) {
    GL_TEST_BEGIN();
    auto& scene = test::test_scene();
    scene.init();

    VramLedger ledger;
    EntityAssets assets;
    RON_CHECK(assets.init(ledger,
                          RON_SOURCE_DIR "/assets/compiled/models/models_2026.mdbin",
                          RON_SOURCE_DIR "/assets/compiled/textures/textures_2026.tex26"));

    EntityRenderer renderer;
    RON_CHECK(renderer.init(ledger, assets, &scene.shaders));

    RenderTarget hdr_rt;
    RON_CHECK(hdr_rt.create(ledger, VramCat::HdrTargets, VramCat::DepthTargets, "hdr_perf", 640, 360, TexFmt::RGBA16F, true));

    RenderTarget ldr_rt;
    RON_CHECK(ldr_rt.create(ledger, VramCat::PostFx, VramCat::DepthTargets, "ldr_perf", 640, 360, TexFmt::RGBA8, false));

    const HeightSampler& sampler = test::test_sampler();
    Site sites[8];
    pick_land_sites(sampler, 8, 400.0f, sites);
    float focus_x = sites[0].x;
    float focus_z = sites[0].z;

    CameraRts cam;
    camera_init(cam, focus_x, focus_z, 0.0f, 300.0f);
    camera_snap(cam, &sampler);

    const u32 N = 20000;
    std::vector<CrowdEntity> crowd;
    crowd.reserve(N);
    init_crowd(crowd, N, assets, sampler, focus_x, focus_z);
    RON_CHECK_EQ(crowd.size(), N);

    float aspect = 640.0f / 360.0f;
    Mat4 view = camera_view(cam, &sampler);
    Mat4 proj = camera_proj(aspect);
    Mat4 vp = mat4_mul(proj, view);
    Vec3 eye = camera_eye(cam, &sampler);

    FrameUniforms frame{};
    fill_frame_uniforms(frame, view, proj, eye,
                        640, 360,
                        1.0f, 0.0f, 1.0f,
                        Vec3{0.30f, 0.70f, 0.65f}, 3.0f,
                        Vec3{1.0f, 0.95f, 0.85f});

    glNamedBufferSubData(scene.frame_ubo.id(), 0, sizeof(FrameUniforms), &frame);
    glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_FRAME, scene.frame_ubo.id());

    GpuTimer timer;
    RON_CHECK(timer.create());

    auto run_frame = [&](bool timed, float& out_ms) {
        renderer.begin_frame(0);
        renderer.feed().clear();
        for (const auto& ent : crowd) {
            renderer.feed().add(ent.inst);
        }
        renderer.upload();

        if (timed) timer.begin(0);
        renderer.cull(frame, 1.0f, 1.0f, false);
        if (timed) timer.end(0);

        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GREATER);
        const float clear_color[4] = { 0.50f, 0.62f, 0.80f, 1.0f };
        hdr_rt.clear(clear_color);

        glBindFramebuffer(GL_FRAMEBUFFER, hdr_rt.fbo());
        glViewport(0, 0, 640, 360);

        Frustum frustum = frustum_from_view_proj(vp);
        scene.terrain.frame_begin(eye, frustum);
        scene.terrain.draw_terrain(scene.shaders.program(ProgramId::Terrain));

        if (timed) timer.begin(1);
        renderer.draw(frame);
        if (timed) timer.end(1);

        scene.terrain.draw_ocean(scene.shaders.program(ProgramId::Ocean));
        scene.terrain.frame_end();

        glBindFramebuffer(GL_FRAMEBUFFER, ldr_rt.fbo());
        glViewport(0, 0, 640, 360);
        glDisable(GL_DEPTH_TEST);
        glBindTextureUnit(TEX_HDR, hdr_rt.color_tex());

        u32 tonemap_prog = scene.shaders.program(ProgramId::Tonemap);
        glUseProgram(tonemap_prog);
        glUniform1f(0, 1.25f);
        draw_fullscreen_triangle();
        glUseProgram(0);

        if (timed) {
            glFinish();
            out_ms = timer.elapsed_ms(0) + timer.elapsed_ms(1);
        }
    };

    // 2 warm-up frames
    float dummy_ms = 0.0f;
    for (int w = 0; w < 2; ++w) {
        run_frame(false, dummy_ms);
    }
    glFinish();

    // 7 timed frames
    float times_ms[7] = {0.0f};
    for (int t = 0; t < 7; ++t) {
        run_frame(true, times_ms[t]);
    }

    RON_CHECK_EQ(glGetError(), GL_NO_ERROR);

    auto st = renderer.read_stats_blocking();
    RON_CHECK_EQ(st.input, N);
    RON_CHECK(st.visible > 0u);

    float sorted_times[7];
    std::memcpy(sorted_times, times_ms, sizeof(times_ms));
    std::sort(sorted_times, sorted_times + 7);
    float median = sorted_times[3];

    std::printf("[PERF] Entity_FramePerf 7 timings: %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f ms (median: %.3f ms)\n",
                times_ms[0], times_ms[1], times_ms[2], times_ms[3],
                times_ms[4], times_ms[5], times_ms[6], median);

    RON_CHECK(median < 4.0f);

    timer.destroy();
    renderer.shutdown();
    assets.shutdown();
    GL_TEST_END();
}

// Test 20: Vram_Gate12
RON_TEST(Vram_Gate12) {
    GL_TEST_BEGIN();
    auto& scene = test::test_scene();
    scene.init();

    VramLedger ledger;

    // Boot order matching ron_view_main:
    // 1. TerrainRenderer
    TerrainRenderer terrain;
    RON_CHECK(terrain.init(ledger, test::test_source(), test::test_tex26(), test::test_sampler(), test::test_quadtree()));

    // 2. WorldOverlays
    WorldOverlays overlays;
    RON_CHECK(overlays.init(ledger, {}, &scene.shaders));

    // 3. MinimapRenderer
    MinimapRenderer minimap;
    RON_CHECK(minimap.init(ledger, overlays, &scene.shaders));

    // Snapshot ledger before Gate 12
    u64 vram_before = ledger.total_used();
    u64 ent_buf_before = ledger.used(VramCat::EntityBuffers);
    u64 model_geom_before = ledger.used(VramCat::ModelGeometry);
    u64 trim_tex_before = ledger.used(VramCat::TrimTextures);
    u64 vat_tex_before = ledger.used(VramCat::VatTextures);

    // 4. EntityAssets
    EntityAssets assets;
    RON_CHECK(assets.init(ledger,
                          RON_SOURCE_DIR "/assets/compiled/models/models_2026.mdbin",
                          RON_SOURCE_DIR "/assets/compiled/textures/textures_2026.tex26"));

    // 5. EntityRenderer
    EntityRenderer renderer;
    RON_CHECK(renderer.init(ledger, assets, &scene.shaders));

    // Check ledger deltas for entities
    u64 vram_after = ledger.total_used();
    u64 delta = vram_after - vram_before;
    u64 expected_delta = assets.vram_bytes() + renderer.vram_bytes();
    RON_CHECK_EQ(delta, expected_delta);

    RON_CHECK(ledger.used(VramCat::EntityBuffers) - ent_buf_before <= 40ull * 1024 * 1024);
    RON_CHECK(ledger.used(VramCat::ModelGeometry) - model_geom_before <= 128ull * 1024 * 1024);
    RON_CHECK(ledger.used(VramCat::TrimTextures) - trim_tex_before <= 64ull * 1024 * 1024);
    RON_CHECK(ledger.used(VramCat::VatTextures) - vat_tex_before <= 32ull * 1024 * 1024);

    // 6. HDR RenderTarget + PersistentRing
    RenderTarget hdr_target;
    RON_CHECK(hdr_target.create(ledger, VramCat::HdrTargets, VramCat::DepthTargets, "scene_hdr", 1600, 900, TexFmt::RGBA16F, true));

    GLint ssbo_align = 0;
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &ssbo_align);
    GLint ubo_align = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_align);

    PersistentRing frame_ring;
    RON_CHECK(frame_ring.create(ledger, VramCat::Staging, "frame_uniforms_ring", sizeof(FrameUniforms), ssbo_align, ubo_align));

    // Freeze ledger
    ledger.freeze();
    RON_CHECK(ledger.frozen());
    RON_CHECK(ledger.total_used() <= 2304ULL * 1024ULL * 1024ULL);

    std::printf("[VRAM] EntityBuffers: %llu B (%.2f MiB)\n",
                static_cast<unsigned long long>(ledger.used(VramCat::EntityBuffers)),
                static_cast<double>(ledger.used(VramCat::EntityBuffers)) / (1024.0 * 1024.0));
    std::printf("[VRAM] ModelGeometry: %llu B (%.2f MiB)\n",
                static_cast<unsigned long long>(ledger.used(VramCat::ModelGeometry)),
                static_cast<double>(ledger.used(VramCat::ModelGeometry)) / (1024.0 * 1024.0));
    std::printf("[VRAM] TrimTextures:  %llu B (%.2f MiB)\n",
                static_cast<unsigned long long>(ledger.used(VramCat::TrimTextures)),
                static_cast<double>(ledger.used(VramCat::TrimTextures)) / (1024.0 * 1024.0));
    std::printf("[VRAM] VatTextures:   %llu B (%.2f MiB)\n",
                static_cast<unsigned long long>(ledger.used(VramCat::VatTextures)),
                static_cast<double>(ledger.used(VramCat::VatTextures)) / (1024.0 * 1024.0));
    std::printf("[VRAM] Total Used:    %llu B (%.2f MiB / 2304 MiB cap)\n",
                static_cast<unsigned long long>(ledger.total_used()),
                static_cast<double>(ledger.total_used()) / (1024.0 * 1024.0));

    ledger.dump();

    renderer.shutdown();
    assets.shutdown();
    GL_TEST_END();
}

} // namespace ron::render
