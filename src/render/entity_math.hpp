#pragma once

#include "core/types.hpp"
#include "render/entity_types.hpp"
#include "render/frame_uniforms.hpp"
#include <string>
#include <cmath>

namespace ron::render {

constexpr float ENT_PI     = 3.14159265358979323846f;
constexpr float ENT_TWO_PI = 6.28318530717958647692f;

// Wrap angle to [-pi, pi]
inline float wrap_pi(float a) noexcept {
    a = std::fmod(a + ENT_PI, ENT_TWO_PI);
    if (a < 0.0f) a += ENT_TWO_PI;
    return a - ENT_PI;
}

// Shortest-arc interpolation of yaw angles: yaw = prev + wrap_pi(curr - prev) * alpha
inline float lerp_yaw(float prev, float curr, float alpha) noexcept {
    float diff = wrap_pi(curr - prev);
    return wrap_pi(prev + diff * alpha);
}

// Transform model-local vector by R_y(yaw):
// R_y(theta) = [ [ sin(theta), 0, -cos(theta) ],
//                [ 0,          1,  0          ],
//                [ cos(theta), 0,  sin(theta) ] ] (col-major)
inline void entity_rotate_y(float yaw, const float in[3], float out[3]) noexcept {
    float s = std::sin(yaw);
    float c = std::cos(yaw);
    out[0] =  s * in[0] + c * in[2];
    out[1] =  in[1];
    out[2] = -c * in[0] + s * in[2];
}

// Column-major model matrix: T * R_y(yaw) * S
void entity_model_matrix(const float pos[3], float yaw, float scale, float out_col_major[16]) noexcept;

// Extract normalized frustum planes from column-major view-projection matrix (reverse-Z + ZERO_TO_ONE)
// Order: L (0), R (1), B (2), T (3), N (4), F (5)
void entity_planes_from_viewproj(const float vp[16], float planes[6][4]) noexcept;

// Fill EntityUbo from FrameUniforms and parameters
void entity_fill_ubo(
    const FrameUniforms& frame,
    float alpha,
    u32 instance_count,
    u32 model_count,
    float time_s,
    EntityUbo* out_ubo) noexcept;

// Model definition on CPU
struct ModelCpu {
    std::string name;
    u8  category{0};
    u32 lod_index_count[LODS_PER_MODEL]{0, 0, 0};
    u32 lod_first_index[LODS_PER_MODEL]{0, 0, 0};
    i32 lod_base_vertex[LODS_PER_MODEL]{0, 0, 0};
    float sphere[4]{0.0f, 0.0f, 0.0f, 0.0f}; // cx, cy, cz, radius
    u32 vat_row_base{0};
    u16 part_count{0};
};

// Cull result per §1
struct CullResult {
    u32  bucket{0xFFFFFFFFu}; // 0xFFFFFFFF if culled
    u8   reason{0};           // 0 visible, 1 invalid, 2 frustum, 3 size
    bool ambiguous{false};    // any plane within +-0.05 or |px - T| / T < 0.002
};

// Reference CPU culling implementation identical to compute shader
CullResult entity_cull_reference(
    const EntityInstance& inst,
    const ModelCpu* models,
    const EntityUbo& ubo) noexcept;

// LOD selection from projected pixel diameter
inline int entity_lod_from_px(float px) noexcept {
    if (px >= LOD0_PX) return 0;
    if (px >= LOD1_PX) return 1;
    if (px >= CULL_PX) return 2;
    return -1; // size-culled
}

// CPU view of VAT texture
struct VatCpu {
    const u16* halfs{nullptr};
    u32 rows{0};
    u32 cols{VAT_COLS};
};

// Sample VAT part matrix (3x4 affine, rows 0..2)
void vat_part_matrix(
    const VatCpu& vat,
    int model,
    int clip,
    float anim_t,
    int part,
    float out3x4[12]) noexcept;

// Skin vertex position by 3x4 affine matrix
inline void skin_position(const float m3x4[12], const float p[3], float out[3]) noexcept {
    out[0] = m3x4[0] * p[0] + m3x4[1] * p[1] + m3x4[2]  * p[2] + m3x4[3];
    out[1] = m3x4[4] * p[0] + m3x4[5] * p[1] + m3x4[6]  * p[2] + m3x4[7];
    out[2] = m3x4[8] * p[0] + m3x4[9] * p[1] + m3x4[10] * p[2] + m3x4[11];
}

float half_to_float(u16 h) noexcept;

} // namespace ron::render
