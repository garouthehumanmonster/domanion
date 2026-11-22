#pragma once

#include "core/types.hpp"
#include "render/gl_objects.hpp"
#include "render/persistent_ring.hpp"
#include "render/rmath.hpp"
#include "render/terrain_quadtree.hpp"
#include "render/terrain_source.hpp"
#include "assets/texture_data.hpp"
#include "render/height_sampler.hpp"
#include "render/world_overlays.hpp"
#include <vector>

namespace ron::render {

class LightingSystem;
class ShaderLibrary;

// TerrainUbo (std140, binding BIND_UBO_TERRAIN), C++ struct must be exactly 192 bytes:
// float lod_range[8];                         // as vec4 u_lod_range[2]
// float skirt_depth, rock_layer, tex_n, inv_n; // vec4 u_tparams0
// float biome_w, biome_h, ocean_depth_max, pad;// vec4 u_tparams1   (ocean_depth_max = 120)
// u32   biome_layer[32];                      // uvec4 u_biome_layer[8], id -> [id>>2][id&3]
struct alignas(16) TerrainUbo {
    float lod_range[8];
    float skirt_depth;
    float rock_layer;
    float tex_n;
    float inv_n;
    float biome_w;
    float biome_h;
    float ocean_depth_max;
    float pad;
    u32   biome_layer[32];
};
static_assert(sizeof(TerrainUbo) == 192, "TerrainUbo must be exactly 192 bytes");

class TerrainRenderer {
public:
    TerrainRenderer() noexcept = default;
    ~TerrainRenderer() noexcept { destroy(); }

    TerrainRenderer(const TerrainRenderer&) = delete;
    TerrainRenderer& operator=(const TerrainRenderer&) = delete;

    bool init(VramLedger& ledger,
              const TerrainSource& source,
              const assets::TextureData& textures,
              const HeightSampler& sampler,
              const TerrainQuadtree& quadtree) noexcept;

    void frame_begin(const Vec3& cam_pos, const Frustum& frustum) noexcept;
    void draw_terrain(u32 program) noexcept;
    void draw_ocean(u32 program) noexcept;
    void frame_end() noexcept;

    void set_overlays(WorldOverlays* ovl) noexcept { ovl_ = ovl; }
    WorldOverlays* overlays() const noexcept { return ovl_; }

    void set_lighting(const LightingSystem* lighting) noexcept { lighting_ = lighting; }
    const LightingSystem* lighting() const noexcept { return lighting_; }

    void set_shaders(const ShaderLibrary* shaders) noexcept { shaders_ = shaders; }
    const ShaderLibrary* shaders() const noexcept { return shaders_; }

    void bind_lit_pass(u32 height_tex_id = 0) const noexcept;

    u32 node_count() const noexcept { return node_count_; }
    u32 triangle_count() const noexcept { return node_count_ * 648; }
    bool overflowed() const noexcept { return overflowed_; }
    void destroy() noexcept;

    const GpuTexture& height_tex() const noexcept { return height_tex_; }
    const GpuTexture& biome_tex() const noexcept { return biome_tex_; }
    const GpuTexture& albedo_arr() const noexcept { return albedo_arr_; }
    const GpuTexture& normal_arr() const noexcept { return normal_arr_; }
    const GpuTexture& rh_arr() const noexcept { return rh_arr_; }
    const GpuTexture& ocean_depth_tex() const noexcept { return ocean_depth_tex_; }
    const GpuBuffer& ubo() const noexcept { return ubo_; }
    const GpuBuffer& ibo() const noexcept { return ibo_; }
    u32 vao() const noexcept { return vao_; }

    const std::vector<u8>& cpu_ocean_depth() const noexcept { return cpu_ocean_depth_; }

private:
    const TerrainQuadtree* quadtree_{nullptr};
    WorldOverlays* ovl_{nullptr};
    const LightingSystem* lighting_{nullptr};
    const ShaderLibrary* shaders_{nullptr};

    GpuTexture height_tex_;
    GpuTexture biome_tex_;
    GpuTexture albedo_arr_;
    GpuTexture normal_arr_;
    GpuTexture rh_arr_;
    GpuTexture ocean_depth_tex_;
    GpuBuffer ubo_;
    GpuBuffer ibo_;
    PersistentRing ring_;
    u32 vao_{0};
    u32 node_count_{0};
    bool overflowed_{false};
    std::vector<u8> cpu_ocean_depth_;
};

} // namespace ron::render
