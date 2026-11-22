#pragma once

#include "core/types.hpp"
#include "render/frame_uniforms.hpp"
#include "render/rmath.hpp"

namespace ron::render {

void fill_frame_uniforms(
    FrameUniforms& out,
    const Mat4& view,
    const Mat4& proj,
    Vec3 cam_pos,
    u32 w,
    u32 h,
    float time_s,
    float sim_tick,
    float interp_alpha,
    Vec3 sun_dir,
    float sun_intensity,
    Vec3 sun_color
);

} // namespace ron::render
