#include "test_framework.hpp"
#include "render/gl_fixture.hpp"
#include "render/scene_fixture.hpp"
#include "render/lighting_system.hpp"
#include "render/entity_renderer.hpp"
#include "render/entity_assets.hpp"
#include "render/shadow_ref.hpp"
#include "render/pbr_ref.hpp"
#include "render/gl_bindings.hpp"
#include "render/camera_rts.hpp"
#include "render/frame_fill.hpp"
#include "render/light_types.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

namespace ron::render {

// Test 10: Shadow_TerrainGpu
RON_TEST(Shadow_TerrainGpu) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    VramLedger ledger;
    auto& scene = test::test_scene();
    scene.init();

    LightingSystem lighting;
    RON_CHECK(lighting.init(ledger, scene.shaders));

    const auto& hs = test::test_sampler();

    struct ShadowProbeIn {
        float pos[4];
        float dir[4];
    };

    ShadowProbeIn probes_in[64];
    float expected[64];

    // Sun direction: elev 25 deg, az 120 deg
    FrameUniforms fu{};
    set_sun(fu, 25.0f, 120.0f);
    float L[3];
    light_to_sun(fu, L);

    for (int i = 0; i < 64; ++i) {
        float x = 500.0f + static_cast<float>(i % 8) * 380.0f;
        float z = 500.0f + static_cast<float>(i / 8) * 380.0f;
        float y = hs.height_m(x, z);

        probes_in[i].pos[0] = x;
        probes_in[i].pos[1] = y;
        probes_in[i].pos[2] = z;
        probes_in[i].pos[3] = 1.0f;

        probes_in[i].dir[0] = L[0];
        probes_in[i].dir[1] = L[1];
        probes_in[i].dir[2] = L[2];
        probes_in[i].dir[3] = 0.0f;

        expected[i] = terrain_shadow_ref(hs, x, y, z, L, 48);
    }

    CameraRts cam;
    camera_init(cam, 2048.0f, 2048.0f, 0.0f, 150.0f);
    lighting.update(fu, cam, LIGHT_FLAG_CSM | LIGHT_FLAG_TERRAIN_SHADOW);

    GpuBuffer ssbo_in;
    RON_CHECK(ssbo_in.create(ledger, VramCat::Reserve, "test_shadow_in", sizeof(probes_in), probes_in, GL_DYNAMIC_STORAGE_BIT));

    GpuBuffer ssbo_out;
    RON_CHECK(ssbo_out.create(ledger, VramCat::Reserve, "test_shadow_out", 64 * sizeof(float), nullptr, GL_DYNAMIC_STORAGE_BIT));

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_TEST_IN, ssbo_in.id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_TEST_OUT, ssbo_out.id());
    lighting.bind(scene.terrain.height_tex().id());

    u32 prog = scene.shaders.program(ProgramId::ShadowSelfTest);
    RON_CHECK(prog != 0);
    glUseProgram(prog);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    float results_out[64]{0.0f};
    glGetNamedBufferSubData(ssbo_out.id(), 0, sizeof(results_out), results_out);

    u32 matches = 0;
    for (int i = 0; i < 64; ++i) {
        float diff = std::abs(results_out[i] - expected[i]);
        if (diff < 0.05f || (results_out[i] > 0.5f) == (expected[i] > 0.5f)) {
            matches++;
        }
    }
    RON_CHECK_EQ(matches, 64u);

    ssbo_in.destroy();
    ssbo_out.destroy();
    lighting.destroy(ledger);

    GL_TEST_END();
}

// Test 11: Pbr_Gpu
RON_TEST(Pbr_Gpu) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    VramLedger ledger;
    auto& scene = test::test_scene();
    scene.init();

    struct alignas(16) PbrTestIn {
        float albedo_rough[4];
        float metal_ao_vis[4];
        float N[4];
        float V[4];
        float L[4];
        float sun_rgb[4];
        float sky[4];
        float ground[4];
    };
    static_assert(sizeof(PbrTestIn) == 128);

    PbrTestIn probes_in[64];
    Vec3 expected[64];

    for (int i = 0; i < 64; ++i) {
        float t = static_cast<float>(i) / 63.0f;
        float rough = 0.04f + 0.96f * t;
        float metal = (i % 3 == 0) ? 0.0f : ((i % 3 == 1) ? 1.0f : 0.5f);
        float ao    = 0.7f + 0.3f * std::cos(t * 6.28f);
        float vis   = (i % 4 == 0) ? 0.0f : ((i % 4 == 1) ? 0.5f : 1.0f);

        Vec3 alb = (i % 2 == 0) ? Vec3(0.8f, 0.2f, 0.2f) : Vec3(0.2f, 0.6f, 0.9f);
        Vec3 N   = Vec3(std::sin(t * 3.14f) * 0.2f, 1.0f, std::cos(t * 3.14f) * 0.2f).normalize();
        Vec3 V   = Vec3(0.0f, 0.7071f, 0.7071f).normalize();
        Vec3 L   = Vec3(0.5f, 0.8f, 0.3f).normalize();
        Vec3 sun_rgb = Vec3(3.0f, 2.85f, 2.55f);
        Vec3 sky = Vec3(0.38f, 0.52f, 0.78f);
        Vec3 ground = Vec3(0.20f, 0.18f, 0.15f);
        float sky_i = 0.55f;

        probes_in[i].albedo_rough[0] = alb.x;
        probes_in[i].albedo_rough[1] = alb.y;
        probes_in[i].albedo_rough[2] = alb.z;
        probes_in[i].albedo_rough[3] = rough;

        probes_in[i].metal_ao_vis[0] = metal;
        probes_in[i].metal_ao_vis[1] = ao;
        probes_in[i].metal_ao_vis[2] = vis;
        probes_in[i].metal_ao_vis[3] = sky_i;

        probes_in[i].N[0] = N.x; probes_in[i].N[1] = N.y; probes_in[i].N[2] = N.z; probes_in[i].N[3] = 0.0f;
        probes_in[i].V[0] = V.x; probes_in[i].V[1] = V.y; probes_in[i].V[2] = V.z; probes_in[i].V[3] = 0.0f;
        probes_in[i].L[0] = L.x; probes_in[i].L[1] = L.y; probes_in[i].L[2] = L.z; probes_in[i].L[3] = 0.0f;

        probes_in[i].sun_rgb[0] = sun_rgb.x; probes_in[i].sun_rgb[1] = sun_rgb.y; probes_in[i].sun_rgb[2] = sun_rgb.z; probes_in[i].sun_rgb[3] = 0.0f;
        probes_in[i].sky[0] = sky.x; probes_in[i].sky[1] = sky.y; probes_in[i].sky[2] = sky.z; probes_in[i].sky[3] = 0.0f;
        probes_in[i].ground[0] = ground.x; probes_in[i].ground[1] = ground.y; probes_in[i].ground[2] = ground.z; probes_in[i].ground[3] = 0.0f;

        expected[i] = pbr_shade(alb, rough, metal, ao, N, V, L, sun_rgb, vis, sky, ground, sky_i);
    }

    GpuBuffer ssbo_in;
    RON_CHECK(ssbo_in.create(ledger, VramCat::Reserve, "test_pbr_in", sizeof(probes_in), probes_in, GL_DYNAMIC_STORAGE_BIT));

    GpuBuffer ssbo_out;
    RON_CHECK(ssbo_out.create(ledger, VramCat::Reserve, "test_pbr_out", 64 * 4 * sizeof(float), nullptr, GL_DYNAMIC_STORAGE_BIT));

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_TEST_IN, ssbo_in.id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_TEST_OUT, ssbo_out.id());

    u32 prog = scene.shaders.program(ProgramId::PbrSelfTest);
    RON_CHECK(prog != 0);
    glUseProgram(prog);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    float results_out[64 * 4]{0.0f};
    glGetNamedBufferSubData(ssbo_out.id(), 0, sizeof(results_out), results_out);

    float max_delta = 0.0f;
    for (int i = 0; i < 64; ++i) {
        float dx = std::abs(results_out[i * 4 + 0] - expected[i].x);
        float dy = std::abs(results_out[i * 4 + 1] - expected[i].y);
        float dz = std::abs(results_out[i * 4 + 2] - expected[i].z);
        max_delta = std::max({max_delta, dx, dy, dz});
    }
    RON_CHECK(max_delta < 0.01f);

    ssbo_in.destroy();
    ssbo_out.destroy();

    GL_TEST_END();
}

// Test 12: Shadow_CascadeDepth
RON_TEST(Shadow_CascadeDepth) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    VramLedger ledger;
    auto& scene = test::test_scene();
    scene.init();

    EntityAssets assets;
    RON_CHECK(assets.init(ledger,
                          RON_SOURCE_DIR "/assets/compiled/models/models_2026.mdbin",
                          RON_SOURCE_DIR "/assets/compiled/textures/textures_2026.tex26"));

    EntityRenderer entity_renderer;
    RON_CHECK(entity_renderer.init(ledger, assets, &scene.shaders));
    entity_renderer.set_shadow_margin(ENTITY_FRUSTUM_MARGIN);

    LightingSystem lighting;
    RON_CHECK(lighting.init(ledger, scene.shaders));

    const auto& sampler = test::test_sampler();
    float focus_x = 2048.0f;
    float focus_z = 2048.0f;

    // Spawn 100 entities in grid around focus
    std::vector<EntityInstance> instances(100);
    for (int i = 0; i < 100; ++i) {
        float dx = static_cast<float>(i % 10 - 5) * 8.0f;
        float dz = static_cast<float>(i / 10 - 5) * 8.0f;
        float ex = focus_x + dx;
        float ez = focus_z + dz;
        float ey = sampler.height_m(ex, ez);

        instances[i].pos_prev[0] = ex; instances[i].pos_prev[1] = ey; instances[i].pos_prev[2] = ez;
        instances[i].yaw_prev = 0.0f;
        instances[i].pos_curr[0] = ex; instances[i].pos_curr[1] = ey; instances[i].pos_curr[2] = ez;
        instances[i].yaw_curr = 0.0f;
        instances[i].ids = entity_pack_ids(14, 1, 0); // veh_mbt
        instances[i].clip = 0;
        instances[i].anim_t = 0.0f;
        instances[i].scale = 1.0f;
        instances[i].hp = 1.0f;
    }

    CameraRts cam;
    camera_init(cam, focus_x, focus_z, 0.0f, 120.0f);
    camera_snap(cam, &sampler);
    Mat4 view = camera_view(cam, &sampler);
    Mat4 proj = camera_proj(640.0f / 360.0f);
    Vec3 eye = camera_eye(cam, &sampler);

    FrameUniforms fu{};
    fill_frame_uniforms(fu, view, proj, eye, 640, 360, 0.0f, 0.0f, 1.0f,
                        Vec3{0.3f, 0.7f, 0.65f}, 3.0f, Vec3{1.0f, 0.95f, 0.85f});
    set_sun(fu, 40.0f, 135.0f);

    entity_renderer.begin_frame(0);
    for (const auto& inst : instances) {
        entity_renderer.feed().add(inst);
    }
    entity_renderer.upload();
    entity_renderer.cull(fu, 1.0f, 0.0f, false);

    lighting.update(fu, cam, LIGHT_FLAG_CSM | LIGHT_FLAG_TERRAIN_SHADOW);
    lighting.render_shadows(entity_renderer, scene.shaders);

    // Read back cascade 0 depth from shadow map layer 0
    std::vector<float> layer0_depth(2048 * 2048);
    glGetTextureSubImage(lighting.shadow_map().id(), 0,
                        0, 0, 0,
                        2048, 2048, 1,
                        GL_DEPTH_COMPONENT, GL_FLOAT,
                        sizeof(float) * layer0_depth.size(),
                        layer0_depth.data());

    u32 caster_texels = 0;
    for (float d : layer0_depth) {
        if (d > 0.0f) {
            caster_texels++;
        }
    }
    RON_CHECK(caster_texels > 0);

    lighting.destroy(ledger);
    entity_renderer.shutdown();
    assets.shutdown();

    GL_TEST_END();
}

} // namespace ron::render
