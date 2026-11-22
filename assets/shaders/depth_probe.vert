#version 460 core

layout(location = 0) uniform float u_z;
layout(location = 0) out vec2 v_uv;

void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, u_z, 1.0);
    v_uv = p;
}
