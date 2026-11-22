#include "render/terrain_renderer.hpp"
#include "render/terrain_materials.hpp"
#include "render/lighting_system.hpp"
#include "render/shader_library.hpp"
#include "render/gl_bindings.hpp"
#include "core/assert_log.hpp"
#include <cstring>
#include <algorithm>

namespace ron::render {

bool TerrainRenderer::init(VramLedger& ledger,
                           const TerrainSource& source,
                           const assets::TextureData& textures,
                           const HeightSampler& sampler,
                           const TerrainQuadtree& quadtree) noexcept {
    destroy();
    quadtree_ = &quadtree;

    if (!source.height || source.height_n == 0 || !source.biome || source.biome_w == 0 || source.biome_h == 0) {
        RON_LOG_ERROR("TerrainRenderer::init: invalid TerrainSource");
        return false;
    }

    // 1. Height texture (R16, mips=1, linear, clamp)
    if (!height_tex_.create_2d(ledger, VramCat::TerrainMaps, "terrain_height",
                               TexFmt::R16, source.height_n, source.height_n, 1)) {
        RON_LOG_ERROR("TerrainRenderer::init: failed to create height texture");
        destroy();
        return false;
    }
    height_tex_.upload_level(0, 0, source.height, static_cast<u64>(source.height_n) * source.height_n * 2);
    glTextureParameteri(height_tex_.id(), GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(height_tex_.id(), GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(height_tex_.id(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(height_tex_.id(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 2. Biome texture (R8, mips=1, nearest, clamp)
    if (!biome_tex_.create_2d(ledger, VramCat::TerrainMaps, "terrain_biome",
                              TexFmt::R8, source.biome_w, source.biome_h, 1)) {
        RON_LOG_ERROR("TerrainRenderer::init: failed to create biome texture");
        destroy();
        return false;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    biome_tex_.upload_level(0, 0, source.biome, static_cast<u64>(source.biome_w) * source.biome_h);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTextureParameteri(biome_tex_.id(), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(biome_tex_.id(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(biome_tex_.id(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(biome_tex_.id(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 3. Terrain texture arrays (8 layers, 10 mips, 512x512)
    // Albedo (BC1_SRGB, tex_id 4)
    if (!albedo_arr_.create_2d_array(ledger, VramCat::TerrainTextures, "terrain_albedo",
                                     TexFmt::BC1_SRGB, 512, 512, 8, 10)) {
        RON_LOG_ERROR("TerrainRenderer::init: failed to create albedo array");
        destroy();
        return false;
    }
    for (u32 layer = 0; layer < 8; ++layer) {
        for (u32 level = 0; level < 10; ++level) {
            TerrainTexLevel lvl;
            if (terrain_tex_level(textures, 4, level, layer, lvl)) {
                albedo_arr_.upload_level(level, layer, lvl.data, lvl.bytes);
            }
        }
    }
    glTextureParameteri(albedo_arr_.id(), GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(albedo_arr_.id(), GL_TEXTURE_WRAP_T, GL_REPEAT);

    // Normal (BC5, tex_id 5)
    if (!normal_arr_.create_2d_array(ledger, VramCat::TerrainTextures, "terrain_normal",
                                     TexFmt::BC5, 512, 512, 8, 10)) {
        RON_LOG_ERROR("TerrainRenderer::init: failed to create normal array");
        destroy();
        return false;
    }
    for (u32 layer = 0; layer < 8; ++layer) {
        for (u32 level = 0; level < 10; ++level) {
            TerrainTexLevel lvl;
            if (terrain_tex_level(textures, 5, level, layer, lvl)) {
                normal_arr_.upload_level(level, layer, lvl.data, lvl.bytes);
            }
        }
    }
    glTextureParameteri(normal_arr_.id(), GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(normal_arr_.id(), GL_TEXTURE_WRAP_T, GL_REPEAT);

    // RH (BC1, tex_id 6)
    if (!rh_arr_.create_2d_array(ledger, VramCat::TerrainTextures, "terrain_rh",
                                 TexFmt::BC1, 512, 512, 8, 10)) {
        RON_LOG_ERROR("TerrainRenderer::init: failed to create rh array");
        destroy();
        return false;
    }
    for (u32 layer = 0; layer < 8; ++layer) {
        for (u32 level = 0; level < 10; ++level) {
            TerrainTexLevel lvl;
            if (terrain_tex_level(textures, 6, level, layer, lvl)) {
                rh_arr_.upload_level(level, layer, lvl.data, lvl.bytes);
            }
        }
    }
    glTextureParameteri(rh_arr_.id(), GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(rh_arr_.id(), GL_TEXTURE_WRAP_T, GL_REPEAT);

    // 4. Ocean depth map (R8 512x512, linear, clamp) in VramCat::Ocean
    cpu_ocean_depth_.resize(512 * 512);
    ocean_depth_map_build(sampler, cpu_ocean_depth_.data());
    if (!ocean_depth_tex_.create_2d(ledger, VramCat::Ocean, "ocean_depth",
                                    TexFmt::R8, 512, 512, 1)) {
        RON_LOG_ERROR("TerrainRenderer::init: failed to create ocean depth texture");
        destroy();
        return false;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    ocean_depth_tex_.upload_level(0, 0, cpu_ocean_depth_.data(), 512 * 512);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTextureParameteri(ocean_depth_tex_.id(), GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(ocean_depth_tex_.id(), GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(ocean_depth_tex_.id(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(ocean_depth_tex_.id(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 5. Node PersistentRing in VramCat::TerrainMaps
    GLint ssbo_align = 256;
    GLint ubo_align = 256;
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &ssbo_align);
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_align);
    if (!ring_.create(ledger, VramCat::TerrainMaps, "terrain_nodes",
                      2048 * sizeof(TerrainNodeGpu),
                      static_cast<u32>(ssbo_align), static_cast<u32>(ubo_align))) {
        RON_LOG_ERROR("TerrainRenderer::init: failed to create node ring");
        destroy();
        return false;
    }

    // 6. TerrainUbo in VramCat::TerrainMaps
    TerrainUbo ubo_data{};
    for (u32 l = 0; l < 8; ++l) {
        ubo_data.lod_range[l] = terrain_lod_range(l);
    }
    ubo_data.skirt_depth = SKIRT_DEPTH_M;
    ubo_data.rock_layer = static_cast<float>(ROCK_LAYER);
    ubo_data.tex_n = static_cast<float>(source.height_n);
    ubo_data.inv_n = 1.0f / static_cast<float>(source.height_n);
    ubo_data.biome_w = static_cast<float>(source.biome_w);
    ubo_data.biome_h = static_cast<float>(source.biome_h);
    ubo_data.ocean_depth_max = 120.0f;
    ubo_data.pad = 0.0f;
    for (u32 b = 0; b < 32; ++b) {
        ubo_data.biome_layer[b] = static_cast<u32>(BIOME_TO_LAYER[b]);
    }

    if (!ubo_.create(ledger, VramCat::TerrainMaps, "terrain_ubo",
                     sizeof(TerrainUbo), &ubo_data, 0)) {
        RON_LOG_ERROR("TerrainRenderer::init: failed to create TerrainUbo");
        destroy();
        return false;
    }

    // 7. Index buffer in VramCat::TerrainMaps
    u16 indices[TERRAIN_INDEX_COUNT];
    terrain_build_indices(indices, TERRAIN_INDEX_COUNT);
    if (!ibo_.create(ledger, VramCat::TerrainMaps, "terrain_ibo",
                     sizeof(indices), indices, 0)) {
        RON_LOG_ERROR("TerrainRenderer::init: failed to create terrain ibo");
        destroy();
        return false;
    }

    // 8. VAO
    glCreateVertexArrays(1, &vao_);
    glVertexArrayElementBuffer(vao_, ibo_.id());

    return true;
}

void TerrainRenderer::frame_begin(const Vec3& cam_pos, const Frustum& frustum) noexcept {
    if (!quadtree_) return;
    u8* slot_bytes = ring_.begin_frame();
    TerrainNodeGpu* slot_nodes = reinterpret_cast<TerrainNodeGpu*>(slot_bytes);

    SelectResult res = cdlod_select(*quadtree_, &frustum, cam_pos, slot_nodes, TERRAIN_MAX_SELECTED);
    node_count_ = res.count;
    overflowed_ = res.overflow;
}

void TerrainRenderer::bind_lit_pass(u32 height_tex_id) const noexcept {
    if (lighting_) {
        lighting_->bind(height_tex_id != 0 ? height_tex_id : height_tex_.id());
    }
}

void TerrainRenderer::draw_terrain(u32 program) noexcept {
    if (!vao_ || node_count_ == 0) return;

    const ShaderLibrary* sl = shaders_ ? shaders_ : (ovl_ ? ovl_->shaders() : nullptr);

    if (ovl_) {
        ovl_->bind_world_pass();
        if (sl) {
            program = lighting_ ? sl->program(ProgramId::TerrainLitOvl)
                                : sl->program(ProgramId::TerrainOvl);
        }
    } else {
        if (sl && (program == 0 || program == sl->program(ProgramId::Terrain))) {
            program = lighting_ ? sl->program(ProgramId::TerrainLit)
                                : sl->program(ProgramId::Terrain);
        }
    }

    if (lighting_) {
        lighting_->bind(height_tex_.id());
    }

    glUseProgram(program);
    glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_TERRAIN, ubo_.id());
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_TERRAIN_NODES,
                      ring_.buffer(), ring_.slot_offset(),
                      2048 * sizeof(TerrainNodeGpu));

    glBindTextureUnit(TEX_TERRAIN_HEIGHT, height_tex_.id());
    glBindTextureUnit(TEX_TERRAIN_BIOME, biome_tex_.id());
    glBindTextureUnit(TEX_TERRAIN_ALBEDO, albedo_arr_.id());
    glBindTextureUnit(TEX_TERRAIN_NORMAL, normal_arr_.id());
    glBindTextureUnit(TEX_TERRAIN_RH, rh_arr_.id());

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
    glDepthMask(GL_TRUE);

    glBindVertexArray(vao_);
    glDrawElementsInstanced(GL_TRIANGLES, 1944, GL_UNSIGNED_SHORT, 0, node_count_);
}

void TerrainRenderer::draw_ocean(u32 program) noexcept {
    if (!vao_) return;

    if (ovl_) {
        ovl_->bind_world_pass();
        if (ovl_->shaders()) {
            program = ovl_->shaders()->program(ProgramId::OceanOvl);
        }
    }

    glUseProgram(program);
    glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_TERRAIN, ubo_.id());
    glBindTextureUnit(TEX_TERRAIN_HEIGHT, height_tex_.id());
    glBindTextureUnit(TEX_OCEAN_DEPTH, ocean_depth_tex_.id());

    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

void TerrainRenderer::frame_end() noexcept {
    ring_.end_frame();
}

void TerrainRenderer::destroy() noexcept {
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    ibo_.destroy();
    ubo_.destroy();
    ring_.destroy();
    ocean_depth_tex_.destroy();
    rh_arr_.destroy();
    normal_arr_.destroy();
    albedo_arr_.destroy();
    biome_tex_.destroy();
    height_tex_.destroy();
    cpu_ocean_depth_.clear();
    quadtree_ = nullptr;
    node_count_ = 0;
    overflowed_ = false;
}

} // namespace ron::render
