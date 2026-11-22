#pragma once

#include "core/types.hpp"
#include "render/entity_types.hpp"
#include "render/entity_assets.hpp"
#include "render/height_sampler.hpp"
#include <vector>

namespace ron::render {

struct CrowdEntity {
    EntityInstance inst;
    float speed{0.0f};
    u8 category{0};
};

struct CrowdRng {
    u32 state{2026};

    u32 next() noexcept {
        u32 x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    float next_float(float min_v, float max_v) noexcept {
        float t = static_cast<float>(next() & 0xFFFFFFu) / 16777215.0f;
        return min_v + t * (max_v - min_v);
    }

    int next_int(int min_v, int max_v) noexcept {
        if (min_v >= max_v) return min_v;
        u32 range = static_cast<u32>(max_v - min_v + 1);
        return min_v + static_cast<int>(next() % range);
    }
};

void init_crowd(
    std::vector<CrowdEntity>& crowd,
    u32 count,
    const EntityAssets& assets,
    const HeightSampler& sampler,
    float focus_x,
    float focus_z);

void update_crowd(
    std::vector<CrowdEntity>& crowd,
    const HeightSampler& sampler,
    float focus_x,
    float focus_z,
    float dt,
    float& sim_accumulator,
    float& out_alpha);

} // namespace ron::render
