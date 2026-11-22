#include "render/frame_fill.hpp"
#include <cstring>

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
) {
    std::memset(&out, 0, sizeof(FrameUniforms));

    std::memcpy(out.view, view.m, sizeof(float) * 16);
    std::memcpy(out.proj, proj.m, sizeof(float) * 16);

    Mat4 view_proj = mat4_mul(proj, view);
    std::memcpy(out.view_proj, view_proj.m, sizeof(float) * 16);

    Mat4 inv_view_proj{};
    mat4_inverse(view_proj, inv_view_proj);
    std::memcpy(out.inv_view_proj, inv_view_proj.m, sizeof(float) * 16);

    out.cam_pos[0] = cam_pos.x;
    out.cam_pos[1] = cam_pos.y;
    out.cam_pos[2] = cam_pos.z;
    out.cam_pos[3] = 0.0f;

    float inv_w = (w > 0) ? (1.0f / static_cast<float>(w)) : 0.0f;
    float inv_h = (h > 0) ? (1.0f / static_cast<float>(h)) : 0.0f;
    out.screen[0] = static_cast<float>(w);
    out.screen[1] = static_cast<float>(h);
    out.screen[2] = inv_w;
    out.screen[3] = inv_h;

    out.time[0] = time_s;
    out.time[1] = sim_tick;
    out.time[2] = interp_alpha;
    out.time[3] = 0.0f;

    Vec3 norm_sun = sun_dir.normalize();
    out.sun_dir[0] = norm_sun.x;
    out.sun_dir[1] = norm_sun.y;
    out.sun_dir[2] = norm_sun.z;
    out.sun_dir[3] = sun_intensity;

    out.sun_color[0] = sun_color.x;
    out.sun_color[1] = sun_color.y;
    out.sun_color[2] = sun_color.z;
    out.sun_color[3] = 0.0f;
}

} // namespace ron::render
