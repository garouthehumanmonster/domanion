#include "render/gpu_preference.hpp"
#include "render/gl_context.hpp"
#include "render/gl_state.hpp"
#include "render/vram_ledger.hpp"
#include "render/shader_library.hpp"
#include "render/render_target.hpp"
#include "render/persistent_ring.hpp"
#include "render/frame_uniforms.hpp"
#include "render/frame_fill.hpp"
#include "render/gpu_timer.hpp"
#include "render/gl_bindings.hpp"
#include "render/camera_rts.hpp"
#include "render/height_sampler.hpp"
#include "render/terrain_quadtree.hpp"
#include "render/terrain_source.hpp"
#include "render/terrain_renderer.hpp"
#include "render/world_overlays.hpp"
#include "render/minimap_renderer.hpp"
#include "render/territory_map.hpp"
#include "render/overlay_types.hpp"
#include "render/minimap_cpu.hpp"
#include "render/entity_assets.hpp"
#include "render/entity_renderer.hpp"
#include "render/entity_crowd.hpp"
#include "sim/theatre_data.hpp"
#include "assets/texture_data.hpp"
#include "core/arena.hpp"
#include "core/assert_log.hpp"

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

RON_EXPORT_GPU_PREFERENCE()

using namespace ron;
using namespace ron::render;

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

static const char* get_shaders_dir() {
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

static const char* get_theatre_path() {
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

static const char* get_textures_path() {
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

static const char* get_models_path() {
    static const char* paths[] = {
        RON_SOURCE_DIR "/assets/compiled/models/models_2026.mdbin",
        "assets/compiled/models/models_2026.mdbin",
        "../assets/compiled/models/models_2026.mdbin",
        "../../assets/compiled/models/models_2026.mdbin"
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

int main(int argc, char** argv) {
    u64 frames_limit = 0;
    int vsync = 1;
    bool fullscreen = false;
    u32 width = 1600;
    u32 height = 900;
    std::string dump_path;
    bool dump_enabled = false;
    bool hidden = false;

    float focus_x = 2048.0f;
    float focus_z = 2048.0f;
    float init_dist = 400.0f;
    float init_yaw = 0.3f;
    bool init_lod_debug = false;

    bool overlays_enabled = true;
    bool fog_enabled = false;
    int craters_count = 6;
    bool minimap_enabled = true;
    bool territory_enabled = true;
    int crowd_count = 3000;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            frames_limit = static_cast<u64>(std::strtoull(argv[++i], nullptr, 10));
        } else if (arg == "--crowd" && i + 1 < argc) {
            crowd_count = std::clamp(std::atoi(argv[++i]), 0, 60000);
        } else if (arg == "--vsync" && i + 1 < argc) {
            vsync = std::atoi(argv[++i]);
        } else if (arg == "--fullscreen") {
            fullscreen = true;
        } else if (arg == "--size" && i + 1 < argc) {
            std::string size_str = argv[++i];
            size_t x_pos = size_str.find('x');
            if (x_pos != std::string::npos) {
                width = static_cast<u32>(std::strtoul(size_str.substr(0, x_pos).c_str(), nullptr, 10));
                height = static_cast<u32>(std::strtoul(size_str.substr(x_pos + 1).c_str(), nullptr, 10));
            }
        } else if (arg == "--dump") {
            dump_enabled = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                dump_path = argv[++i];
            } else {
                dump_path = "scene.ppm";
            }
        } else if (arg == "--hidden") {
            hidden = true;
        } else if (arg == "--focus" && i + 1 < argc) {
            std::string f_str = argv[++i];
            size_t comma = f_str.find(',');
            if (comma != std::string::npos) {
                focus_x = std::strtof(f_str.substr(0, comma).c_str(), nullptr);
                focus_z = std::strtof(f_str.substr(comma + 1).c_str(), nullptr);
            }
        } else if (arg == "--dist" && i + 1 < argc) {
            init_dist = std::strtof(argv[++i], nullptr);
        } else if (arg == "--yaw" && i + 1 < argc) {
            init_yaw = std::strtof(argv[++i], nullptr);
        } else if (arg == "--lod-debug") {
            init_lod_debug = true;
        } else if (arg == "--overlays" && i + 1 < argc) {
            overlays_enabled = (std::atoi(argv[++i]) != 0);
        } else if (arg == "--fog") {
            fog_enabled = true;
        } else if (arg == "--craters" && i + 1 < argc) {
            craters_count = std::atoi(argv[++i]);
        } else if (arg == "--no-minimap") {
            minimap_enabled = false;
        }
    }

    GlContextDesc desc;
    desc.width = static_cast<int>(width);
    desc.height = static_cast<int>(height);
    desc.title = "DOMINION 2026";
    desc.hidden = hidden;
    desc.fullscreen_borderless = fullscreen;
    desc.swap_interval = vsync;
#if defined(RON_DEBUG) && RON_DEBUG
    desc.debug = true;
#else
    desc.debug = false;
#endif

    GlContext ctx;
    if (!ctx.create(desc)) {
        RON_LOG_ERROR("Failed to initialize GlContext");
        return 2;
    }

    gl_state_init();

    VramLedger ledger;

    const char* shaders_dir = get_shaders_dir();
    ShaderLibrary shader_lib;
    std::string shader_log;
    if (!shader_lib.init(disk_file_reader, const_cast<char*>(shaders_dir), shader_log)) {
        RON_LOG_ERROR("Shader library init failed:\n%s", shader_log.c_str());
        return 3;
    }

    // Load Theatre & Tex26
    static MemoryArena arena_thtr;
    if (!arena_thtr.init(MiB(32), MiB(1), "view_thtr").is_ok()) {
        RON_LOG_ERROR("Failed to init arena_thtr");
        return 5;
    }
    sim::TheatreData theatre;
    const char* thtr_path = get_theatre_path();
    if (!theatre.load_file(thtr_path, arena_thtr).is_ok() || !theatre.is_valid()) {
        RON_LOG_ERROR("Failed to load theatre: %s", thtr_path);
        return 5;
    }

    static MemoryArena arena_tex;
    if (!arena_tex.init(MiB(96), MiB(1), "view_tex").is_ok()) {
        RON_LOG_ERROR("Failed to init arena_tex");
        return 5;
    }
    assets::TextureData tex26;
    const char* tex_path = get_textures_path();
    if (tex26.load_file(tex_path, arena_tex) != assets::TextureError::Ok || !tex26.is_valid()) {
        RON_LOG_ERROR("Failed to load textures: %s", tex_path);
        return 5;
    }

    TerrainSource source;
    if (!terrain_source_from(theatre, source)) {
        RON_LOG_ERROR("Failed to adapt terrain source");
        return 5;
    }

    HeightSampler sampler;
    sampler.init(source.height, source.height_n);

    TerrainQuadtree quadtree;
    if (!quadtree.build(sampler)) {
        RON_LOG_ERROR("Failed to build terrain quadtree");
        return 5;
    }

    TerrainRenderer terrain_renderer;
    if (!terrain_renderer.init(ledger, source, tex26, sampler, quadtree)) {
        RON_LOG_ERROR("Failed to init TerrainRenderer");
        return 5;
    }

    WorldOverlays overlays;
    if (!overlays.init(ledger, {}, &shader_lib)) {
        RON_LOG_ERROR("Failed to init WorldOverlays");
        return 5;
    }

    MinimapRenderer minimap;
    if (!minimap.init(ledger, overlays, &shader_lib)) {
        RON_LOG_ERROR("Failed to init MinimapRenderer");
        return 5;
    }
    minimap.bake_base(sampler);

    // Gate 12: EntityAssets & EntityRenderer
    EntityAssets entity_assets;
    EntityRenderer entity_renderer;
    std::vector<CrowdEntity> crowd;
    bool freeze_cull = false;

    if (crowd_count > 0) {
        if (!entity_assets.init(ledger, get_models_path(), get_textures_path())) {
            RON_LOG_ERROR("Failed to init EntityAssets");
            return 5;
        }
        if (!entity_renderer.init(ledger, entity_assets, &shader_lib)) {
            RON_LOG_ERROR("Failed to init EntityRenderer");
            return 5;
        }
        if (overlays_enabled) {
            entity_renderer.set_overlays(&overlays);
        }

        crowd.reserve(60000);
        init_crowd(crowd, crowd_count, entity_assets, sampler, focus_x, focus_z);
    }

    // Pick 8 land sites
    Site sites[8];
    int site_count = pick_land_sites(sampler, 8, 400.0f, sites);
    std::vector<TerritorySource> sources(site_count);
    std::vector<Blip> blips(site_count);
    for (int s = 0; s < site_count; ++s) {
        sources[s].x = sites[s].x;
        sources[s].z = sites[s].z;
        sources[s].radius = 380.0f;
        sources[s].owner = static_cast<u8>(s + 1);

        Vec4 c = overlay_palette(static_cast<u8>(s + 1));
        u32 r = static_cast<u32>(std::clamp(c.x * 255.0f, 0.0f, 255.0f));
        u32 g = static_cast<u32>(std::clamp(c.y * 255.0f, 0.0f, 255.0f));
        u32 b = static_cast<u32>(std::clamp(c.z * 255.0f, 0.0f, 255.0f));
        u32 a = static_cast<u32>(std::clamp(c.w * 255.0f, 0.0f, 255.0f));
        u32 rgba = (a << 24) | (b << 16) | (g << 8) | r;

        blips[s] = Blip{ sites[s].x, sites[s].z, rgba, 4.0f };
    }

    std::vector<u8> owner_map(512 * 512, 0);
    territory_rasterize(sources.data(), site_count, &sampler, owner_map.data());
    overlays.set_territory_owner(owner_map.data());

    // Deterministic craters near focus at start:
    // radius 14+4*(i%4), depth 3, scorch 1, mul 1.8, seed = 1000+i, positions on a ring of 120 m around focus
    for (int c_idx = 0; c_idx < craters_count; ++c_idx) {
        float angle = static_cast<float>(c_idx) * (2.0f * 3.14159265f / static_cast<float>(std::max(1, craters_count)));
        float cx = focus_x + 120.0f * std::cos(angle);
        float cz = focus_z + 120.0f * std::sin(angle);
        float cr = 14.0f + 4.0f * static_cast<float>(c_idx % 4);
        CraterSplat s{ cx, cz, cr, 3.0f, 1.0f, 1.8f, static_cast<u32>(1000 + c_idx), 0 };
        overlays.craters().enqueue(s);
    }

    if (!fog_enabled) {
        overlays.fog().reveal_all();
    }

    if (overlays_enabled) {
        terrain_renderer.set_overlays(&overlays);
    }

    RenderTarget hdr_target;
    if (!hdr_target.create(ledger, VramCat::HdrTargets, VramCat::DepthTargets, "scene_hdr", width, height, TexFmt::RGBA16F, true)) {
        RON_LOG_ERROR("Failed to create HDR RenderTarget");
        return 4;
    }

    GLint ssbo_align = 0;
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &ssbo_align);
    GLint ubo_align = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_align);

    PersistentRing frame_ring;
    if (!frame_ring.create(ledger, VramCat::Staging, "frame_uniforms_ring", sizeof(FrameUniforms), ssbo_align, ubo_align)) {
        RON_LOG_ERROR("Failed to create PersistentRing");
        return 4;
    }

    GpuTimer gpu_timer;
    if (!gpu_timer.create()) {
        RON_LOG_ERROR("Failed to create GpuTimer");
        return 2;
    }

    ledger.freeze();
    ledger.dump();

    static float s_frame_times[4096] = {0};
    static float s_gpu_times[4096] = {0};

    u64 frame_count = 0;
    u64 total_nodes = 0;
    u64 total_tris = 0;
    bool quit = false;
    Uint64 start_counter = SDL_GetPerformanceCounter();
    Uint64 perf_freq = SDL_GetPerformanceFrequency();
    Uint64 last_title_update = start_counter;
    Uint64 last_frame_time = start_counter;
    float last_frame_ms = 16.667f;

    CameraRts cam;
    camera_init(cam, focus_x, focus_z, init_yaw, init_dist);
    camera_snap(cam, &sampler);

    bool lod_debug = init_lod_debug;
    Mat4 last_inv_vp{};
    float sim_accumulator = 0.0f;
    float crowd_alpha = 1.0f;

    while (!quit) {
        CameraInput input{};

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                quit = true;
            } else if (ev.type == SDL_EVENT_KEY_DOWN) {
                if (ev.key.key == SDLK_ESCAPE) {
                    quit = true;
                } else if (ev.key.key == SDLK_F2) {
                    lod_debug = !lod_debug;
                } else if (ev.key.key == SDLK_F3) {
                    minimap_enabled = !minimap_enabled;
                } else if (ev.key.key == SDLK_F4) {
                    fog_enabled = !fog_enabled;
                } else if (ev.key.key == SDLK_F5) {
                    territory_enabled = !territory_enabled;
                } else if (ev.key.key == SDLK_F6) {
                    if (crowd_count > 0) {
                        auto st = entity_renderer.read_stats_blocking();
                        std::printf("stats: input=%u visible=%u frustum_culled=%u size_culled=%u lod0=%u lod1=%u lod2=%u draws=%d\n",
                                    st.input, st.visible, st.frustum_culled, st.size_culled,
                                    st.lod[0], st.lod[1], st.lod[2], st.draws);
                        std::fflush(stdout);
                    }
                } else if (ev.key.key == SDLK_F7) {
                    freeze_cull = !freeze_cull;
                    std::printf("[ron_view] freeze_cull: %s\n", freeze_cull ? "ON" : "OFF");
                    std::fflush(stdout);
                } else if (ev.key.key == SDLK_C) {
                    float mx = 0.0f, my = 0.0f;
                    SDL_GetMouseState(&mx, &my);
                    int cur_w = 0, cur_h = 0;
                    SDL_GetWindowSizeInPixels(ctx.window(), &cur_w, &cur_h);
                    float ndc_x = (mx + 0.5f) / static_cast<float>(cur_w) * 2.0f - 1.0f;
                    float ndc_y = 1.0f - (my + 0.5f) / static_cast<float>(cur_h) * 2.0f;
                    Vec3 ray_orig{}, ray_dir{}, hit{};
                    unproject_ray(last_inv_vp, ndc_x, ndc_y, ray_orig, ray_dir);
                    float sx = cam.x, sz = cam.z;
                    if (pick_terrain(sampler, ray_orig, ray_dir, 16000.0f, hit)) {
                        sx = hit.x; sz = hit.z;
                    }
                    CraterSplat s{ sx, sz, 18.0f, 3.5f, 1.0f, 1.8f, static_cast<u32>(frame_count), 0 };
                    overlays.craters().enqueue(s);
                } else if (ev.key.key == SDLK_N) {
                    float mx = 0.0f, my = 0.0f;
                    SDL_GetMouseState(&mx, &my);
                    int cur_w = 0, cur_h = 0;
                    SDL_GetWindowSizeInPixels(ctx.window(), &cur_w, &cur_h);
                    float ndc_x = (mx + 0.5f) / static_cast<float>(cur_w) * 2.0f - 1.0f;
                    float ndc_y = 1.0f - (my + 0.5f) / static_cast<float>(cur_h) * 2.0f;
                    Vec3 ray_orig{}, ray_dir{}, hit{};
                    unproject_ray(last_inv_vp, ndc_x, ndc_y, ray_orig, ray_dir);
                    float sx = cam.x, sz = cam.z;
                    if (pick_terrain(sampler, ray_orig, ray_dir, 16000.0f, hit)) {
                        sx = hit.x; sz = hit.z;
                    }
                    CraterSplat s{ sx, sz, 48.0f, 7.0f, 1.0f, 2.5f, static_cast<u32>(frame_count), 0 };
                    overlays.craters().enqueue(s);
                }
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    int cur_w = 0, cur_h = 0;
                    SDL_GetWindowSizeInPixels(ctx.window(), &cur_w, &cur_h);
                    PxRect mm_rect{ 8.0f, static_cast<float>(cur_h) - 8.0f - 256.0f, 256.0f, 256.0f };
                    float wx = 0.0f, wz = 0.0f;
                    if (minimap_enabled && minimap_pick(ev.button.x, ev.button.y, mm_rect, &wx, &wz)) {
                        cam.target_x = wx;
                        cam.target_z = wz;
                        cam.x = wx;
                        cam.z = wz;
                        camera_snap(cam, &sampler);
                    }
                }
            } else if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                input.zoom_steps += ev.wheel.y;
            }
        }

        if (frames_limit > 0 && frame_count >= frames_limit) {
            break;
        }

        // Read keyboard state
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys) {
            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    input.pan_z += 1.0f;
            if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  input.pan_z -= 1.0f;
            if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) input.pan_x += 1.0f;
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  input.pan_x -= 1.0f;
            if (keys[SDL_SCANCODE_Q])                             input.rotate -= 1.0f;
            if (keys[SDL_SCANCODE_E])                             input.rotate += 1.0f;
        }

        // Edge scroll
        float mouse_x = 0.0f, mouse_y = 0.0f;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        SDL_WindowFlags win_flags = SDL_GetWindowFlags(ctx.window());
        if ((win_flags & SDL_WINDOW_INPUT_FOCUS) != 0) {
            int win_w = 0, win_h = 0;
            SDL_GetWindowSize(ctx.window(), &win_w, &win_h);
            if (mouse_x <= 4.0f)              input.pan_x -= 1.0f;
            if (mouse_x >= win_w - 4.0f)      input.pan_x += 1.0f;
            if (mouse_y <= 4.0f)              input.pan_z += 1.0f;
            if (mouse_y >= win_h - 4.0f)      input.pan_z -= 1.0f;
        }

        float dt = std::clamp(last_frame_ms * 0.001f, 0.001f, 0.1f);
        camera_update(cam, input, dt, &sampler);

        if (crowd_count > 0) {
            update_crowd(crowd, sampler, focus_x, focus_z, dt, sim_accumulator, crowd_alpha);
        }

        Uint64 now_counter = SDL_GetPerformanceCounter();
        float time_sec = static_cast<float>(now_counter - start_counter) / static_cast<float>(perf_freq);

        // Update fog and overlays
        if (fog_enabled) {
            overlays.fog().begin_frame();
            overlays.fog().reveal_circle(cam.x, cam.z, 260.0f);
            for (int s = 0; s < site_count; ++s) {
                overlays.fog().reveal_circle(sites[s].x, sites[s].z, 150.0f);
            }
            overlays.fog().commit();
        }

        overlays.set_flags(territory_enabled && overlays_enabled,
                           fog_enabled && overlays_enabled,
                           overlays_enabled);
        overlays.update(dt, time_sec);

        // FRAME-SAFE BEGIN
        u8* ring_ptr = frame_ring.begin_frame();
        FrameUniforms* uniforms = reinterpret_cast<FrameUniforms*>(ring_ptr);

        float aspect = static_cast<float>(width) / static_cast<float>(height);
        Mat4 view = camera_view(cam, &sampler);
        Mat4 proj = camera_proj(aspect);
        Mat4 view_proj = mat4_mul(proj, view);
        mat4_inverse(view_proj, last_inv_vp);
        Vec3 eye = camera_eye(cam, &sampler);

        fill_frame_uniforms(*uniforms, view, proj, eye, width, height,
                            time_sec, 0.0f, crowd_alpha,
                            Vec3{0.30f, 0.70f, 0.65f}, 3.0f,
                            Vec3{1.0f, 0.95f, 0.85f});

        glBindBufferRange(GL_UNIFORM_BUFFER, BIND_UBO_FRAME, frame_ring.buffer(), static_cast<GLintptr>(frame_ring.slot_offset()), sizeof(FrameUniforms));

        gpu_timer.begin(0);

        Frustum frustum = frustum_from_view_proj(view_proj);
        terrain_renderer.frame_begin(eye, frustum);

        u32 cur_nodes = terrain_renderer.node_count();
        u32 cur_tris = terrain_renderer.triangle_count();
        total_nodes += cur_nodes;
        total_tris += cur_tris;

        // Clear HDR target to fog colour (0.50, 0.62, 0.80, 1) and depth 0.0
        const float fog_color[4] = { 0.50f, 0.62f, 0.80f, 1.0f };
        hdr_target.clear(fog_color);
        glClearDepth(0.0);
        glDepthMask(GL_TRUE);

        glBindFramebuffer(GL_FRAMEBUFFER, hdr_target.fbo());
        glViewport(0, 0, width, height);

        if (crowd_count > 0) {
            entity_renderer.begin_frame(static_cast<int>(frame_count));
            entity_renderer.feed().clear();
            for (const auto& ent : crowd) {
                entity_renderer.feed().add(ent.inst);
            }
            entity_renderer.upload();
            entity_renderer.cull(*uniforms, crowd_alpha, time_sec, freeze_cull);
        }

        u32 prog_id = lod_debug ? shader_lib.program(ProgramId::TerrainDebugLod) :
                      (overlays_enabled ? shader_lib.program(ProgramId::TerrainOvl) : shader_lib.program(ProgramId::Terrain));
        terrain_renderer.draw_terrain(prog_id);

        if (crowd_count > 0) {
            entity_renderer.draw(*uniforms);
        }

        terrain_renderer.draw_ocean(overlays_enabled ? shader_lib.program(ProgramId::OceanOvl) : shader_lib.program(ProgramId::Ocean));

        terrain_renderer.frame_end();

        // Tonemap into default framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        int win_w = 0, win_h = 0;
        SDL_GetWindowSizeInPixels(ctx.window(), &win_w, &win_h);
        glViewport(0, 0, win_w, win_h);
        glBindTextureUnit(TEX_HDR, hdr_target.color_tex());
        glUseProgram(shader_lib.program(ProgramId::Tonemap));
        glUniform1f(0, 1.25f); // exposure = 1.25
        draw_fullscreen_triangle();

        // Minimap pass
        if (minimap_enabled) {
            float quad_xz[8];
            float cam_pos[3] = { eye.x, eye.y, eye.z };
            camera_ground_quad(last_inv_vp.m, cam_pos, sampler, quad_xz);

            MinimapRenderer::Scene sc{};
            for (int q = 0; q < 8; ++q) sc.quad_xz[q] = quad_xz[q];
            sc.blips = blips.data();
            sc.blip_count = static_cast<int>(blips.size());
            sc.fog_on = fog_enabled && overlays_enabled;
            minimap.render(sc, time_sec);

            PxRect mm_rect{ 8.0f, static_cast<float>(win_h) - 8.0f - 256.0f, 256.0f, 256.0f };
            minimap.blit(mm_rect, win_w, win_h);
        }

        gpu_timer.end(0);

        bool is_last_frame = (frames_limit > 0 && frame_count + 1 >= frames_limit) || quit;
        if (is_last_frame && dump_enabled) {
            glReadBuffer(GL_BACK);
            std::vector<u8> pixels(static_cast<size_t>(win_w) * static_cast<size_t>(win_h) * 3);
            glReadPixels(0, 0, win_w, win_h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

            FILE* fp = fopen(dump_path.c_str(), "wb");
            if (fp) {
                fprintf(fp, "P6\n%d %d\n255\n", win_w, win_h);
                // Flip rows to top-first
                for (int y = win_h - 1; y >= 0; --y) {
                    fwrite(&pixels[static_cast<size_t>(y) * static_cast<size_t>(win_w) * 3], 1, static_cast<size_t>(win_w) * 3, fp);
                }
                fclose(fp);
            }

            // Also dump minimap.ppm (512x512)
            std::vector<u8> mm_pixels(512 * 512 * 4);
            glGetTextureImage(minimap.target_tex(), 0, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(mm_pixels.size()), mm_pixels.data());

            std::string mm_path = "minimap.ppm";
            size_t slash_pos = dump_path.find_last_of("/\\");
            if (slash_pos != std::string::npos) {
                mm_path = dump_path.substr(0, slash_pos + 1) + "minimap.ppm";
            }
            FILE* fp_mm = fopen(mm_path.c_str(), "wb");
            if (fp_mm) {
                fprintf(fp_mm, "P6\n512 512\n255\n");
                for (int y = 0; y < 512; ++y) {
                    for (int x = 0; x < 512; ++x) {
                        int idx = (y * 512 + x) * 4;
                        fwrite(&mm_pixels[idx], 1, 3, fp_mm);
                    }
                }
                fclose(fp_mm);
            }
            if (mm_path != "minimap.ppm") {
                FILE* fp_root = fopen("minimap.ppm", "wb");
                if (fp_root) {
                    fprintf(fp_root, "P6\n512 512\n255\n");
                    for (int y = 0; y < 512; ++y) {
                        for (int x = 0; x < 512; ++x) {
                            int idx = (y * 512 + x) * 4;
                            fwrite(&mm_pixels[idx], 1, 3, fp_root);
                        }
                    }
                    fclose(fp_root);
                }
            }
        }

        frame_ring.end_frame();
        ctx.swap();
        // FRAME-SAFE END

        Uint64 frame_end_counter = SDL_GetPerformanceCounter();
        float frame_ms = static_cast<float>(frame_end_counter - last_frame_time) * 1000.0f / static_cast<float>(perf_freq);
        last_frame_time = frame_end_counter;
        last_frame_ms = frame_ms;

        u32 rec_idx = static_cast<u32>(frame_count % 4096);
        s_frame_times[rec_idx] = frame_ms;
        float gpu_ms = gpu_timer.elapsed_ms(0);
        s_gpu_times[rec_idx] = gpu_ms;

        frame_count++;

        if (frame_end_counter - last_title_update >= perf_freq / 2) {
            float fps = (frame_ms > 0.001f) ? (1000.0f / frame_ms) : 0.0f;
            char title_buf[160];
            std::snprintf(title_buf, sizeof(title_buf),
                          "DOMINION 2026 | %.1f fps | cpu %.2f ms | gpu %.2f ms | nodes %u tris %u",
                          fps, frame_ms, gpu_ms, cur_nodes, cur_tris);
            SDL_SetWindowTitle(ctx.window(), title_buf);
            last_title_update = frame_end_counter;
        }
    }

    // On exit print: frames, avg fps, avg/p99 frame ms, avg GPU ms, and the ledger summary
    u32 recorded = static_cast<u32>(std::min<u64>(frame_count, 4096));
    float sum_frame_ms = 0.0f;
    float sum_gpu_ms = 0.0f;
    std::vector<float> sorted_frame_times;
    sorted_frame_times.reserve(recorded);

    for (u32 i = 0; i < recorded; ++i) {
        sum_frame_ms += s_frame_times[i];
        sum_gpu_ms += s_gpu_times[i];
        sorted_frame_times.push_back(s_frame_times[i]);
    }

    float avg_frame_ms = (recorded > 0) ? (sum_frame_ms / recorded) : 0.0f;
    float avg_gpu_ms = (recorded > 0) ? (sum_gpu_ms / recorded) : 0.0f;
    float avg_fps = (avg_frame_ms > 0.001f) ? (1000.0f / avg_frame_ms) : 0.0f;

    std::sort(sorted_frame_times.begin(), sorted_frame_times.end());
    size_t p99_idx = static_cast<size_t>(static_cast<double>(recorded) * 0.99);
    if (p99_idx >= recorded && recorded > 0) p99_idx = recorded - 1;
    float p99_frame_ms = (recorded > 0) ? sorted_frame_times[p99_idx] : 0.0f;

    u64 avg_nodes = (frame_count > 0) ? (total_nodes / frame_count) : 0;
    u64 avg_tris = (frame_count > 0) ? (total_tris / frame_count) : 0;

    std::printf("\n========================================\n");
    std::printf("ron_view completed:\n");
    std::printf("overlays: territory=%d fog=%d craters_applied=%u dropped=%u\n",
                (territory_enabled && overlays_enabled) ? 1 : 0,
                (fog_enabled && overlays_enabled) ? 1 : 0,
                overlays.craters().applied_total(),
                overlays.craters().dropped());
    if (crowd_count > 0) {
        auto st = entity_renderer.read_stats_blocking();
        std::printf("entities: input=%u visible=%u frustum_culled=%u size_culled=%u lod0=%u lod1=%u lod2=%u draws=%d dropped=%u\n",
                    st.input, st.visible, st.frustum_culled, st.size_culled,
                    st.lod[0], st.lod[1], st.lod[2], st.draws, entity_renderer.feed().dropped());
    }
    std::printf("Frames: %llu | Avg FPS: %.1f | Avg frame: %.2f ms | P99 frame: %.2f ms | Avg GPU: %.2f ms\n",
                static_cast<unsigned long long>(frame_count), avg_fps, avg_frame_ms, p99_frame_ms, avg_gpu_ms);
    std::printf("Avg nodes: %llu | Avg tris: %llu\n",
                static_cast<unsigned long long>(avg_nodes), static_cast<unsigned long long>(avg_tris));
    std::printf("========================================\n");
    std::fflush(stdout);

    ledger.dump();
    std::fflush(stdout);
    std::fflush(stderr);

    return 0;
}
