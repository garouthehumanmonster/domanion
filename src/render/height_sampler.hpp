#pragma once

#include "core/types.hpp"
#include "render/rmath.hpp"
#include <algorithm>
#include <cmath>

namespace ron::render {

inline float height_code_to_m(u16 code) {
    return -128.0f + 640.0f * (static_cast<float>(code) / 65535.0f);
}

inline u16 height_m_to_code(float m) {
    float clamped = std::clamp(m, -128.0f, 512.0f);
    float code_f = std::round(((clamped - (-128.0f)) / 640.0f) * 65535.0f);
    return static_cast<u16>(std::clamp(code_f, 0.0f, 65535.0f));
}

class HeightSampler {
public:
    HeightSampler() = default;

    void init(const u16* codes, u32 n) {
        codes_ = codes;
        n_ = n;
        if (codes_ && n_ > 0) {
            u16 min_c = codes_[0];
            u16 max_c = codes_[0];
            u64 total = static_cast<u64>(n_) * n_;
            for (u64 i = 1; i < total; ++i) {
                if (codes_[i] < min_c) min_c = codes_[i];
                if (codes_[i] > max_c) max_c = codes_[i];
            }
            min_m_ = height_code_to_m(min_c);
            max_m_ = height_code_to_m(max_c);
        } else {
            min_m_ = -128.0f;
            max_m_ = 512.0f;
        }
    }

    u32 n() const { return n_; }
    float cell_m() const { return (n_ > 0) ? (4096.0f / static_cast<float>(n_)) : 0.0f; }

    u16 code(i32 i, i32 j) const {
        if (!codes_ || n_ == 0) return 13107;
        i = std::clamp(i, 0, static_cast<i32>(n_ - 1));
        j = std::clamp(j, 0, static_cast<i32>(n_ - 1));
        return codes_[static_cast<u64>(j) * n_ + static_cast<u64>(i)];
    }

    float height_m(float x, float z) const {
        if (!codes_ || n_ == 0) return 0.0f;
        float cell = cell_m();
        float u = x / cell - 0.5f;
        float v = z / cell - 0.5f;

        float u_clamped = std::clamp(u, 0.0f, static_cast<float>(n_ - 1));
        float v_clamped = std::clamp(v, 0.0f, static_cast<float>(n_ - 1));

        int i0 = static_cast<int>(std::floor(u_clamped));
        int j0 = static_cast<int>(std::floor(v_clamped));
        int i1 = std::min(i0 + 1, static_cast<int>(n_ - 1));
        int j1 = std::min(j0 + 1, static_cast<int>(n_ - 1));

        float fx = u_clamped - static_cast<float>(i0);
        float fz = v_clamped - static_cast<float>(j0);

        float h00 = height_code_to_m(code(i0, j0));
        float h10 = height_code_to_m(code(i1, j0));
        float h01 = height_code_to_m(code(i0, j1));
        float h11 = height_code_to_m(code(i1, j1));

        float h0 = h00 * (1.0f - fx) + h10 * fx;
        float h1 = h01 * (1.0f - fx) + h11 * fx;
        return h0 * (1.0f - fz) + h1 * fz;
    }

    float min_m() const { return min_m_; }
    float max_m() const { return max_m_; }

private:
    const u16* codes_{nullptr};
    u32 n_{0};
    float min_m_{-128.0f};
    float max_m_{512.0f};
};

bool pick_terrain(const HeightSampler& sampler, Vec3 origin, Vec3 dir, float max_dist, Vec3& hit);

} // namespace ron::render
