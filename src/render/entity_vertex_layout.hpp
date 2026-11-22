#pragma once

#include "core/types.hpp"
#include <cstring>
#include <cstddef>
#include <cmath>

namespace ron::render {

constexpr u32 ENTITY_VERTEX_STRIDE = 24;
constexpr u32 ENTITY_OFFSET_POS    = 0;
constexpr u32 ENTITY_OFFSET_NORMAL = 12;
constexpr u32 ENTITY_OFFSET_UV     = 16;
constexpr u32 ENTITY_OFFSET_TRIM   = 20;
constexpr u32 ENTITY_OFFSET_FLAGS  = 21;
constexpr u32 ENTITY_OFFSET_PART   = 22;

static_assert(ENTITY_VERTEX_STRIDE == 24);
static_assert(ENTITY_OFFSET_POS == 0);
static_assert(ENTITY_OFFSET_NORMAL == 12);
static_assert(ENTITY_OFFSET_UV == 16);
static_assert(ENTITY_OFFSET_TRIM == 20);
static_assert(ENTITY_OFFSET_FLAGS == 21);
static_assert(ENTITY_OFFSET_PART == 22);

struct DecodedVertex {
    float pos[3];
    float nrm[3];
    float uv[2];
    u32   part;
};

inline void decode_oct_snorm16(i16 x, i16 y, float out[3]) noexcept {
    float px = static_cast<float>(x) / 32767.0f;
    float py = static_cast<float>(y) / 32767.0f;
    float pz = 1.0f - std::fabs(px) - std::fabs(py);

    if (pz < 0.0f) {
        float sx = (px >= 0.0f) ? 1.0f : -1.0f;
        float sy = (py >= 0.0f) ? 1.0f : -1.0f;
        float px_orig = px;
        float py_orig = py;
        px = (1.0f - std::fabs(py_orig)) * sx;
        py = (1.0f - std::fabs(px_orig)) * sy;
    }

    float len_sq = px * px + py * py + pz * pz;
    if (len_sq > 0.0f) {
        float inv_len = 1.0f / std::sqrt(len_sq);
        out[0] = px * inv_len;
        out[1] = py * inv_len;
        out[2] = pz * inv_len;
    } else {
        out[0] = 0.0f;
        out[1] = 1.0f;
        out[2] = 0.0f;
    }
}

inline DecodedVertex decode_vertex(const u8* ptr) noexcept {
    DecodedVertex out{};
    std::memcpy(out.pos, ptr + ENTITY_OFFSET_POS, 12);

    i16 nx = 0, ny = 0;
    std::memcpy(&nx, ptr + ENTITY_OFFSET_NORMAL, 2);
    std::memcpy(&ny, ptr + ENTITY_OFFSET_NORMAL + 2, 2);
    decode_oct_snorm16(nx, ny, out.nrm);

    u16 u = 0, v = 0;
    std::memcpy(&u, ptr + ENTITY_OFFSET_UV, 2);
    std::memcpy(&v, ptr + ENTITY_OFFSET_UV + 2, 2);
    out.uv[0] = static_cast<float>(u) / 65535.0f;
    out.uv[1] = static_cast<float>(v) / 65535.0f;

    u16 part = 0;
    std::memcpy(&part, ptr + ENTITY_OFFSET_PART, 2);
    out.part = static_cast<u32>(part);

    return out;
}

} // namespace ron::render
