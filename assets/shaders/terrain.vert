#version 460 core

#include "common/frame.glsl"
#include "common/terrain_common.glsl"
#include "common/world_overlay.glsl"

layout(location = 0) out vec3 v_world;
layout(location = 1) flat out float v_lod;

void main() {
    int id = gl_VertexID;
    int gi = id % 19 - 1;
    int gj = id / 19 - 1;

    bool is_skirt = (gi < 0 || gi > 16 || gj < 0 || gj > 16);
    float cgi = clamp(float(gi), 0.0, 16.0);
    float cgj = clamp(float(gj), 0.0, 16.0);
    vec2 g = vec2(cgi, cgj);

    TerrainNodeData node = u_nodes[gl_InstanceID];
    vec2 node_xz = node.pos_size_level.xy;
    float node_size = node.pos_size_level.z;
    int level = int(round(node.pos_size_level.w));

    float cell = node_size / 16.0;
    vec2 p_xz = node_xz + g * cell;
    float h0 = height_at_world(p_xz);
    float d = length(vec3(p_xz.x, h0, p_xz.y) - u_cam_pos.xyz);

    float rp = (level == 0) ? 0.0 : lod_range(level - 1);
    float r = lod_range(level);
    float ms = mix(rp, r, 0.6);
    float morph = clamp((d - ms) / max(0.001, r - ms), 0.0, 1.0);

    g -= fract(g * 0.5) * 2.0 * morph;
    p_xz = node_xz + g * cell;
    float h = height_at_world(p_xz);

#if RON_OVERLAYS
    h += ovl_crater_height(p_xz);
#endif

    if (is_skirt) {
        h -= u_tparams0.x; // SKIRT_DEPTH_M
    }

    vec3 world_pos = vec3(p_xz.x, h, p_xz.y);
    v_world = world_pos;
    v_lod = float(level);

    gl_Position = u_view_proj * vec4(world_pos, 1.0);
}
