layout(std140, binding = BIND_UBO_FRAME) uniform FrameUbo {
    mat4 u_view;
    mat4 u_proj;
    mat4 u_view_proj;
    mat4 u_inv_view_proj;
    vec4 u_cam_pos;    // xyz world pos
    vec4 u_screen;     // x=width y=height z=1/width w=1/height
    vec4 u_time;       // x=seconds y=sim_tick z=interp_alpha w=unused
    vec4 u_sun_dir;    // xyz direction TOWARD the sun, w=intensity
    vec4 u_sun_color;  // rgb, w unused
};
