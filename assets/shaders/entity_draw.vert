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

layout(binding = TEX_VAT) uniform sampler2D u_vat;

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_world_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) flat out uint v_owner;
layout(location = 4) flat out uint v_flags;

vec3 decode_oct(vec2 e) {
    vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0) {
        v.xy = (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(v);
}

void main() {
    uint inst_idx = b_visible[gl_BaseInstance + gl_InstanceID];
    EntityInstance inst = b_instances[inst_idx];

    uint model_id = inst.ids & 0xFFFFu;
    uint owner    = (inst.ids >> 16) & 0x0Fu;
    uint flags    = (inst.ids >> 20) & 0xFFu;

    v_owner = owner;
    v_flags = flags;
    v_uv    = a_uv;

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
    v_world_pos = world_p.xyz;

    // Normal transformation
    vec3 n_oct = decode_oct(a_normal);
    vec3 n_posed = vec3(
        dot(part_r0.xyz, n_oct),
        dot(part_r1.xyz, n_oct),
        dot(part_r2.xyz, n_oct)
    );
    v_world_normal = normalize(entity_rotate_y(yaw, n_posed));

    gl_Position = u_view_proj * world_p;
}
