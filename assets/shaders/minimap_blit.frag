#version 460 core

layout(binding = TEX_MINIMAP_BASE) uniform sampler2D u_minimap_target;

layout(location = 0) in vec2 v_uv;
out vec4 frag_color;

void main() {
    vec2 uv = vec2(v_uv.x, 1.0 - v_uv.y);
    vec4 col = texture(u_minimap_target, uv);
    frag_color = vec4(col.rgb, 1.0);
}
