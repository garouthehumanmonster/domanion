#pragma once

#include "core/types.hpp"

namespace ron::render {

struct alignas(16) FrameUniforms {
    float view[16];
    float proj[16];
    float view_proj[16];
    float inv_view_proj[16];
    float cam_pos[4];
    float screen[4];
    float time[4];
    float sun_dir[4];
    float sun_color[4];
};

static_assert(sizeof(FrameUniforms) == 336, "FrameUniforms must be 336 bytes");
constexpr u32 FRAME_UBO_BYTES = 336;

} // namespace ron::render
