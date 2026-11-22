#include "test_framework.hpp"
#include "render/gl_fixture.hpp"
#include "render/scene_fixture.hpp"
#include "render/entity_renderer.hpp"
#include "render/entity_test_util.hpp"
#include "render/territory_map.hpp"
#include "render/gpu_timer.hpp"
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

namespace ron::render {

static float test_height_callback(float x, float z, void* user) {
    auto* sampler = static_cast<const HeightSampler*>(user);
    return sampler ? sampler->height_m(x, z) : 0.0f;
}

// Global cached test data for Test 10 & 11
static std::vector<EntityInstance> s_test10_instances;
static EntityRenderer::EntityStats s_test10_stats;
static std::vector<u32> s_test10_ranks;
static std::vector<u32> s_test10_counters;
static FrameUniforms s_test10_frame;

// Test 10: Cull_MatchesCpu
RON_TEST(Cull_MatchesCpu) {
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

    const HeightSampler& sampler = test::test_sampler();
    Site sites[8];
    int site_count = pick_land_sites(sampler, 8, 400.0f, sites);
    RON_CHECK(site_count > 0);

    float focus_x = sites[0].x;
    float focus_z = sites[0].z;

    CameraRts cam;
    camera_init(cam, focus_x, focus_z, 0.0f, 120.0f);
    camera_snap(cam, &sampler);

    Mat4 view = camera_view(cam, &sampler);
    Mat4 proj = camera_proj(640.0f / 360.0f);
    Mat4 vp = mat4_mul(proj, view);
    Vec3 eye = camera_eye(cam, &sampler);

    FrameUniforms frame{};
    frame.screen[0] = 640.0f;
    frame.screen[1] = 360.0f;
    frame.cam_pos[0] = eye.x;
    frame.cam_pos[1] = eye.y;
    frame.cam_pos[2] = eye.z;
    std::memcpy(frame.view, view.m, sizeof(frame.view));
    std::memcpy(frame.proj, proj.m, sizeof(frame.proj));
    std::memcpy(frame.view_proj, vp.m, sizeof(frame.view_proj));

    const u32 N = 20000;
    auto instances = test::make_random_instances(
        N, assets.model_count(),
        focus_x, focus_z,
        60.0f, 900.0f,
        1000,
        test_height_callback,
        const_cast<HeightSampler*>(&sampler));

    renderer.begin_frame(0);
    for (const auto& inst : instances) {
        renderer.feed().add(inst);
    }
    renderer.upload();

    renderer.cull(frame, 0.5f, 0.0f, false);

    EntityRenderer::EntityStats stats = renderer.read_stats_blocking();

    std::vector<u32> ranks(N * 2);
    glGetNamedBufferSubData(renderer.rank_buffer(), 0, N * 8, ranks.data());

    std::vector<u32> counters(16 + MAX_BUCKETS);
    glGetNamedBufferSubData(renderer.counters_buffer(), 0, (16 + MAX_BUCKETS) * 4, counters.data());

    // Save for Test 11
    s_test10_instances = instances;
    s_test10_stats = stats;
    s_test10_ranks = ranks;
    s_test10_counters = counters;
    s_test10_frame = frame;

    // CPU reference comparison
    EntityUbo ubo;
    entity_fill_ubo(frame, 0.5f, N, assets.model_count(), 0.0f, &ubo);

    u32 amb_count = 0;
    for (u32 i = 0; i < N; ++i) {
        CullResult res = entity_cull_reference(instances[i], assets.models(), ubo);
        u32 gpu_bucket = ranks[i * 2 + 0];

        if (res.ambiguous) {
            amb_count++;
        } else {
            RON_CHECK(gpu_bucket == res.bucket);
        }
    }

    RON_CHECK(amb_count <= N * 2 / 1000); // Ambiguous <= 0.2% of N

    // Coverage asserts
    RON_CHECK(stats.lod[0] >= 20);
    RON_CHECK(stats.lod[1] >= 20);
    RON_CHECK(stats.lod[2] >= 20);
    RON_CHECK(stats.frustum_culled >= 200);
    RON_CHECK(stats.size_culled >= 200);
    RON_CHECK(stats.visible == (stats.lod[0] + stats.lod[1] + stats.lod[2]));
    RON_CHECK(stats.input == N);
    RON_CHECK(stats.visible + stats.frustum_culled + stats.size_culled + stats.invalid == N);

    renderer.shutdown();
    assets.shutdown();
    GL_TEST_END();
}

// Test 11: Cull_Cmds
RON_TEST(Cull_Cmds) {
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

    const u32 N = static_cast<u32>(s_test10_instances.size());
    RON_CHECK(N == 20000);

    renderer.begin_frame(0);
    for (const auto& inst : s_test10_instances) {
        renderer.feed().add(inst);
    }
    renderer.upload();

    renderer.cull(s_test10_frame, 0.5f, 0.0f, false);

    u32 bucket_count = assets.model_count() * 3;

    std::vector<DrawCmd> cmds(MAX_BUCKETS);
    glGetNamedBufferSubData(renderer.indirect_buffer(), 0, MAX_BUCKETS * sizeof(DrawCmd), cmds.data());

    std::vector<u32> visible(N);
    glGetNamedBufferSubData(renderer.visible_buffer(), 0, N * sizeof(u32), visible.data());

    // Check commands match ModelGpu table
    for (u32 b = 0; b < bucket_count; ++b) {
        u32 m = b / 3;
        u32 l = b % 3;
        const ModelGpu& mg = assets.source().model_gpu(m);
        RON_CHECK(cmds[b].count == mg.lod[l].index_count);
        RON_CHECK(cmds[b].first_index == mg.lod[l].first_index);
        RON_CHECK(cmds[b].base_vertex == mg.lod[l].base_vertex);
    }

    // Prefix sum check
    u32 expected_base = 0;
    for (u32 b = 0; b < bucket_count; ++b) {
        RON_CHECK(cmds[b].base_instance == expected_base);
        expected_base += cmds[b].instance_count;
    }
    RON_CHECK(expected_base == s_test10_stats.visible);

    // Visible list check
    std::vector<bool> seen(N, false);
    for (u32 b = 0; b < bucket_count; ++b) {
        for (u32 k = 0; k < cmds[b].instance_count; ++k) {
            u32 inst_id = visible[cmds[b].base_instance + k];
            RON_CHECK(inst_id < N);
            RON_CHECK(s_test10_ranks[inst_id * 2 + 0] == b);
            RON_CHECK(!seen[inst_id]);
            seen[inst_id] = true;
        }
    }

    renderer.shutdown();
    assets.shutdown();
    GL_TEST_END();
}

// Test 12: Cull_Edge
RON_TEST(Cull_Edge) {
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

    const HeightSampler& sampler = test::test_sampler();
    Site sites[8];
    pick_land_sites(sampler, 8, 400.0f, sites);
    float focus_x = sites[0].x;
    float focus_z = sites[0].z;
    float focus_y = sampler.height_m(focus_x, focus_z);

    CameraRts cam;
    camera_init(cam, focus_x, focus_z, 0.0f, 120.0f);
    camera_snap(cam, &sampler);

    Mat4 view = camera_view(cam, &sampler);
    Mat4 proj = camera_proj(640.0f / 360.0f);
    Mat4 vp = mat4_mul(proj, view);
    Vec3 eye = camera_eye(cam, &sampler);

    FrameUniforms frame{};
    frame.screen[0] = 640.0f;
    frame.screen[1] = 360.0f;
    frame.cam_pos[0] = eye.x;
    frame.cam_pos[1] = eye.y;
    frame.cam_pos[2] = eye.z;
    std::memcpy(frame.view, view.m, sizeof(frame.view));
    std::memcpy(frame.proj, proj.m, sizeof(frame.proj));
    std::memcpy(frame.view_proj, vp.m, sizeof(frame.view_proj));

    // (a) N=0
    renderer.begin_frame(0);
    renderer.upload();
    renderer.cull(frame, 0.0f, 0.0f, false);
    EntityRenderer::EntityStats s0 = renderer.read_stats_blocking();
    RON_CHECK(s0.input == 0);
    RON_CHECK(s0.visible == 0);
    std::vector<DrawCmd> cmds0(MAX_BUCKETS);
    glGetNamedBufferSubData(renderer.indirect_buffer(), 0, MAX_BUCKETS * sizeof(DrawCmd), cmds0.data());
    for (u32 b = 0; b < static_cast<u32>(assets.model_count() * 3); ++b) {
        RON_CHECK(cmds0[b].instance_count == 0);
    }

    // (b) N=65536 copies of model 0 at focus
    renderer.begin_frame(1);
    EntityInstance inst_f{};
    inst_f.ids = entity_pack_ids(0, 1, 0);
    inst_f.scale = 1.0f;
    inst_f.pos_prev[0] = focus_x; inst_f.pos_prev[1] = focus_y; inst_f.pos_prev[2] = focus_z;
    inst_f.pos_curr[0] = focus_x; inst_f.pos_curr[1] = focus_y; inst_f.pos_curr[2] = focus_z;
    for (u32 i = 0; i < MAX_INSTANCES; ++i) {
        renderer.feed().add(inst_f);
    }
    renderer.upload();
    renderer.cull(frame, 0.0f, 0.0f, false);
    EntityRenderer::EntityStats s64k = renderer.read_stats_blocking();
    RON_CHECK(s64k.visible == MAX_INSTANCES);

    std::vector<DrawCmd> cmds64k(MAX_BUCKETS);
    glGetNamedBufferSubData(renderer.indirect_buffer(), 0, MAX_BUCKETS * sizeof(DrawCmd), cmds64k.data());
    u32 full_buckets = 0;
    for (u32 b = 0; b < static_cast<u32>(assets.model_count() * 3); ++b) {
        if (cmds64k[b].instance_count == MAX_INSTANCES) full_buckets++;
        else RON_CHECK(cmds64k[b].instance_count == 0);
    }
    RON_CHECK(full_buckets == 1);

    std::vector<u32> vis64k(MAX_INSTANCES);
    glGetNamedBufferSubData(renderer.visible_buffer(), 0, MAX_INSTANCES * sizeof(u32), vis64k.data());
    std::vector<bool> seen64k(MAX_INSTANCES, false);
    for (u32 i = 0; i < MAX_INSTANCES; ++i) {
        RON_CHECK(vis64k[i] < MAX_INSTANCES);
        RON_CHECK(!seen64k[vis64k[i]]);
        seen64k[vis64k[i]] = true;
    }

    // (c) 5000 instances 50m behind eye
    renderer.begin_frame(2);
    EntityInstance inst_behind{};
    inst_behind.ids = entity_pack_ids(0, 1, 0);
    inst_behind.scale = 1.0f;
    float dir_x = eye.x - focus_x;
    float dir_z = eye.z - focus_z;
    float len = std::sqrt(dir_x * dir_x + dir_z * dir_z);
    float bx = eye.x + (dir_x / len) * 50.0f;
    float bz = eye.z + (dir_z / len) * 50.0f;
    inst_behind.pos_prev[0] = bx; inst_behind.pos_prev[1] = eye.y; inst_behind.pos_prev[2] = bz;
    inst_behind.pos_curr[0] = bx; inst_behind.pos_curr[1] = eye.y; inst_behind.pos_curr[2] = bz;
    for (int i = 0; i < 5000; ++i) {
        renderer.feed().add(inst_behind);
    }
    renderer.upload();
    renderer.cull(frame, 0.0f, 0.0f, false);
    EntityRenderer::EntityStats s_behind = renderer.read_stats_blocking();
    RON_CHECK(s_behind.visible == 0);
    RON_CHECK(s_behind.frustum_culled == 5000);

    // (d) One invalid model instance
    renderer.begin_frame(3);
    EntityInstance inst_inv{};
    inst_inv.ids = entity_pack_ids(assets.model_count(), 1, 0);
    inst_inv.scale = 1.0f;
    inst_inv.pos_prev[0] = focus_x; inst_inv.pos_prev[1] = focus_y; inst_inv.pos_prev[2] = focus_z;
    inst_inv.pos_curr[0] = focus_x; inst_inv.pos_curr[1] = focus_y; inst_inv.pos_curr[2] = focus_z;
    renderer.feed().add(inst_inv);
    renderer.upload();
    renderer.cull(frame, 0.0f, 0.0f, false);
    EntityRenderer::EntityStats s_inv = renderer.read_stats_blocking();
    RON_CHECK(s_inv.invalid == 1);
    RON_CHECK(s_inv.visible == 0);

    renderer.shutdown();
    assets.shutdown();
    GL_TEST_END();
}

// Test 13: Cull_Perf
RON_TEST(Cull_Perf) {
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

    const HeightSampler& sampler = test::test_sampler();
    Site sites[8];
    pick_land_sites(sampler, 8, 400.0f, sites);
    float focus_x = sites[0].x;
    float focus_z = sites[0].z;

    CameraRts cam;
    camera_init(cam, focus_x, focus_z, 0.0f, 300.0f);
    camera_snap(cam, &sampler);

    Mat4 view = camera_view(cam, &sampler);
    Mat4 proj = camera_proj(640.0f / 360.0f);
    Mat4 vp = mat4_mul(proj, view);
    Vec3 eye = camera_eye(cam, &sampler);

    FrameUniforms frame{};
    frame.screen[0] = 640.0f;
    frame.screen[1] = 360.0f;
    frame.cam_pos[0] = eye.x;
    frame.cam_pos[1] = eye.y;
    frame.cam_pos[2] = eye.z;
    std::memcpy(frame.view, view.m, sizeof(frame.view));
    std::memcpy(frame.proj, proj.m, sizeof(frame.proj));
    std::memcpy(frame.view_proj, vp.m, sizeof(frame.view_proj));

    const u32 N = 32768;
    auto instances = test::make_random_instances(
        N, assets.model_count(),
        focus_x, focus_z,
        100.0f, 600.0f,
        2000,
        test_height_callback,
        const_cast<HeightSampler*>(&sampler));

    renderer.begin_frame(0);
    for (const auto& inst : instances) {
        renderer.feed().add(inst);
    }
    renderer.upload();

    GpuTimer timer;
    RON_CHECK(timer.create());

    // 2 warm-up runs
    for (int w = 0; w < 2; ++w) {
        renderer.cull(frame, 0.5f, 0.0f, false);
    }
    glFinish();

    // 7 timed runs
    float times_ms[7];
    for (int t = 0; t < 7; ++t) {
        timer.begin(0);
        renderer.cull(frame, 0.5f, 0.0f, false);
        timer.end(0);
        times_ms[t] = timer.elapsed_ms(0);
    }

    float sorted_times[7];
    std::memcpy(sorted_times, times_ms, sizeof(times_ms));
    std::sort(sorted_times, sorted_times + 7);
    float median = sorted_times[3];

    std::printf("[PERF] Cull_Perf 7 timings: %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f ms (median: %.3f ms)\n",
                times_ms[0], times_ms[1], times_ms[2], times_ms[3],
                times_ms[4], times_ms[5], times_ms[6], median);

#if defined(RON_DEBUG) && RON_DEBUG
    RON_CHECK(median < 2.0f); // Spec: if Debug build is >0.6 ms, cap applies to Release; log both
#else
    RON_CHECK(median < 0.6f);
#endif

    timer.destroy();
    renderer.shutdown();
    assets.shutdown();
    GL_TEST_END();
}

} // namespace ron::render
