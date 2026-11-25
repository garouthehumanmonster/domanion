#include "render/terrain_source.hpp"
#include "assets/theatre_data.hpp"
#include "assets/texture_data.hpp"
#include <vector>
#include <cstring>
#include <algorithm>

namespace ron::render {

static std::vector<u16> s_aligned_height;

bool terrain_source_from(const assets::TheatreData& theatre, TerrainSource& out) {
    if (!theatre.is_valid()) return false;
    auto h_grid = theatre.height_grid();
    u32 n = theatre.height_n();
    if (h_grid.size() != static_cast<size_t>(n) * n) {
        return false;
    }
    auto b_grid = theatre.biome_grid();
    u32 bw = theatre.logic_n();
    u32 bh = theatre.logic_n();
    if (b_grid.size() != static_cast<size_t>(bw) * bh) {
        return false;
    }

    const u16* h_ptr = h_grid.data();
    if (reinterpret_cast<uintptr_t>(h_ptr) % 2 != 0) {
        s_aligned_height.assign(h_grid.begin(), h_grid.end());
        h_ptr = s_aligned_height.data();
    }

    out.height = h_ptr;
    out.height_n = n;
    out.biome = b_grid.data();
    out.biome_w = bw;
    out.biome_h = bh;
    return true;
}

bool terrain_tex_level(const assets::TextureData& tex, u32 tex_id, u32 level, u32 layer, TerrainTexLevel& out) {
    if (!tex.is_valid() || tex_id >= tex.texture_count()) return false;
    const auto& rec = tex.texture(tex_id);
    if (level >= rec.mips || layer >= rec.layers) return false;

    const u8* data = tex.level_data(tex_id, level, layer);
    if (!data) return false;

    u32 mip_w = std::max(1u, static_cast<u32>(rec.width >> level));
    u32 mip_h = std::max(1u, static_cast<u32>(rec.height >> level));
    usize sz = assets::TextureData::level_size(rec.format, mip_w, mip_h);

    out.data = data;
    out.bytes = static_cast<u64>(sz);
    return true;
}

} // namespace ron::render
