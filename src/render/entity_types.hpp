#pragma once

#include "core/types.hpp"
#include <type_traits>
#include <cstddef>

namespace ron::render {

// §1 Constants
constexpr u32   MAX_INSTANCES  = 65536;
constexpr u32   MAX_MODELS     = 128;
constexpr u32   LODS_PER_MODEL = 3;
constexpr u32   MAX_BUCKETS    = MAX_MODELS * LODS_PER_MODEL; // 384
constexpr float CULL_PX        = 1.5f;
constexpr u32   VAT_FRAMES     = 44;
constexpr u32   VAT_COLS       = 48; // 16 parts x 3 texels
constexpr float ANIM_FPS       = 16.0f;

// Tunables (§1)
constexpr float LOD0_PX        = 48.0f; // allowed 48..96
constexpr float LOD1_PX        = 16.0f; // allowed 16..32

// Clips (§1)
// 0 Idle {start 0, count 8, loop 1}
// 1 Move {start 8, count 16, loop 1}
// 2 Attack {start 24, count 8, loop 0}
// 3 Die {start 32, count 12, loop 0}
struct ClipDesc {
    u32 start;
    u32 count;
    u32 loop;
};

constexpr ClipDesc ENTITY_CLIPS[4] = {
    {0, 8, 1},
    {8, 16, 1},
    {24, 8, 0},
    {32, 12, 0}
};

// Flags (§1): FLAG_SELECTED = 1<<0, FLAG_HIT_FLASH = 1<<1 (flags occupy ids bits 20..27)
constexpr u32 FLAG_SELECTED  = 1u << 0;
constexpr u32 FLAG_HIT_FLASH = 1u << 1;

// Packing helpers: model_id bits 0..15, owner bits 16..19, flags bits 20..27, bits 28..31 zero
inline constexpr u32 entity_pack_ids(u32 model, u32 owner, u32 flags) noexcept {
    return (model & 0xFFFFu) | ((owner & 0x0Fu) << 16) | ((flags & 0xFFu) << 20);
}

inline constexpr u32 entity_model_id(u32 ids) noexcept {
    return ids & 0xFFFFu;
}

inline constexpr u32 entity_owner_id(u32 ids) noexcept {
    return (ids >> 16) & 0x0Fu;
}

inline constexpr u32 entity_flags(u32 ids) noexcept {
    return (ids >> 20) & 0xFFu;
}

// -----------------------------------------------------------------------------
// EntityInstance (render-side, std430, 64 B, trivially copyable)
// -----------------------------------------------------------------------------
struct alignas(16) EntityInstance {
    float pos_prev[3]; // offset 0
    float yaw_prev;    // offset 12 (yaw radians, 0 = north (-Z), +pi/2 = east (+X))
    float pos_curr[3]; // offset 16
    float yaw_curr;    // offset 28
    u32   ids;         // offset 32: model_id bits 0..15, owner bits 16..19, flags bits 20..27, bits 28..31 zero
    u32   clip;        // offset 36: 0..3
    float anim_t;      // offset 40: seconds since clip start
    float scale;       // offset 44: uniform, > 0
    float hp;          // offset 48: reserved (Gate 13), 0..1
    u32   tint;        // offset 52: reserved, 0
    u32   r0;          // offset 56: reserved, 0
    u32   r1;          // offset 60: reserved, 0

    u32 model() const noexcept { return entity_model_id(ids); }
    u32 owner() const noexcept { return entity_owner_id(ids); }
    u32 flags() const noexcept { return entity_flags(ids); }
};

static_assert(sizeof(EntityInstance) == 64);
static_assert(alignof(EntityInstance) == 16);
static_assert(std::is_trivially_copyable_v<EntityInstance>);
static_assert(offsetof(EntityInstance, pos_prev) == 0);
static_assert(offsetof(EntityInstance, yaw_prev) == 12);
static_assert(offsetof(EntityInstance, pos_curr) == 16);
static_assert(offsetof(EntityInstance, yaw_curr) == 28);
static_assert(offsetof(EntityInstance, ids) == 32);
static_assert(offsetof(EntityInstance, clip) == 36);
static_assert(offsetof(EntityInstance, anim_t) == 40);
static_assert(offsetof(EntityInstance, scale) == 44);
static_assert(offsetof(EntityInstance, hp) == 48);
static_assert(offsetof(EntityInstance, tint) == 52);
static_assert(offsetof(EntityInstance, r0) == 56);
static_assert(offsetof(EntityInstance, r1) == 60);

// -----------------------------------------------------------------------------
// ModelGpu (std430, 80 B)
// uvec4 lod[3] {index_count, first_index, base_vertex (int bits), 0};
// vec4 sphere {cx,cy,cz,radius} (model space, encloses LOD0 geometry in ALL 44 VAT poses, padded x1.02);
// uvec4 vat {row_base, 0,0,0}.
// -----------------------------------------------------------------------------
struct alignas(16) ModelGpu {
    struct LodCmd {
        u32 index_count;
        u32 first_index;
        i32 base_vertex;
        u32 pad;
    } lod[3];             // offset 0 (3 * 16 = 48 B)
    float sphere[4];      // offset 48: cx, cy, cz, radius (16 B)
    u32   vat_row_base;   // offset 64
    u32   vat_pad[3];     // offset 68 (12 B)
};

static_assert(sizeof(ModelGpu) == 80);
static_assert(alignof(ModelGpu) == 16);
static_assert(std::is_trivially_copyable_v<ModelGpu>);
static_assert(offsetof(ModelGpu, lod) == 0);
static_assert(offsetof(ModelGpu, sphere) == 48);
static_assert(offsetof(ModelGpu, vat_row_base) == 64);

// -----------------------------------------------------------------------------
// DrawCmd = GL DrawElementsIndirectCommand (20 B, tightly packed)
// -----------------------------------------------------------------------------
#pragma pack(push, 1)
struct DrawCmd {
    u32 count;
    u32 instance_count;
    u32 first_index;
    i32 base_vertex;
    u32 base_instance;
};
#pragma pack(pop)

static_assert(sizeof(DrawCmd) == 20);
static_assert(std::is_trivially_copyable_v<DrawCmd>);
static_assert(offsetof(DrawCmd, count) == 0);
static_assert(offsetof(DrawCmd, instance_count) == 4);
static_assert(offsetof(DrawCmd, first_index) == 8);
static_assert(offsetof(DrawCmd, base_vertex) == 12);
static_assert(offsetof(DrawCmd, base_instance) == 16);

// -----------------------------------------------------------------------------
// EntityUbo (std140, 160 B)
// vec4 planes[6] (normalised, inside = dot(n,p)+d >= 0; order L,R,B,T,N,F; xyz=n, w=d);
// vec4 cam_alpha {cam_pos xyz, alpha};
// vec4 cull_p {proj_scale_px, LOD0_PX, LOD1_PX, CULL_PX};
// uvec4 counts {instance_count, model_count, bucket_count (=model_count*3), 0};
// vec4 time_p {time_s, ANIM_FPS, 0, 0}.
// -----------------------------------------------------------------------------
struct alignas(16) EntityUbo {
    float planes[6][4];  // offset 0..95 (96 B)
    float cam_alpha[4];  // offset 96: cam_pos.xyz, alpha
    float cull_p[4];     // offset 112: proj_scale_px, LOD0_PX, LOD1_PX, CULL_PX
    u32   counts[4];     // offset 128: instance_count, model_count, bucket_count, 0
    float time_p[4];     // offset 144: time_s, ANIM_FPS, 0, 0
};

static_assert(sizeof(EntityUbo) == 160);
static_assert(alignof(EntityUbo) == 16);
static_assert(std::is_trivially_copyable_v<EntityUbo>);
static_assert(offsetof(EntityUbo, planes) == 0);
static_assert(offsetof(EntityUbo, cam_alpha) == 96);
static_assert(offsetof(EntityUbo, cull_p) == 112);
static_assert(offsetof(EntityUbo, counts) == 128);
static_assert(offsetof(EntityUbo, time_p) == 144);

} // namespace ron::render
