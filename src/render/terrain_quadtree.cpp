#include "render/terrain_quadtree.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace ron::render {

TerrainQuadtree::TerrainQuadtree() {
    nodes_.resize(21845);
}

bool TerrainQuadtree::build(const HeightSampler& sampler) {
    if (sampler.n() == 0) return false;
    nodes_.resize(21845);

    u32 d0 = 128;
    float cell = sampler.cell_m();

    for (u32 iz = 0; iz < d0; ++iz) {
        for (u32 ix = 0; ix < d0; ++ix) {
            float x0 = static_cast<float>(ix) * 32.0f;
            float z0 = static_cast<float>(iz) * 32.0f;

            float u0 = x0 / cell - 0.5f;
            float u1 = (x0 + 32.0f) / cell - 0.5f;
            i32 i0 = static_cast<i32>(std::floor(u0)) - 1;
            i32 i1 = static_cast<i32>(std::ceil(u1)) + 1;

            float v0 = z0 / cell - 0.5f;
            float v1 = (z0 + 32.0f) / cell - 0.5f;
            i32 j0 = static_cast<i32>(std::floor(v0)) - 1;
            i32 j1 = static_cast<i32>(std::ceil(v1)) + 1;

            i0 = std::clamp(i0, 0, static_cast<i32>(sampler.n() - 1));
            i1 = std::clamp(i1, 0, static_cast<i32>(sampler.n() - 1));
            j0 = std::clamp(j0, 0, static_cast<i32>(sampler.n() - 1));
            j1 = std::clamp(j1, 0, static_cast<i32>(sampler.n() - 1));

            float minh = 1e9f;
            float maxh = -1e9f;
            for (i32 j = j0; j <= j1; ++j) {
                for (i32 i = i0; i <= i1; ++i) {
                    float h = height_code_to_m(sampler.code(i, j));
                    if (h < minh) minh = h;
                    if (h > maxh) maxh = h;
                }
            }
            nodes_[iz * d0 + ix] = NodeBounds{ minh, maxh };
        }
    }

    for (u32 L = 1; L <= 7; ++L) {
        u32 d = 128u >> L;
        u32 off = level_offset(L);
        u32 prev_off = level_offset(L - 1);
        u32 prev_d = 128u >> (L - 1);

        for (u32 iz = 0; iz < d; ++iz) {
            for (u32 ix = 0; ix < d; ++ix) {
                float minh = 1e9f;
                float maxh = -1e9f;
                for (u32 dz = 0; dz < 2; ++dz) {
                    for (u32 dx = 0; dx < 2; ++dx) {
                        u32 cix = 2 * ix + dx;
                        u32 ciz = 2 * iz + dz;
                        const auto& child = nodes_[prev_off + (ciz * prev_d + cix)];
                        if (child.minh < minh) minh = child.minh;
                        if (child.maxh > maxh) maxh = child.maxh;
                    }
                }
                nodes_[off + (iz * d + ix)] = NodeBounds{ minh, maxh };
            }
        }
    }
    return true;
}

void TerrainQuadtree::bounds(u32 level, u32 ix, u32 iz, float& minh, float& maxh) const {
    if (level >= TERRAIN_LEVELS) {
        minh = -128.0f;
        maxh = 512.0f;
        return;
    }
    u32 d = 128u >> level;
    if (ix >= d || iz >= d || nodes_.empty()) {
        minh = -128.0f;
        maxh = 512.0f;
        return;
    }
    u32 off = level_offset(level);
    const auto& b = nodes_[off + (iz * d + ix)];
    minh = b.minh;
    maxh = b.maxh;
}

namespace {

void select_add(
    u32 level,
    u32 ix,
    u32 iz,
    TerrainNodeGpu* out,
    u32 capacity,
    u32& count,
    bool& overflow
) {
    if (count < capacity) {
        float size = 32.0f * static_cast<float>(1u << level);
        out[count] = TerrainNodeGpu{
            static_cast<float>(ix) * size,
            static_cast<float>(iz) * size,
            size,
            static_cast<float>(level)
        };
        count++;
    } else {
        overflow = true;
    }
}

bool select_recursive(
    const TerrainQuadtree& qt,
    u32 level,
    u32 ix,
    u32 iz,
    Vec3 cam,
    const Frustum* frustum,
    TerrainNodeGpu* out,
    u32 capacity,
    u32& count,
    bool& overflow
) {
    float size = 32.0f * static_cast<float>(1u << level);
    float x = static_cast<float>(ix) * size;
    float z = static_cast<float>(iz) * size;
    float minh = 0.0f, maxh = 0.0f;
    qt.bounds(level, ix, iz, minh, maxh);

    Vec3 box_mn(x, minh, z);
    Vec3 box_mx(x + size, maxh, z + size);

    float r = terrain_lod_range(level);
    if (aabb_dist_sq(cam, box_mn, box_mx) > r * r) {
        return false;
    }

    if (frustum != nullptr && !frustum_hits_aabb(*frustum, box_mn, box_mx)) {
        return true; // culled, nothing to draw
    }

    if (level == 0) {
        select_add(0, ix, iz, out, capacity, count, overflow);
        return true;
    }

    float rp = terrain_lod_range(level - 1);
    if (aabb_dist_sq(cam, box_mn, box_mx) > rp * rp) {
        select_add(level, ix, iz, out, capacity, count, overflow);
        return true;
    }

    // Children in z-major, x-minor order
    for (u32 dz = 0; dz < 2; ++dz) {
        for (u32 dx = 0; dx < 2; ++dx) {
            u32 cix = 2 * ix + dx;
            u32 ciz = 2 * iz + dz;
            if (!select_recursive(qt, level - 1, cix, ciz, cam, frustum, out, capacity, count, overflow)) {
                select_add(level - 1, cix, ciz, out, capacity, count, overflow);
            }
        }
    }
    return true;
}

} // namespace

SelectResult cdlod_select(
    const TerrainQuadtree& qt,
    const Frustum* frustum_or_null,
    Vec3 cam,
    TerrainNodeGpu* out,
    u32 capacity
) {
    SelectResult res{};
    select_recursive(qt, TERRAIN_ROOT_LEVEL, 0, 0, cam, frustum_or_null, out, capacity, res.count, res.overflow);
    return res;
}

u32 terrain_build_indices(u16* out, u32 capacity) {
    if (capacity < TERRAIN_INDEX_COUNT || out == nullptr) return 0;

    u32 idx = 0;
    for (u32 vj = 0; vj < 18; ++vj) {
        for (u32 vi = 0; vi < 18; ++vi) {
            u16 a = static_cast<u16>(vj * 19 + vi);
            u16 b = static_cast<u16>(vj * 19 + (vi + 1));
            u16 c = static_cast<u16>((vj + 1) * 19 + vi);
            u16 d = static_cast<u16>((vj + 1) * 19 + (vi + 1));

            // Triangle 1: (a, c, b)
            out[idx++] = a;
            out[idx++] = c;
            out[idx++] = b;

            // Triangle 2: (b, c, d)
            out[idx++] = b;
            out[idx++] = c;
            out[idx++] = d;
        }
    }
    return idx;
}

void ocean_depth_map_build(const HeightSampler& sampler, u8* out) {
    if (out == nullptr) return;

    static float dist[512 * 512];
    bool has_land = false;

    for (int j = 0; j < 512; ++j) {
        for (int i = 0; i < 512; ++i) {
            float x = (static_cast<float>(i) + 0.5f) * 8.0f;
            float z = (static_cast<float>(j) + 0.5f) * 8.0f;
            if (sampler.height_m(x, z) > 0.0f) {
                dist[j * 512 + i] = 0.0f;
                has_land = true;
            } else {
                dist[j * 512 + i] = 1e9f;
            }
        }
    }

    if (!has_land) {
        std::memset(out, 255, 512 * 512);
        return;
    }

    // Forward pass: W, NW, N, NE
    for (int j = 0; j < 512; ++j) {
        for (int i = 0; i < 512; ++i) {
            float d = dist[j * 512 + i];
            if (i > 0) d = std::min(d, dist[j * 512 + (i - 1)] + 1.0f);
            if (i > 0 && j > 0) d = std::min(d, dist[(j - 1) * 512 + (i - 1)] + 1.41421356f);
            if (j > 0) d = std::min(d, dist[(j - 1) * 512 + i] + 1.0f);
            if (i + 1 < 512 && j > 0) d = std::min(d, dist[(j - 1) * 512 + (i + 1)] + 1.41421356f);
            dist[j * 512 + i] = d;
        }
    }

    // Backward pass: E, SE, S, SW
    for (int j = 511; j >= 0; --j) {
        for (int i = 511; i >= 0; --i) {
            float d = dist[j * 512 + i];
            if (i + 1 < 512) d = std::min(d, dist[j * 512 + (i + 1)] + 1.0f);
            if (i + 1 < 512 && j + 1 < 512) d = std::min(d, dist[(j + 1) * 512 + (i + 1)] + 1.41421356f);
            if (j + 1 < 512) d = std::min(d, dist[(j + 1) * 512 + i] + 1.0f);
            if (i > 0 && j + 1 < 512) d = std::min(d, dist[(j + 1) * 512 + (i - 1)] + 1.41421356f);
            dist[j * 512 + i] = d;
        }
    }

    for (int idx = 0; idx < 512 * 512; ++idx) {
        float d_cells = dist[idx];
        if (d_cells <= 0.0f) {
            out[idx] = 0;
        } else {
            float depth_m = std::min(0.12f * d_cells * 8.0f, 120.0f);
            float val = std::round(depth_m / 120.0f * 255.0f);
            out[idx] = static_cast<u8>(std::clamp(val, 0.0f, 255.0f));
        }
    }
}

} // namespace ron::render
