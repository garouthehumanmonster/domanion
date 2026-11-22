#pragma once

#include "core/types.hpp"
#include "render/rmath.hpp"
#include "render/height_sampler.hpp"
#include <vector>
#include <algorithm>

namespace ron::render {

constexpr u32 TERRAIN_LEVELS = 8;
constexpr u32 TERRAIN_ROOT_LEVEL = 7;
constexpr u32 TERRAIN_MAX_SELECTED = 2048;
constexpr u32 TERRAIN_GRID_QUADS = 16;
constexpr u32 TERRAIN_GRID_VERTS = 19;
constexpr u32 TERRAIN_INDEX_COUNT = 1944;
constexpr u32 TERRAIN_VERTEX_COUNT = 361;
constexpr float TERRAIN_LEAF_M = 32.0f;
constexpr float SKIRT_DEPTH_M = 10.0f;

struct TerrainNodeGpu {
    float x;
    float z;
    float size;
    float level;
};
static_assert(sizeof(TerrainNodeGpu) == 16);

inline float terrain_lod_range(u32 level) {
    if (level >= 7) return 1.0e9f;
    return 96.0f * static_cast<float>(1u << level);
}

inline float aabb_dist_sq(Vec3 p, Vec3 mn, Vec3 mx) {
    float dx = 0.0f;
    if (p.x < mn.x) dx = mn.x - p.x;
    else if (p.x > mx.x) dx = p.x - mx.x;

    float dy = 0.0f;
    if (p.y < mn.y) dy = mn.y - p.y;
    else if (p.y > mx.y) dy = p.y - mx.y;

    float dz = 0.0f;
    if (p.z < mn.z) dz = mn.z - p.z;
    else if (p.z > mx.z) dz = p.z - mx.z;

    return dx * dx + dy * dy + dz * dz;
}

struct NodeBounds {
    float minh{-128.0f};
    float maxh{512.0f};
};

class TerrainQuadtree {
public:
    TerrainQuadtree();

    bool build(const HeightSampler& sampler);
    u32 dim(u32 level) const { return (level < TERRAIN_LEVELS) ? (128u >> level) : 0u; }
    void bounds(u32 level, u32 ix, u32 iz, float& minh, float& maxh) const;

    static u32 level_offset(u32 level) {
        u32 off = 0;
        for (u32 l = 0; l < level && l < TERRAIN_LEVELS; ++l) {
            u32 d = 128u >> l;
            off += d * d;
        }
        return off;
    }

private:
    std::vector<NodeBounds> nodes_;
};

struct SelectResult {
    u32 count{0};
    bool overflow{false};
};

SelectResult cdlod_select(
    const TerrainQuadtree& qt,
    const Frustum* frustum_or_null,
    Vec3 cam,
    TerrainNodeGpu* out,
    u32 capacity
);

u32 terrain_build_indices(u16* out, u32 capacity);

void ocean_depth_map_build(const HeightSampler& sampler, u8* out /* 512 * 512 */);

} // namespace ron::render
