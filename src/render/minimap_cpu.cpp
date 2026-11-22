#include "render/minimap_cpu.hpp"
#include "render/rmath.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace ron::render {

bool minimap_pick(float px, float py, const PxRect& rect, float* wx, float* wz) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return false;
    if (px < rect.x || px > rect.x + rect.w || py < rect.y || py > rect.y + rect.h) {
        return false;
    }
    if (wx) *wx = ((px - rect.x) / rect.w) * 4096.0f;
    if (wz) *wz = ((py - rect.y) / rect.h) * 4096.0f;
    return true;
}

void minimap_world_to_px(float wx, float wz, const PxRect& rect, float* px, float* py) {
    if (px) *px = rect.x + (wx / 4096.0f) * rect.w;
    if (py) *py = rect.y + (wz / 4096.0f) * rect.h;
}

void minimap_bake(const HeightSampler& sampler, u8* rgba /*512*512*4*/) {
    if (!rgba) return;

    Vec3 L = Vec3(-0.5f, 0.7f, -0.5f).normalize();
    float up_dot_L = Vec3(0.0f, 1.0f, 0.0f).dot(L);

    for (int j = 0; j < 512; ++j) {
        float cz = (static_cast<float>(j) + 0.5f) * 8.0f;
        for (int i = 0; i < 512; ++i) {
            float cx = (static_cast<float>(i) + 0.5f) * 8.0f;
            float h = sampler.height_m(cx, cz);
            Vec3 col;

            if (h < 0.0f) {
                float depth = -h;
                float t = std::min(depth / 60.0f, 1.0f);
                col = (1.0f - t) * Vec3(0.10f, 0.35f, 0.55f) + t * Vec3(0.02f, 0.10f, 0.28f);
            } else {
                float u = std::clamp(h / 400.0f, 0.0f, 1.0f);
                if (u < 0.35f) {
                    float s = u / 0.35f;
                    col = (1.0f - s) * Vec3(0.20f, 0.42f, 0.18f) + s * Vec3(0.45f, 0.40f, 0.25f);
                } else {
                    float s = (u - 0.35f) / 0.65f;
                    col = (1.0f - s) * Vec3(0.45f, 0.40f, 0.25f) + s * Vec3(0.85f, 0.85f, 0.85f);
                }

                // Hillshade (land only)
                float dhdx = (sampler.height_m(cx + 8.0f, cz) - sampler.height_m(cx - 8.0f, cz)) / 16.0f;
                float dhdz = (sampler.height_m(cx, cz + 8.0f) - sampler.height_m(cx, cz - 8.0f)) / 16.0f;
                Vec3 n = Vec3(-dhdx, 1.0f, -dhdz).normalize();
                float shade = std::clamp(0.85f + 0.9f * (n.dot(L) - up_dot_L), 0.65f, 1.15f);
                col = col * shade;
                col.x = std::clamp(col.x, 0.0f, 1.0f);
                col.y = std::clamp(col.y, 0.0f, 1.0f);
                col.z = std::clamp(col.z, 0.0f, 1.0f);
            }

            int idx = (j * 512 + i) * 4;
            rgba[idx + 0] = static_cast<u8>(std::clamp(static_cast<int>(std::floor(col.x * 255.0f + 0.5f)), 0, 255));
            rgba[idx + 1] = static_cast<u8>(std::clamp(static_cast<int>(std::floor(col.y * 255.0f + 0.5f)), 0, 255));
            rgba[idx + 2] = static_cast<u8>(std::clamp(static_cast<int>(std::floor(col.z * 255.0f + 0.5f)), 0, 255));
            rgba[idx + 3] = 255;
        }
    }
}

void camera_ground_quad(const float inv_view_proj[16], const float cam_pos[3], const HeightSampler& sampler, float out_xz[8]) {
    if (!inv_view_proj || !out_xz) return;

    Mat4 inv_vp;
    std::memcpy(inv_vp.m, inv_view_proj, sizeof(float) * 16);

    // Corners in order: BL(-1,-1), BR(1,-1), TR(1,1), TL(-1,1)
    const float ndc[4][2] = {
        { -1.0f, -1.0f }, // BL
        {  1.0f, -1.0f }, // BR
        {  1.0f,  1.0f }, // TR
        { -1.0f,  1.0f }  // TL
    };

    for (int k = 0; k < 4; ++k) {
        Vec3 ro, rd;
        unproject_ray(inv_vp, ndc[k][0], ndc[k][1], ro, rd);
        Vec3 hit;
        bool ok = pick_terrain(sampler, ro, rd, 16000.0f, hit);
        if (!ok) {
            hit = ro + rd * 8000.0f;
        }
        if (std::isnan(hit.x) || std::isnan(hit.z) || !std::isfinite(hit.x) || !std::isfinite(hit.z)) {
            hit.x = cam_pos ? cam_pos[0] : 2048.0f;
            hit.z = cam_pos ? cam_pos[2] : 2048.0f;
        }
        out_xz[k * 2 + 0] = hit.x;
        out_xz[k * 2 + 1] = hit.z;
    }
}

} // namespace ron::render
