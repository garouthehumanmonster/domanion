// Shared include for entity pipeline shaders
#ifndef ENTITY_COMMON_GLSL
#define ENTITY_COMMON_GLSL

// Bindings and defines from gl_bindings.hpp are injected by ShaderPreproc:
// BIND_UBO_ENTITY (4), BIND_SSBO_INSTANCES (0), BIND_SSBO_VISIBLE (1),
// BIND_SSBO_INDIRECT (2), BIND_SSBO_COUNTERS (3), BIND_SSBO_MODELS (9),
// BIND_SSBO_BUCKETS (10), TEX_VAT (14), TEX_TRIM_ALBEDO (15), etc.

struct EntityInstance {
    vec4 pos_yaw_prev; // xyz = pos_prev, w = yaw_prev
    vec4 pos_yaw_curr; // xyz = pos_curr, w = yaw_curr
    uint ids;          // bits 0..15 model_id, 16..19 owner, 20..27 flags
    uint clip;         // 0..3
    float anim_t;      // seconds
    float scale;
    float hp;          // reserved
    uint tint;         // reserved
    uint r0;
    uint r1;
};

struct LodCmd {
    uint index_count;
    uint first_index;
    int  base_vertex;
    uint pad;
};

struct ModelGpu {
    LodCmd lod[3];     // offset 0 (48 B)
    vec4 sphere;       // offset 48: cx, cy, cz, radius
    uint vat_row_base; // offset 64
    uint vat_pad[3];   // offset 68 (12 B)
};

struct DrawCmd {
    uint count;
    uint instanceCount;
    uint firstIndex;
    int  baseVertex;
    uint baseInstance;
};

layout(std140, binding = BIND_UBO_ENTITY) uniform EntityUboBlock {
    vec4 u_planes[6];      // normalized frustum planes L,R,B,T,N,F: xyz=n, w=d
    vec4 u_cam_alpha;      // xyz = cam_pos, w = alpha
    vec4 u_cull_p;         // x = proj_scale_px, y = LOD0_PX, z = LOD1_PX, w = CULL_PX
    uvec4 u_counts;        // x = instance_count, y = model_count, z = bucket_count, w = 0
    vec4 u_time_p;         // x = time_s, y = ANIM_FPS, z = 0, w = 0
};

const float ENT_PI     = 3.14159265358979323846;
const float ENT_TWO_PI = 6.28318530717958647692;

float wrap_pi(float a) {
    a = mod(a + ENT_PI, ENT_TWO_PI);
    if (a < 0.0) a += ENT_TWO_PI;
    return a - ENT_PI;
}

float lerp_yaw(float prev, float curr, float alpha) {
    float diff = wrap_pi(curr - prev);
    return wrap_pi(prev + diff * alpha);
}

vec3 entity_rotate_y(float yaw, vec3 v) {
    float s = sin(yaw);
    float c = cos(yaw);
    return vec3(s * v.x + c * v.z, v.y, -c * v.x + s * v.z);
}

mat4 entity_model_matrix(vec3 pos, float yaw, float scale) {
    float s = sin(yaw);
    float c = cos(yaw);
    return mat4(
        vec4( s * scale, 0.0, -c * scale, 0.0),
        vec4( 0.0,     scale,  0.0,       0.0),
        vec4( c * scale, 0.0,  s * scale, 0.0),
        vec4( pos,                        1.0)
    );
}

struct ClipDesc {
    uint start;
    uint count;
    uint loop;
};

const ClipDesc ENTITY_CLIPS[4] = ClipDesc[4](
    ClipDesc(0u, 8u, 1u),
    ClipDesc(8u, 16u, 1u),
    ClipDesc(24u, 8u, 1u),
    ClipDesc(32u, 12u, 0u)
);

// Distance fog (must stay identical to terrain; unify in Gate 13)
vec3 entity_distance_fog(vec3 radiance, vec3 world_pos, vec3 cam_pos) {
    float d = length(world_pos - cam_pos);
    float fog = 1.0 - exp(-d * 0.0003);
    return mix(radiance, vec3(0.50, 0.62, 0.80), fog);
}

#endif // ENTITY_COMMON_GLSL
