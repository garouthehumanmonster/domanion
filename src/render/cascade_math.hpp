#pragma once

#include "core/types.hpp"
#include "render/light_types.hpp"
#include "render/frame_uniforms.hpp"

namespace ron::render {

struct CascadeSet {
    float split[5];
    float light_vp[4][16];
    float texel_world[4];
    float inv_range[4];
    float radius[4];
    float centre_ls[4][3];
};

void cascade_splits(float n, float f, float lambda, float out[5]) noexcept;
void cascade_fit(const FrameUniforms& fu, const float L[3], int res, CascadeSet* out) noexcept;
int cascade_select(const CascadeSet& cs, float view_depth) noexcept;
void lighting_fill_ubo(const CascadeSet& cs, const FrameUniforms& fu, u32 flags, LightingUbo* out) noexcept;

} // namespace ron::render
