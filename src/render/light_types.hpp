#pragma once

#include "core/types.hpp"
#include "render/frame_uniforms.hpp"
#include <cmath>
#include <cstddef>
#include <type_traits>

namespace ron::render {

// Gate 13 Constants (§1)
constexpr u32   SHADOW_RES            = 2048;
constexpr u32   TERRAIN_SHADOW_STEPS  = 48;
constexpr u32   SHADOW_CASCADES       = 4;
constexpr float SPLIT_LAMBDA          = 0.80f;
constexpr float SHADOW_NEAR           = 1.0f;
constexpr float SHADOW_DISTANCE       = 1500.0f;
constexpr float CASCADE_Z_UP          = 700.0f;
constexpr float CASCADE_Z_DOWN        = 400.0f;
constexpr float TERRAIN_SHADOW_REACH  = 1100.0f;
constexpr float TERRAIN_SHADOW_BIAS   = 0.5f;
constexpr float ENTITY_FRUSTUM_MARGIN = 48.0f;

// Ambient defaults (§1)
constexpr float AMBIENT_SKY[3]        = {0.38f, 0.52f, 0.78f};
constexpr float AMBIENT_GROUND[3]     = {0.20f, 0.18f, 0.15f};
constexpr float AMBIENT_INTENSITY     = 0.55f;

// Light flags (§1)
constexpr u32 LIGHT_FLAG_CSM            = 1u << 0; // bit 0 = CSM (1)
constexpr u32 LIGHT_FLAG_TERRAIN_SHADOW = 1u << 1; // bit 1 = terrain height march (2)

// Unit vector towards sun from FrameUniforms
inline void light_to_sun(const FrameUniforms& fu, float L[3]) noexcept {
    float len = std::sqrt(fu.sun_dir[0] * fu.sun_dir[0] +
                          fu.sun_dir[1] * fu.sun_dir[1] +
                          fu.sun_dir[2] * fu.sun_dir[2]);
    float inv_len = (len > 1e-6f) ? (1.0f / len) : 1.0f;
    L[0] = fu.sun_dir[0] * inv_len;
    L[1] = fu.sun_dir[1] * inv_len;
    L[2] = fu.sun_dir[2] * inv_len;
}

// Set sun direction in FrameUniforms from elevation and azimuth (degrees)
// Azimuth 0 = north (-Z), 90 = east (+X)
inline void set_sun(FrameUniforms& fu, float elev_deg, float az_deg) noexcept {
    constexpr float DEG2RAD = 3.14159265358979323846f / 180.0f;
    float elev_rad = elev_deg * DEG2RAD;
    float az_rad = az_deg * DEG2RAD;

    float cos_e = std::cos(elev_rad);
    float sin_e = std::sin(elev_rad);
    float cos_a = std::cos(az_rad);
    float sin_a = std::sin(az_rad);

    // dir = (cos(elev)*sin(az), sin(elev), -cos(elev)*cos(az))
    float dir[3] = {
        cos_e * sin_a,
        sin_e,
        -cos_e * cos_a
    };
    float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    float inv_len = (len > 1e-6f) ? (1.0f / len) : 1.0f;
    fu.sun_dir[0] = dir[0] * inv_len;
    fu.sun_dir[1] = dir[1] * inv_len;
    fu.sun_dir[2] = dir[2] * inv_len;
}

// LightingUbo std140 (368 B)
struct alignas(16) LightingUbo {
    float light_vp[4][16];   // offset 0 (256 B): 4 cascades of col-major 4x4 matrix
    float splits[4];         // offset 256 (16 B): vec4 {z_1, z_2, z_3, z_4}
    float texel[4];          // offset 272 (16 B): vec4 {texel_world[0..3]}
    float inv_range[4];      // offset 288 (16 B): vec4 {inv_range[0..3]}
    float cam_fwd[4];        // offset 304 (16 B): vec4 {fwd.x, fwd.y, fwd.z, flags (as float)}
    float ambient_sky[4];    // offset 320 (16 B): vec4 {rgb, intensity}
    float ambient_ground[4]; // offset 336 (16 B): vec4 {rgb, 0.0f}
    float shadow_p[4];       // offset 352 (16 B): vec4 {1.0f (pcf radius), 48.0f, 1100.0f, 0.0f}
};

static_assert(sizeof(LightingUbo) == 368, "LightingUbo must be 368 B");
static_assert(alignof(LightingUbo) == 16, "LightingUbo must be 16-byte aligned");
static_assert(offsetof(LightingUbo, light_vp) == 0);
static_assert(offsetof(LightingUbo, splits) == 256);
static_assert(offsetof(LightingUbo, texel) == 272);
static_assert(offsetof(LightingUbo, inv_range) == 288);
static_assert(offsetof(LightingUbo, cam_fwd) == 304);
static_assert(offsetof(LightingUbo, ambient_sky) == 320);
static_assert(offsetof(LightingUbo, ambient_ground) == 336);
static_assert(offsetof(LightingUbo, shadow_p) == 352);
static_assert(std::is_trivially_copyable_v<LightingUbo>);

// MarksUbo std140 (832 B)
struct alignas(16) MarksUbo {
    float icon_p[4];          // offset 0 (16 B): vec4 {20.0f, 8.0f, 28.0f, ui_scale}
    float bar_p[4];           // offset 16 (16 B): vec4 {14.0f, 64.0f, 4.0f, 3.0f}
    float screen[4];          // offset 32 (16 B): vec4 {w, h, 1/w, 1/h}
    float atlas[4];           // offset 48 (16 B): vec4 {atlas_w (1024), atlas_h (512), 4, 0}
    float owner_color[16][4]; // offset 64 (256 B): vec4 owner_color[16] (overlay_palette, alpha 1.0)
    float icon_uv[32][4];     // offset 320 (512 B): vec4 icon_uv[32] {u0, v0, u1, v1}
};

static_assert(sizeof(MarksUbo) == 832, "MarksUbo must be 832 B");
static_assert(alignof(MarksUbo) == 16, "MarksUbo must be 16-byte aligned");
static_assert(offsetof(MarksUbo, icon_p) == 0);
static_assert(offsetof(MarksUbo, bar_p) == 16);
static_assert(offsetof(MarksUbo, screen) == 32);
static_assert(offsetof(MarksUbo, atlas) == 48);
static_assert(offsetof(MarksUbo, owner_color) == 64);
static_assert(offsetof(MarksUbo, icon_uv) == 320);
static_assert(std::is_trivially_copyable_v<MarksUbo>);

} // namespace ron::render
