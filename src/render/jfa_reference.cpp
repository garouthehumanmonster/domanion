#include "render/jfa_reference.hpp"
#include <vector>
#include <cmath>

namespace ron::render {

void jfa_seed_reference(const u8* owner, int owner_n, float* out_xy /*G*G*2*/) {
    if (!owner || !out_xy || owner_n <= 0) return;
    int g = 2 * owner_n;

    const int DIRS[4][2] = {
        {  1,  0 },
        { -1,  0 },
        {  0,  1 },
        {  0, -1 }
    };

    for (int py = 0; py < g; ++py) {
        for (int px = 0; px < g; ++px) {
            int o = static_cast<int>(owner[(py >> 1) * owner_n + (px >> 1)]);
            float best_x = -1.0f;
            float best_y = -1.0f;

            for (int d = 0; d < 4; ++d) {
                int qx = px + DIRS[d][0];
                int qy = py + DIRS[d][1];
                if (qx < 0 || qx >= g || qy < 0 || qy >= g) {
                    continue; // treated as same owner
                }
                int qo = static_cast<int>(owner[(qy >> 1) * owner_n + (qx >> 1)]);
                if (qo != o) {
                    best_x = static_cast<float>(2 * px + 1 + DIRS[d][0]);
                    best_y = static_cast<float>(2 * py + 1 + DIRS[d][1]);
                    break;
                }
            }

            int idx = (py * g + px) * 2;
            out_xy[idx + 0] = best_x;
            out_xy[idx + 1] = best_y;
        }
    }
}

void jfa_brute_d2(const float* seeds_xy, int g, float* out_d2) {
    if (!seeds_xy || !out_d2 || g <= 0) return;

    struct Seed {
        float x;
        float y;
    };
    std::vector<Seed> valid_seeds;
    valid_seeds.reserve(static_cast<size_t>(g * g));

    for (int i = 0; i < g * g; ++i) {
        float sx = seeds_xy[i * 2 + 0];
        float sy = seeds_xy[i * 2 + 1];
        if (sx >= 0.0f) {
            valid_seeds.push_back({ sx, sy });
        }
    }

    for (int py = 0; py < g; ++py) {
        float cy = static_cast<float>(2 * py + 1);
        for (int px = 0; px < g; ++px) {
            float cx = static_cast<float>(2 * px + 1);
            float min_d2 = 1e30f;

            for (const auto& s : valid_seeds) {
                float dx = s.x - cx;
                float dy = s.y - cy;
                float d2 = dx * dx + dy * dy;
                if (d2 < min_d2) {
                    min_d2 = d2;
                }
            }

            out_d2[py * g + px] = min_d2;
        }
    }
}

} // namespace ron::render
