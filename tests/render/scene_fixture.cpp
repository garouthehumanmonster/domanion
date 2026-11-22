#include "render/scene_fixture.hpp"
#include "core/arena.hpp"
#include "core/assert_log.hpp"
#include "render/gl_state.hpp"
#include "render/gl_bindings.hpp"
#include "render/shader_preproc.hpp"
#include "render/frame_fill.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

namespace ron::render::test {

namespace {

const char* get_theatre_path() {
    static const char* paths[] = {
        RON_SOURCE_DIR "/assets/compiled/theatres/continent_2026.thtr",
        "assets/compiled/theatres/continent_2026.thtr",
        "../assets/compiled/theatres/continent_2026.thtr",
        "../../assets/compiled/theatres/continent_2026.thtr"
    };
    for (const char* p : paths) {
        FILE* f = fopen(p, "rb");
        if (f) {
            fclose(f);
            return p;
        }
    }
    return paths[0];
}

const char* get_textures_path() {
    static const char* paths[] = {
        RON_SOURCE_DIR "/assets/compiled/textures/textures_2026.tex26",
        "assets/compiled/textures/textures_2026.tex26",
        "../assets/compiled/textures/textures_2026.tex26",
        "../../assets/compiled/textures/textures_2026.tex26"
    };
    for (const char* p : paths) {
        FILE* f = fopen(p, "rb");
        if (f) {
            fclose(f);
            return p;
        }
    }
    return paths[0];
}

const char* get_shaders_dir() {
    static const char* paths[] = {
        RON_SOURCE_DIR "/assets/shaders",
        "assets/shaders",
        "../assets/shaders",
        "../../assets/shaders"
    };
    for (const char* p : paths) {
        std::string test_file = std::string(p) + "/common/frame.glsl";
        FILE* f = fopen(test_file.c_str(), "rb");
        if (f) {
            fclose(f);
            return p;
        }
    }
    return paths[0];
}

struct SceneDataFixture {
    sim::TheatreData theatre;
    assets::TextureData tex26;
    TerrainSource source;
    HeightSampler sampler;
    TerrainQuadtree quadtree;
    bool initialized{false};

    void init() {
        if (initialized) return;

        static MemoryArena arena_thtr;
        if (!arena_thtr.init(MiB(32), MiB(1), "scene_thtr").is_ok()) {
            RON_LOG_ERROR("Failed to init arena_thtr");
            std::abort();
        }
        const char* thtr_path = get_theatre_path();
        auto thtr_res = theatre.load_file(thtr_path, arena_thtr);
        if (!thtr_res.is_ok() || !theatre.is_valid()) {
            RON_LOG_ERROR("Failed to load theatre: run the Gate 06/08 bake (%s)", thtr_path);
            std::abort();
        }

        static MemoryArena arena_tex;
        if (!arena_tex.init(MiB(96), MiB(1), "scene_tex").is_ok()) {
            RON_LOG_ERROR("Failed to init arena_tex");
            std::abort();
        }
        const char* tex_path = get_textures_path();
        auto tex_err = tex26.load_file(tex_path, arena_tex);
        if (tex_err != assets::TextureError::Ok || !tex26.is_valid()) {
            RON_LOG_ERROR("Failed to load textures: run the Gate 06/08 bake (%s)", tex_path);
            std::abort();
        }

        if (!terrain_source_from(theatre, source)) {
            RON_LOG_ERROR("Failed to adapt terrain source: run the Gate 06/08 bake");
            std::abort();
        }

        sampler.init(source.height, source.height_n);
        if (!quadtree.build(sampler)) {
            RON_LOG_ERROR("Failed to build terrain quadtree");
            std::abort();
        }

        initialized = true;
    }
};

SceneDataFixture& fixture() {
    static SceneDataFixture s_fix;
    s_fix.init();
    return s_fix;
}

} // namespace

const sim::TheatreData& test_theatre() {
    return fixture().theatre;
}

const assets::TextureData& test_tex26() {
    return fixture().tex26;
}

const TerrainSource& test_source() {
    return fixture().source;
}

const HeightSampler& test_sampler() {
    return fixture().sampler;
}

const TerrainQuadtree& test_quadtree() {
    return fixture().quadtree;
}

void SceneFixture::init() {
    if (initialized) return;

    gl_state_init();

    const char* s_dir = get_shaders_dir();
    std::string init_log;
    if (!shaders.init(disk_file_reader, const_cast<char*>(s_dir), init_log)) {
        RON_LOG_ERROR("SceneFixture::init: failed to load shaders: %s", init_log.c_str());
        std::abort();
    }

    if (!terrain.init(ledger, test_source(), test_tex26(), test_sampler(), test_quadtree())) {
        RON_LOG_ERROR("SceneFixture::init: failed to init terrain renderer");
        std::abort();
    }

    if (!frame_ubo.create(ledger, VramCat::Staging, "frame_ubo", 336, nullptr, GL_DYNAMIC_STORAGE_BIT)) {
        RON_LOG_ERROR("SceneFixture::init: failed to create frame UBO");
        std::abort();
    }

    initialized = true;
}

void SceneFixture::render_view(RenderTarget& rt,
                               const CameraRts& cam,
                               ProgramId terrain_program,
                               bool draw_ocean_flag,
                               const float clear_rgba[4],
                               float time) {
    init();

    float aspect = static_cast<float>(rt.width()) / static_cast<float>(rt.height());
    Mat4 view = camera_view(cam, &test_sampler());
    Mat4 proj = camera_proj(aspect);
    Mat4 view_proj = mat4_mul(proj, view);
    Vec3 eye = camera_eye(cam, &test_sampler());

    FrameUniforms frame{};
    fill_frame_uniforms(frame, view, proj, eye,
                        rt.width(), rt.height(),
                        time, 0.0f, 0.0f,
                        Vec3{0.30f, 0.70f, 0.65f}, 3.0f,
                        Vec3{1.0f, 0.95f, 0.85f});

    glNamedBufferSubData(frame_ubo.id(), 0, sizeof(FrameUniforms), &frame);
    glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_FRAME, frame_ubo.id());

    glDepthMask(GL_TRUE);
    rt.clear(clear_rgba);
    glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo());
    glViewport(0, 0, rt.width(), rt.height());

    Frustum frustum = frustum_from_view_proj(view_proj);
    terrain.frame_begin(eye, frustum);
    terrain.draw_terrain(shaders.program(terrain_program));
    if (draw_ocean_flag) {
        terrain.draw_ocean(shaders.program(ProgramId::Ocean));
    }
    terrain.frame_end();
}

SceneFixture& test_scene() {
    static SceneFixture s_scene;
    s_scene.init();
    return s_scene;
}

Vec3 find_flattest_point(const HeightSampler& sampler) {
    Vec3 flattest_pt(2048.0f, 0.0f, 2048.0f);
    float best_flatness = 1e9f;
    u32 tex_stride = static_cast<u32>(std::round(32.0f / sampler.cell_m()));
    u32 tex_step = static_cast<u32>(std::round(8.0f / sampler.cell_m()));

    for (u32 j = tex_step * 4; j < sampler.n() - tex_step * 4; j += tex_stride) {
        for (u32 i = tex_step * 4; i < sampler.n() - tex_step * 4; i += tex_stride) {
            float x = (static_cast<float>(i) + 0.5f) * sampler.cell_m();
            float z = (static_cast<float>(j) + 0.5f) * sampler.cell_m();
            float h_center = sampler.height_m(x, z);
            if (h_center <= 0.0f) continue;

            float h_e = sampler.height_m(x + 16.0f, z);
            float h_w = sampler.height_m(x - 16.0f, z);
            float h_s = sampler.height_m(x, z + 16.0f);
            float h_n = sampler.height_m(x, z - 16.0f);

            float max_diff = std::max({
                std::abs(h_e - h_center),
                std::abs(h_w - h_center),
                std::abs(h_s - h_center),
                std::abs(h_n - h_center)
            });

            if (max_diff < best_flatness) {
                best_flatness = max_diff;
                flattest_pt = Vec3(x, h_center, z);
            }
        }
    }
    return flattest_pt;
}

Vec3 find_highest_point(const HeightSampler& sampler, const TerrainSource& source) {
    u32 n = source.height_n;
    u16 max_code = 0;
    u32 max_i = 0, max_j = 0;
    for (u32 j = 0; j < n; ++j) {
        for (u32 i = 0; i < n; ++i) {
            u16 code = source.height[j * n + i];
            if (code > max_code) {
                max_code = code;
                max_i = i;
                max_j = j;
            }
        }
    }
    float x = (static_cast<float>(max_i) + 0.5f) * sampler.cell_m();
    float z = (static_cast<float>(max_j) + 0.5f) * sampler.cell_m();
    float y = sampler.height_m(x, z);
    return Vec3(x, y, z);
}

Vec3 find_max_ocean_depth_cell(const TerrainRenderer& renderer, u8& out_val) {
    const auto& depth_map = renderer.cpu_ocean_depth();
    u8 max_v = 0;
    u32 max_i = 0, max_j = 0;
    for (u32 j = 0; j < 512; ++j) {
        for (u32 i = 0; i < 512; ++i) {
            u8 v = depth_map[j * 512 + i];
            if (v > max_v) {
                max_v = v;
                max_i = i;
                max_j = j;
            }
        }
    }
    out_val = max_v;
    float cell_m = 4096.0f / 512.0f;
    float x = (static_cast<float>(max_i) + 0.5f) * cell_m;
    float z = (static_cast<float>(max_j) + 0.5f) * cell_m;
    return Vec3(x, 0.0f, z);
}

} // namespace ron::render::test
