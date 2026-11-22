#version 460 core

struct BlipGpu {
    vec2 pos;
    uint rgba;
    float radius_px;
};

layout(std430, binding = BIND_SSBO_BLIPS) readonly buffer BlipBuffer {
    BlipGpu u_blips[];
};

out vec2 v_local_px;
out float v_r;
flat out vec3 v_color;

const vec2 CORNERS[6] = vec2[6](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

void main() {
    uint idx = uint(gl_InstanceID);
    BlipGpu b = u_blips[idx];

    vec2 center_px = b.pos / 8.0;
    float r = max(b.radius_px, 2.0);
    float half_size = r + 1.0;

    vec2 corner = CORNERS[gl_VertexID % 6];
    vec2 pos_px = center_px + corner * half_size;

    vec2 ndc = (pos_px / 512.0) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);

    v_local_px = corner * half_size;
    v_r = r;
    v_color = unpackUnorm4x8(b.rgba).rgb;
}
