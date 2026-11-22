#version 460 core

#include "common/frame.glsl"
#include "entity_common.glsl"

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in uint a_part;

layout(std430, binding = BIND_SSBO_VISIBLE) readonly buffer VisibleBlock {
    uint b_visible[];
};

layout(std430, binding = BIND_SSBO_INSTANCES) readonly buffer InstancesBlock {
    EntityInstance b_instances[];
};

layout(std430, binding = BIND_SSBO_MODELS) readonly buffer ModelsBlock {
    ModelGpu b_models[];
};

layout(std140, binding = BIND_UBO_LIGHT) uniform LightingBlock {
    mat4 u_light_vp[4];
    vec4 u_cascade_split;
    vec4 u_shadow_texel;
    vec4 u_inv_range;
    vec4 u_cam_fwd;
    vec4 u_ambient_sky;
    vec4 u_ambient_ground;
    vec4 u_shadow_p;
};

layout(binding = TEX_VAT) uniform sampler2D u_vat;

layout(location = 0) uniform int u_cascade;

void main() {
    uint inst_idx = b_visible[gl_BaseInstance + gl_InstanceID];
    EntityInstance inst = b_instances[inst_idx];

    uint model_id = inst.ids & 0xFFFFu;

    // Interpolate transform
    float alpha = u_cam_alpha.w;
    vec3 pos = mix(inst.pos_yaw_prev.xyz, inst.pos_yaw_curr.xyz, alpha);
    float yaw = lerp_yaw(inst.pos_yaw_prev.w, inst.pos_yaw_curr.w, alpha);
    float scale = inst.scale;

    // Sample VAT part matrix
    uint clip_id = min(inst.clip, 3u);
    ClipDesc cd = ENTITY_CLIPS[clip_id];
    float fps = (u_time_p.y > 0.0) ? u_time_p.y : RON_ANIM_FPS;
    float f = inst.anim_t * fps;
    float f_clamped = (cd.loop != 0u) ? mod(f, float(cd.count)) : clamp(f, 0.0, float(cd.count - 1u));
    if (f_clamped < 0.0) f_clamped += float(cd.count);
    float f0 = floor(f_clamped);
    float fr = f_clamped - f0;
    uint u_f0 = uint(f0);
    uint u_f1 = (cd.loop != 0u) ? ((u_f0 + 1u) % cd.count) : min(u_f0 + 1u, cd.count - 1u);

    uint row_base = b_models[model_id].vat_row_base;
    int row0 = int(row_base + cd.start + u_f0);
    int row1 = int(row_base + cd.start + u_f1);

    int col0 = int(a_part * 3u);
    vec4 part_r0 = mix(texelFetch(u_vat, ivec2(col0 + 0, row0), 0), texelFetch(u_vat, ivec2(col0 + 0, row1), 0), fr);
    vec4 part_r1 = mix(texelFetch(u_vat, ivec2(col0 + 1, row0), 0), texelFetch(u_vat, ivec2(col0 + 1, row1), 0), fr);
    vec4 part_r2 = mix(texelFetch(u_vat, ivec2(col0 + 2, row0), 0), texelFetch(u_vat, ivec2(col0 + 2, row1), 0), fr);

    // Apply part matrix to a_pos
    vec4 pos4 = vec4(a_pos, 1.0);
    vec3 posed_pos = vec3(
        dot(part_r0, pos4),
        dot(part_r1, pos4),
        dot(part_r2, pos4)
    );

    vec4 world_p = entity_model_matrix(pos, yaw, scale) * vec4(posed_pos, 1.0);

    int cascade_idx = clamp(u_cascade, 0, 3);
    gl_Position = u_light_vp[cascade_idx] * world_p;
}
