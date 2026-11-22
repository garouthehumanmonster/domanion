#version 460 core

layout(location = 1) uniform vec4 u_color;
layout(location = 0) out vec4 o_c;

void main() {
    o_c = u_color;
}
