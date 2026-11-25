#pragma once

#include "assets/theatre_data.hpp"
#include "assets/texture_data.hpp"
#include "render/terrain_source.hpp"
#include "render/height_sampler.hpp"
#include "render/terrain_quadtree.hpp"
#include "render/vram_ledger.hpp"
#include "render/shader_library.hpp"
#include "render/terrain_renderer.hpp"
#include "render/render_target.hpp"
#include "render/camera_rts.hpp"

namespace ron::render::test {

const assets::TheatreData& test_theatre();
const assets::TextureData& test_tex26();
const TerrainSource& test_source();
const HeightSampler& test_sampler();
const TerrainQuadtree& test_quadtree();

struct SceneFixture {
    VramLedger ledger;
    ShaderLibrary shaders;
    TerrainRenderer terrain;
    GpuBuffer frame_ubo;
    bool initialized{false};

    void init();
    void render_view(RenderTarget& rt,
                     const CameraRts& cam,
                     ProgramId terrain_program,
                     bool draw_ocean_flag,
                     const float clear_rgba[4],
                     float time = 1.0f);
};

SceneFixture& test_scene();

Vec3 find_flattest_point(const HeightSampler& sampler);
Vec3 find_highest_point(const HeightSampler& sampler, const TerrainSource& source);
Vec3 find_max_ocean_depth_cell(const TerrainRenderer& renderer, u8& out_val);

} // namespace ron::render::test
