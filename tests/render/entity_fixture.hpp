#pragma once

#include "render/scene_fixture.hpp"
#include "render/entity_renderer.hpp"
#include "render/entity_assets.hpp"
#include "render/render_target.hpp"
#include "render/camera_rts.hpp"
#include "render/gl_state.hpp"
#include "render/gl_bindings.hpp"
#include "render/frame_fill.hpp"
#include "render/world_overlays.hpp"
#include <vector>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

namespace ron::render::test {

struct EntityDrawResult {
    std::vector<u8> color_rgba8; // 640 * 360 * 4
    std::vector<float> depth;    // 640 * 360
};

class EntityDrawFixture {
public:
    VramLedger ledger;
    EntityAssets assets;
    EntityRenderer renderer;
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

        RON_CHECK(renderer.init(ledger, assets, &scene.shaders));

        RON_CHECK(hdr_rt.create(ledger, VramCat::HdrTargets, VramCat::DepthTargets, "hdr_rt_fix", 640, 360, TexFmt::RGBA16F, true));
        RON_CHECK(ldr_rt.create(ledger, VramCat::PostFx, VramCat::DepthTargets, "ldr_rt_fix", 640, 360, TexFmt::RGBA8, false));

        initialized = true;
    }

    EntityDrawResult render(
        const CameraRts& cam,
        const std::vector<EntityInstance>& instances,
        float alpha = 1.0f,
        float time_s = 1.0f,
        bool draw_ocean = true,
        WorldOverlays* ovl = nullptr,
        const float clear_color[4] = nullptr,
        bool draw_terrain = true,
        bool draw_entities = true)
    {
        init();
        auto& scene = test_scene();

        // 1. Update overlays
        renderer.set_overlays(ovl);
        scene.terrain.set_overlays(ovl);

        // 2. Camera & Frame uniforms
        float aspect = 640.0f / 360.0f;
        Mat4 view = camera_view(cam, &test_sampler());
        Mat4 proj = camera_proj(aspect);
        Mat4 view_proj = mat4_mul(proj, view);
        Vec3 eye = camera_eye(cam, &test_sampler());

        FrameUniforms frame{};
        fill_frame_uniforms(frame, view, proj, eye,
                            640, 360,
                            time_s, 0.0f, alpha,
                            Vec3{0.30f, 0.70f, 0.65f}, 3.0f,
                            Vec3{1.0f, 0.95f, 0.85f});

        glNamedBufferSubData(scene.frame_ubo.id(), 0, sizeof(FrameUniforms), &frame);
        glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_FRAME, scene.frame_ubo.id());

        // 3. Entities begin_frame / upload / cull
        renderer.begin_frame(0);
        renderer.feed().clear();
        for (const auto& inst : instances) {
            renderer.feed().add(inst);
        }
        renderer.upload();
        renderer.cull(frame, alpha, time_s, false);

        // 4. Bind HDR target + clear
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GREATER);

        static const float def_clear[4] = { 0.50f, 0.62f, 0.80f, 1.0f };
        const float* clr = clear_color ? clear_color : def_clear;
        hdr_rt.clear(clr);

        glBindFramebuffer(GL_FRAMEBUFFER, hdr_rt.fbo());
        glViewport(0, 0, 640, 360);

        // 5. Draw terrain
        Frustum frustum = frustum_from_view_proj(view_proj);
        scene.terrain.frame_begin(eye, frustum);
        if (draw_terrain) {
            scene.terrain.draw_terrain(scene.shaders.program(ovl ? ProgramId::TerrainOvl : ProgramId::Terrain));
        }

        // 6. Draw entities
        if (draw_entities) {
            renderer.draw(frame);
        }

        // 7. Draw ocean
        if (draw_ocean) {
            scene.terrain.draw_ocean(scene.shaders.program(ovl ? ProgramId::OceanOvl : ProgramId::Ocean));
        }
        scene.terrain.frame_end();

        // 8. Tonemap
        glBindFramebuffer(GL_FRAMEBUFFER, ldr_rt.fbo());
        glViewport(0, 0, 640, 360);
        glDisable(GL_DEPTH_TEST);
        glBindTextureUnit(TEX_HDR, hdr_rt.color_tex());

        u32 tonemap_prog = scene.shaders.program(ProgramId::Tonemap);
        glUseProgram(tonemap_prog);
        glUniform1f(0, 1.25f);
        draw_fullscreen_triangle();
        glUseProgram(0);

        // 9. Readback
        EntityDrawResult res;
        res.color_rgba8.resize(640 * 360 * 4);
        glGetTextureSubImage(ldr_rt.color_tex(), 0, 0, 0, 0, 640, 360, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(res.color_rgba8.size()), res.color_rgba8.data());

        res.depth.resize(640 * 360);
        glGetTextureSubImage(hdr_rt.depth_tex(), 0, 0, 0, 0, 640, 360, 1,
                            GL_DEPTH_COMPONENT, GL_FLOAT, static_cast<GLsizei>(res.depth.size() * sizeof(float)), res.depth.data());

        return res;
    }
};

inline EntityDrawFixture& test_entity_fixture() {
    static EntityDrawFixture s_fix;
    s_fix.init();
    return s_fix;
}

} // namespace ron::render::test
