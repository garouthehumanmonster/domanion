#include "render/shadow_ref.hpp"
#include "render/light_types.hpp"
#include <cmath>
#include <algorithm>

namespace ron::render {

float terrain_shadow_ref(const HeightSampler& hs, float x, float y, float z, const float L[3], int steps) noexcept {
    if (L[1] <= 0.01f) return 0.0f;

    float lxz = std::sqrt(L[0] * L[0] + L[2] * L[2]);
    if (lxz < 1e-4f) return 1.0f;

    float tanE = L[1] / lxz;
    float dir_x = L[0] / lxz;
    float dir_z = L[2] / lxz;

    float occ = 0.0f;
    for (int i = 1; i <= steps; ++i) {
        float d = TERRAIN_SHADOW_REACH * std::pow(static_cast<float>(i) / static_cast<float>(steps), 1.5f);
        float qx = std::clamp(x + dir_x * d, 0.0f, 4096.0f);
        float qz = std::clamp(z + dir_z * d, 0.0f, 4096.0f);
        float h = hs.height_m(qx, qz);
        float h_ray = y + TERRAIN_SHADOW_BIAS + d * tanE;
        float pen = 0.05f * d + 0.75f;
        float o_i = std::clamp((h - h_ray) / pen + 0.5f, 0.0f, 1.0f);
        occ = std::max(occ, o_i);
        if (occ >= 1.0f) break;
    }

    return 1.0f - occ;
}

} // namespace ron::render
