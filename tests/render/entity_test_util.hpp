#pragma once

#include "core/types.hpp"
#include "render/entity_types.hpp"
#include <vector>
#include <cmath>

namespace ron::render::test {

struct Xorshift32 {
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

inline std::vector<EntityInstance> make_random_instances(
    u32 n,
    u32 model_count,
    float focus_x, float focus_z,
    float near_radius, float far_radius,
    u32 near_count,
    float (*height_fn)(float x, float z, void* user),
    void* user)
{
    std::vector<EntityInstance> res(n);
    Xorshift32 rng{2026};

    for (u32 i = 0; i < n; ++i) {
        EntityInstance inst{};
        u32 model = static_cast<u32>(rng.next_int(0, model_count - 1));
        u32 owner = static_cast<u32>(rng.next_int(1, 8));
        u32 flags = 0;
        inst.ids = entity_pack_ids(model, owner, flags);
        inst.clip = static_cast<u32>(rng.next_int(0, 3));
        inst.anim_t = rng.next_float(0.0f, 10.0f);

        float r, angle;
        if (i < near_count) {
            inst.scale = rng.next_float(2.0f, 4.0f);
            r = rng.next_float(0.0f, near_radius);
        } else {
            inst.scale = rng.next_float(0.5f, 2.5f);
            r = rng.next_float(0.0f, far_radius);
        }
        angle = rng.next_float(0.0f, 6.2831853f);

        float px = focus_x + r * std::cos(angle);
        float pz = focus_z + r * std::sin(angle);
        float ground_y = height_fn ? height_fn(px, pz, user) : 0.0f;
        float py = ground_y + rng.next_float(0.0f, 30.0f);

        inst.pos_prev[0] = px;
        inst.pos_prev[1] = py;
        inst.pos_prev[2] = pz;

        float yaw = rng.next_float(-3.14159f, 3.14159f);
        inst.yaw_prev = yaw;

        // curr = prev + up to 2m
        float dx = rng.next_float(-2.0f, 2.0f);
        float dy = rng.next_float(-0.5f, 0.5f);
        float dz = rng.next_float(-2.0f, 2.0f);
        inst.pos_curr[0] = px + dx;
        inst.pos_curr[1] = py + dy;
        inst.pos_curr[2] = pz + dz;

        inst.yaw_curr = yaw + rng.next_float(-0.5f, 0.5f);

        res[i] = inst;
    }

    return res;
}

} // namespace ron::render::test
