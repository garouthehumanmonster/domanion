#version 460 core

#include "common/frame.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main() {
    float base_val = v_uv.x * 8.0;
    float mod_val = 0.75 + 0.25 * sin(u_time.x + v_uv.y * 6.0);
    o_color = vec4(vec3(base_val * mod_val), 1.0);
}
