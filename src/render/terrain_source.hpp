#pragma once

#include "core/types.hpp"

namespace ron::sim {
class TheatreData;
}

namespace ron::assets {
class TextureData;
}

namespace ron::render {

struct TerrainSource {
    const u16* height{nullptr};
    u32 height_n{0};
    const u8* biome{nullptr};
    u32 biome_w{0};
    u32 biome_h{0};
};

bool terrain_source_from(const sim::TheatreData& theatre, TerrainSource& out);

struct TerrainTexLevel {
    const void* data{nullptr};
    u64 bytes{0};
};

bool terrain_tex_level(const assets::TextureData& tex, u32 tex_id, u32 level, u32 layer, TerrainTexLevel& out);

} // namespace ron::render
