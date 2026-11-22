#include "render/crater_math.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace ron::render {

static constexpr float K_PI = 3.14159265358979323846f;

float crater_extent_m(const CraterSplat& splat) {
    return std::max(1.75f * splat.radius, splat.scorch_mul * splat.radius);
}

float crater_offset_m(const CraterSplat& splat, float px, float pz) {
    float dx = px - splat.x;
    float dz = pz - splat.z;
    float r = std::sqrt(dx * dx + dz * dz);
    float ang = (r > 1e-4f) ? std::atan2(dz, dx) : 0.0f;

    float p1 = static_cast<float>(splat.seed & 0xFFFFu) * (2.0f * K_PI / 65535.0f);
    float p2 = static_cast<float>(splat.seed >> 16) * (2.0f * K_PI / 65535.0f);
    float nz = 0.5f + 0.25f * std::sin(3.0f * ang + p1) + 0.25f * std::sin(5.0f * ang + p2);

    float rm = splat.radius * (1.0f + 0.08f * (2.0f * nz - 1.0f));
    float t = r / rm;
    float a = 1.0f - t * t;
    float bowl = (t < 1.0f) ? (-splat.depth * a * a) : 0.0f;
    float q = (t - 1.12f) / 0.18f;
    float rim = 0.18f * splat.depth * std::exp(-q * q);
    return bowl + rim;
}

float crater_scorch(const CraterSplat& splat, float px, float pz) {
    float dx = px - splat.x;
    float dz = pz - splat.z;
    float r = std::sqrt(dx * dx + dz * dz);
    float ang = (r > 1e-4f) ? std::atan2(dz, dx) : 0.0f;

    float p1 = static_cast<float>(splat.seed & 0xFFFFu) * (2.0f * K_PI / 65535.0f);
    float p2 = static_cast<float>(splat.seed >> 16) * (2.0f * K_PI / 65535.0f);
    float nz = 0.5f + 0.25f * std::sin(3.0f * ang + p1) + 0.25f * std::sin(5.0f * ang + p2);

    float sr = splat.radius * splat.scorch_mul;
    float edge0 = 0.6f * sr;
    float edge1 = sr;
    float x_ss = std::clamp((r - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    float ss = x_ss * x_ss * (3.0f - 2.0f * x_ss);
    float nz_factor = 0.85f + 0.15f * nz;
    return splat.scorch * nz_factor * (1.0f - ss);
}

TexelBox crater_box(const CraterSplat& splat, int n) {
    float cell = 4096.0f / static_cast<float>(n);
    float extent = crater_extent_m(splat);
    int x0 = std::clamp(static_cast<int>(std::floor((splat.x - extent) / cell)), 0, n - 1);
    int x1 = std::clamp(static_cast<int>(std::floor((splat.x + extent) / cell)), 0, n - 1);
    int y0 = std::clamp(static_cast<int>(std::floor((splat.z - extent) / cell)), 0, n - 1);
    int y1 = std::clamp(static_cast<int>(std::floor((splat.z + extent) / cell)), 0, n - 1);
    return TexelBox{ x0, y0, x1, y1 };
}

void crater_clear(u8* rgba, int n) {
    if (!rgba || n <= 0) return;
    for (int i = 0; i < n * n; ++i) {
        rgba[i * 4 + 0] = 128;
        rgba[i * 4 + 1] = 0;
        rgba[i * 4 + 2] = 0;
        rgba[i * 4 + 3] = 0;
    }
}

void crater_apply_cpu(u8* rgba, int n, const CraterSplat& splat) {
    if (!rgba || n <= 0) return;
    float cell = 4096.0f / static_cast<float>(n);
    float extent = crater_extent_m(splat);
    TexelBox box = crater_box(splat, n);

    for (int j = box.y0; j <= box.y1; ++j) {
        float pz = (static_cast<float>(j) + 0.5f) * cell;
        for (int i = box.x0; i <= box.x1; ++i) {
            float px = (static_cast<float>(i) + 0.5f) * cell;
            float dx = px - splat.x;
            float dz = pz - splat.z;
            float r = std::sqrt(dx * dx + dz * dz);
            if (r > extent) continue;

            float off = crater_offset_m(splat, px, pz);
            float sc = crater_scorch(splat, px, pz);

            int idx = (j * n + i) * 4;
            u8 old_code = rgba[idx + 0];
            float old_off = (static_cast<float>(old_code) - 128.0f) * 0.0625f;
            float new_off = std::clamp(old_off + off, -8.0f, 7.9375f);
            float code_f = std::floor(new_off / 0.0625f + 0.5f) + 128.0f;
            u8 new_code = static_cast<u8>(std::clamp(static_cast<int>(code_f), 0, 255));

            u8 old_g = rgba[idx + 1];
            float sc_clamped = std::clamp(sc, 0.0f, 1.0f);
            u8 sc_code = static_cast<u8>(std::clamp(static_cast<int>(std::floor(sc_clamped * 255.0f + 0.5f)), 0, 255));
            u8 new_g = std::max(old_g, sc_code);

            rgba[idx + 0] = new_code;
            rgba[idx + 1] = new_g;
        }
    }
}

CraterSplat crater_sanitize(CraterSplat splat) {
    CraterSplat out = splat;
    out.radius = std::clamp(splat.radius, 3.0f, 64.0f);
    out.depth = std::clamp(splat.depth, 0.0f, 8.0f);
    out.scorch = std::clamp(splat.scorch, 0.0f, 1.0f);
    out.scorch_mul = std::clamp(splat.scorch_mul, 1.0f, 3.0f);
    return out;
}

} // namespace ron::render
