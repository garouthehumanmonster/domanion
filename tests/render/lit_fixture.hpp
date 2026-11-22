#pragma once

// LitFixture: Full frame rendering fixture in Gate 13 order:
// overlays -> entity cull -> lighting update -> shadow pass -> HDR clear -> terrain -> entities -> ocean -> tonemap -> icons -> bars -> readback
//
// Real helper names:
// - LitOptions: options passed to render()
// - LitRenderResult: image readbacks (color_rgba8, depth) and frame matrices (view, proj, view_proj, eye, fwd)
// - LitFixture::render(const LitOptions&): main entry point
// - test_lit_fixture(): singleton accessor

#include "render/scene_fixture.hpp"
#include "render/entity_renderer.hpp"
#include "render/entity_assets.hpp"
#include "render/lighting_system.hpp"
#include "render/render_target.hpp"
#include "render/camera_rts.hpp"
#include "render/gl_state.hpp"
#include "render/gl_bindings.hpp"
#include "render/frame_fill.hpp"
#include "render/world_overlays.hpp"
#include "render/light_types.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

namespace ron::render::test {

struct LitOptions {
    Vec3 focus{2048.0f, 0.0f, 2048.0f};
    float dist{250.0f};
    float pitch{45.0f};
    float yaw{0.0f};
    float sun_elev{40.0f};
    float sun_az{135.0f};
    Vec3 sun_color{3.0f, 2.85f, 2.55f};
    float sun_intensity{1.0f};
    u32 light_flags{LIGHT_FLAG_CSM | LIGHT_FLAG_TERRAIN_SHADOW};
    bool enable_lighting{true};
    bool enable_marks{false};
    bool enable_overlays{false};
    bool draw_terrain{true};
    bool draw_entities{true};
    bool draw_ocean{true};
    std::vector<EntityInstance> entities;
    float time_s{1.0f};
    float alpha{1.0f};
};

struct LitRenderResult {
    std::vector<u8> color_rgba8; // 640 * 360 * 4
    std::vector<float> depth;    // 640 * 360
    Mat4 view;
    Mat4 proj;
    Mat4 view_proj;
    Vec3 eye;
    Vec3 fwd;
};

inline float calc_luminance(u8 r, u8 g, u8 b) noexcept {
    return 0.299f * static_cast<float>(r) + 0.587f * static_cast<float>(g) + 0.114f * static_cast<float>(b);
}

inline float calc_mean_luminance(const std::vector<u8>& color, int x0, int y0, int w, int h, int stride = 640) noexcept {
    if (color.empty() || w <= 0 || h <= 0) return 0.0f;
    double sum = 0.0;
    int count = 0;
    for (int y = y0; y < y0 + h; ++y) {
        if (y < 0 || y >= 360) continue;
        for (int x = x0; x < x0 + w; ++x) {
            if (x < 0 || x >= stride) continue;
            size_t idx = static_cast<size_t>((y * stride + x) * 4);
            sum += calc_luminance(color[idx], color[idx + 1], color[idx + 2]);
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(sum / count) : 0.0f;
}

class LitFixture {
public:
    VramLedger ledger;
    EntityAssets assets;
    EntityRenderer entity_renderer;
    LightingSystem lighting;
    WorldOverlays overlays;
    RenderTarget hdr_rt;
    RenderTarget ldr_rt;
    bool initialized{false};

    void init() {
        if (initialized) return;

        auto& scene = test_scene();
        scene.init();

        RON_CHECK(assets.init(ledger,
                              RON_SOURCE_DIR "/assets/compiled/models/models_2026.mdbin",
                              RON_SOURCE_DIR "/assets/compiled/textures/textures_2026.tex26"));

        RON_CHECK(entity_renderer.init(ledger, assets, &scene.shaders));
        RON_CHECK(lighting.init(ledger, scene.shaders));

        RON_CHECK(overlays.init(ledger, &scene.shaders));

        RON_CHECK(hdr_rt.create(ledger, VramCat::HdrTargets, VramCat::DepthTargets, "lit_hdr_rt", 640, 360, TexFmt::RGBA16F, true));
        RON_CHECK(ldr_rt.create(ledger, VramCat::PostFx, VramCat::DepthTargets, "lit_ldr_rt", 640, 360, TexFmt::RGBA8, false));

        initialized = true;
    }

    LitRenderResult render(const LitOptions& opt) {
        init();
        auto& scene = test_scene();

        // 1. Camera setup
        CameraRts cam{};
        camera_init(cam, opt.focus.x, opt.focus.z, opt.yaw, opt.dist);
        camera_snap(cam, &test_sampler());

        float aspect = 640.0f / 360.0f;
        Mat4 view = camera_view(cam, &test_sampler());
        Mat4 proj = camera_proj(aspect);
        Mat4 view_proj = mat4_mul(proj, view);
        Vec3 eye = camera_eye(cam, &test_sampler());
        Vec3 fwd{ -view.m[2], -view.m[6], -view.m[10] };

        // 2. Fill frame uniforms
        FrameUniforms fu{};
        if (opt.enable_lighting) {
            fill_frame_uniforms(fu, view, proj, eye,
                                640, 360,
                                opt.time_s, 0.0f, opt.alpha,
                                Vec3{0.30f, 0.70f, 0.65f}, opt.sun_intensity,
                                opt.sun_color);
            set_sun(fu, opt.sun_elev, opt.sun_az);
            fu.sun_dir[3] = opt.sun_intensity;
            fu.sun_color[0] = opt.sun_color.x;
            fu.sun_color[1] = opt.sun_color.y;
            fu.sun_color[2] = opt.sun_color.z;
            fu.sun_color[3] = 0.0f;
        } else {
            fill_frame_uniforms(fu, view, proj, eye,
                                640, 360,
                                opt.time_s, 0.0f, opt.alpha,
                                Vec3{0.30f, 0.70f, 0.65f}, 3.0f,
                                Vec3{1.0f, 0.95f, 0.85f});
        }

        glNamedBufferSubData(scene.frame_ubo.id(), 0, sizeof(FrameUniforms), &fu);
        glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_FRAME, scene.frame_ubo.id());

        // 3. Overlays
        if (opt.enable_overlays) {
            scene.terrain.set_overlays(&overlays);
            entity_renderer.set_overlays(&overlays);
        } else {
            scene.terrain.set_overlays(nullptr);
            entity_renderer.set_overlays(nullptr);
        }

        // 4. Entity feed / upload / cull
        entity_renderer.begin_frame(0);
        entity_renderer.feed().clear();
        for (const auto& inst : opt.entities) {
            entity_renderer.feed().add(inst);
        }
        entity_renderer.upload();
        entity_renderer.set_frustum_margin(ENTITY_FRUSTUM_MARGIN);
        entity_renderer.cull(fu, opt.alpha, opt.time_s, false);

        // 5. Lighting update & shadow pass
        if (opt.enable_lighting) {
            lighting.update(fu, cam, opt.light_flags);
            lighting.render_shadows(entity_renderer, scene.shaders);
            scene.terrain.set_lighting(&lighting);
            entity_renderer.set_lighting(&lighting);
        } else {
            scene.terrain.set_lighting(nullptr);
            entity_renderer.set_lighting(nullptr);
        }

        // 6. Bind HDR target + clear
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GREATER);

        static const float def_clear[4] = { 0.50f, 0.62f, 0.80f, 1.0f };
        hdr_rt.clear(def_clear);

        glBindFramebuffer(GL_FRAMEBUFFER, hdr_rt.fbo());
        glViewport(0, 0, 640, 360);

        // 7. Draw terrain & entities & ocean
        Frustum frustum = frustum_from_view_proj(view_proj);
        scene.terrain.set_shaders(&scene.shaders);
        scene.terrain.frame_begin(eye, frustum);
        if (opt.draw_terrain) {
            scene.terrain.draw_terrain(scene.shaders.program(opt.enable_overlays ? ProgramId::TerrainOvl : ProgramId::Terrain));
        }
        if (opt.draw_entities && !opt.entities.empty()) {
            entity_renderer.draw(fu);
        }
        if (opt.draw_ocean) {
            scene.terrain.draw_ocean(scene.shaders.program(opt.enable_overlays ? ProgramId::OceanOvl : ProgramId::Ocean));
        }
        scene.terrain.frame_end();

        // 8. Tonemap pass: HDR -> LDR
        glBindFramebuffer(GL_FRAMEBUFFER, ldr_rt.fbo());
        glViewport(0, 0, 640, 360);
        glDisable(GL_DEPTH_TEST);
        glBindTextureUnit(TEX_HDR, hdr_rt.color_tex());

        u32 tonemap_prog = scene.shaders.program(ProgramId::Tonemap);
        glUseProgram(tonemap_prog);
        glUniform1f(0, 1.25f);
        draw_fullscreen_triangle();
        glUseProgram(0);

        // 9. Readback LDR color & HDR depth
        LitRenderResult res{};
        res.color_rgba8.resize(640 * 360 * 4);
        res.depth.resize(640 * 360);

        glGetTextureSubImage(ldr_rt.color_tex(), 0, 0, 0, 0, 640, 360, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(res.color_rgba8.size()), res.color_rgba8.data());
        glGetTextureSubImage(hdr_rt.depth_tex(), 0, 0, 0, 0, 640, 360, 1,
                            GL_DEPTH_COMPONENT, GL_FLOAT, static_cast<GLsizei>(res.depth.size() * sizeof(float)), res.depth.data());

        res.view = view;
        res.proj = proj;
        res.view_proj = view_proj;
        res.eye = eye;
        res.fwd = fwd;
        return res;
    }
};

inline LitFixture& test_lit_fixture() {
    static LitFixture s_fix;
    s_fix.init();
    return s_fix;
}

} // namespace ron::render::test
