#include "render/height_sampler.hpp"

namespace ron::render {

bool pick_terrain(const HeightSampler& sampler, Vec3 origin, Vec3 dir, float max_dist, Vec3& hit) {
    if (origin.x < 0.0f || origin.x >= 4096.0f || origin.z < 0.0f || origin.z >= 4096.0f) {
        return false;
    }

    float h_origin = sampler.height_m(origin.x, origin.z);
    if (origin.y < h_origin) {
        return false;
    }

    float t_prev = 0.0f;
    float t = 0.0f;

    while (t < max_dist) {
        float step = std::clamp(0.002f * t, 0.5f, 4.0f);
        t += step;
        if (t > max_dist) {
            t = max_dist;
        }

        Vec3 p = origin + dir * t;
        if (p.x < 0.0f || p.x >= 4096.0f || p.z < 0.0f || p.z >= 4096.0f) {
            return false;
        }

        float h = sampler.height_m(p.x, p.z);
        if (p.y < h) {
            float t_above = t_prev;
            float t_below = t;
            for (int s = 0; s < 16; ++s) {
                float t_mid = 0.5f * (t_above + t_below);
                Vec3 pm = origin + dir * t_mid;
                float hm = sampler.height_m(pm.x, pm.z);
                if (pm.y < hm) {
                    t_below = t_mid;
                } else {
                    t_above = t_mid;
                }
            }
            float t_hit = 0.5f * (t_above + t_below);
            hit = origin + dir * t_hit;
            return true;
        }

        t_prev = t;
    }

    return false;
}

} // namespace ron::render
