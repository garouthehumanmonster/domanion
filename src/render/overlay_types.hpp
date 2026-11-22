#pragma once

#include "core/types.hpp"
#include "render/rmath.hpp"
#include "render/gl_bindings.hpp"
#include <type_traits>
#include <cstddef>
#include <algorithm>

namespace ron::render {

constexpr int OWNER_N = 512;
constexpr int JFA_G = 1024;
constexpr int CRATER_N = 2048;
constexpr int FOG_N = 512;
constexpr int MINIMAP_N = 512;
constexpr int WORLD_M = 4096;
constexpr int SPLAT_QUEUE_CAP = 1024;
constexpr int MAX_SPLATS_PER_FLUSH = 64;
constexpr int MAX_BLIPS = 4096;

static_assert(OWNER_N == 512);
static_assert(JFA_G == 1024);
static_assert(CRATER_N == 2048);
static_assert(FOG_N == 512);
static_assert(MINIMAP_N == 512);
static_assert(WORLD_M == 4096);
static_assert(SPLAT_QUEUE_CAP == 1024);
static_assert(MAX_SPLATS_PER_FLUSH == 64);
static_assert(MAX_BLIPS == 4096);

struct alignas(4) CraterSplat {
    float x;
    float z;
    float radius;
    float depth;
    float scorch;
    float scorch_mul;
    u32 seed;
    u32 pad;
};
static_assert(sizeof(CraterSplat) == 32);
static_assert(std::is_trivially_copyable_v<CraterSplat>);

struct alignas(4) Blip {
    float x;
    float z;
    u32 rgba; // packed R in low byte
    float radius_px;
};
static_assert(sizeof(Blip) == 16);
static_assert(std::is_trivially_copyable_v<Blip>);

struct alignas(16) OverlayUbo {
    float owner_color[16][4]; // 256 B (0..255)
    // p0 (offset 256)
    float territory_on{0.0f};
    float fill_alpha{0.12f};
    float line_w0_m{2.0f};
    float line_w1_m{5.0f};
    // p1 (offset 272)
    float fog_on{0.0f};
    float fog_unexplored{0.0f};
    float fog_explored_dim{0.45f};
    float crater_on{1.0f};
    // p2 (offset 288)
    float world_size{4096.0f};
    float inv_world{1.0f / 4096.0f};
    float reserved_p2{0.0f};
    float time_s{0.0f};
    // p3 (offset 304)
    float local_player{0.0f};
    float reserved_p3_0{0.0f};
    float reserved_p3_1{0.0f};
    float reserved_p3_2{0.0f};
};
static_assert(sizeof(OverlayUbo) == 320);
static_assert(offsetof(OverlayUbo, territory_on) == 256);
static_assert(offsetof(OverlayUbo, fog_on) == 272);
static_assert(offsetof(OverlayUbo, world_size) == 288);
static_assert(offsetof(OverlayUbo, local_player) == 304);
static_assert(std::is_trivially_copyable_v<OverlayUbo>);

struct alignas(16) MinimapUbo {
    float quad[8]; // BL, BR, TR, TL (x, z)
    float fog_on{0.0f};
    float blip_count{0.0f};
    float time_s{0.0f};
    float line_px{1.0f};
};
static_assert(sizeof(MinimapUbo) == 48);
static_assert(std::is_trivially_copyable_v<MinimapUbo>);

struct Vec4 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{0.0f};

    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

inline Vec4 overlay_palette(int owner) noexcept {
    int o = std::clamp(owner, 0, 15);
    switch (o) {
        case 0: return Vec4{0.50f, 0.50f, 0.50f, 1.0f};
        case 1: return Vec4{0.86f, 0.16f, 0.16f, 1.0f};
        case 2: return Vec4{0.15f, 0.38f, 0.92f, 1.0f};
        case 3: return Vec4{0.18f, 0.72f, 0.28f, 1.0f};
        case 4: return Vec4{0.95f, 0.80f, 0.15f, 1.0f};
        case 5: return Vec4{0.60f, 0.28f, 0.80f, 1.0f};
        case 6: return Vec4{0.95f, 0.52f, 0.12f, 1.0f};
        case 7: return Vec4{0.15f, 0.80f, 0.85f, 1.0f};
        case 8: return Vec4{0.90f, 0.90f, 0.92f, 1.0f};
        default: return Vec4{0.60f, 0.60f, 0.60f, 1.0f};
    }
}

} // namespace ron::render
