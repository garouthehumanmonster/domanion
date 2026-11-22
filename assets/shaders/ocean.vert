#version 460 core

#include "common/frame.glsl"
#include "common/terrain_common.glsl"

layout(location = 0) out vec3 v_world;

void main() {
    int id = gl_VertexID;
    float x = ((id & 1) != 0) ? 24096.0 : -20000.0;
    float z = ((id >> 1) != 0) ? 24096.0 : -20000.0;
    vec3 world_pos = vec3(x, 0.0, z);
    v_world = world_pos;
    gl_Position = u_view_proj * vec4(world_pos, 1.0);
}
