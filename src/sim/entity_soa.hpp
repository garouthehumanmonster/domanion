#pragma once

#include "entity_handle.hpp"
#include "core/result.hpp"
#include "core/assert_log.hpp"
#include <cstring>

namespace ron {

// -------------------------------------------------------------------------
// Spawn Parameters POD
// -------------------------------------------------------------------------
struct SpawnParams {
    u16            archetype_id{0};
    u8             player_id{0};
    MovementDomain domain{MovementDomain::Land_Infantry};
    Vec3fx         pos{};
    Angle16        heading{0};
    fx32           move_speed{fx32::from_int(5)};
    fx32           radius{fx32::one()};
    i32            max_hp{100};
    u8             aps_max_charges{0};
    u16            aps_reload_ticks{200};
    i32            max_capacitor{0};
};

// -------------------------------------------------------------------------
// EntityStore: Pointer-Free Dense SoA + Sparse Generational Table
// -------------------------------------------------------------------------
struct RON_CACHE_ALIGN EntityStore {
    // Metadata
    u32 active_count{0};
    u32 free_top{0};

    // Sparse Indirection Arrays
    u32 sparse_to_dense[MAX_ENTITIES];
    u16 slot_generation[MAX_ENTITIES];
    u32 free_slots[MAX_ENTITIES];

    // Dense Active Arrays [0, active_count)
    EntityHandle handle[MAX_ENTITIES];
    u16          archetype_id[MAX_ENTITIES];
    u8           player_id[MAX_ENTITIES];
    u8           domain[MAX_ENTITIES];
    u32          flags[MAX_ENTITIES];
    Vec3fx       prev_pos[MAX_ENTITIES];
    Vec3fx       curr_pos[MAX_ENTITIES];
    Vec2fx       velocity_xz[MAX_ENTITIES];
    Angle16      prev_heading[MAX_ENTITIES];
    Angle16      curr_heading[MAX_ENTITIES];
    Angle16      turret_yaw[MAX_ENTITIES];
    Angle16      barrel_pitch[MAX_ENTITIES];
    fx32         move_speed[MAX_ENTITIES];
    fx32         radius[MAX_ENTITIES];
    i32          hp[MAX_ENTITIES];
    i32          max_hp[MAX_ENTITIES];
    u8           aps_charges[MAX_ENTITIES];
    u8           aps_max_charges[MAX_ENTITIES];
    u16          aps_reload_timer[MAX_ENTITIES];
    u16          aps_reload_period[MAX_ENTITIES];
    i32          capacitor_energy[MAX_ENTITIES];
    u8           current_order[MAX_ENTITIES];
    u8           stance[MAX_ENTITIES];
    EntityHandle target_entity[MAX_ENTITIES];
    Vec2fx       target_pos_xz[MAX_ENTITIES];
    u16          weapon_cooldown_ticks[MAX_ENTITIES];
    u16          anim_state_ticks[MAX_ENTITIES];

    void init() noexcept;
    bool is_alive(EntityHandle h) const noexcept;
    u32 resolve_dense(EntityHandle h) const noexcept;
    Result<EntityHandle> spawn(const SpawnParams& p) noexcept;
    bool mark_for_destroy(EntityHandle h) noexcept;
    void destroy_at_dense(u32 d) noexcept;
    u32 sweep_pending_destroys() noexcept;

    void clear_dense_slot(u32 d) noexcept;
    void copy_dense_slot(u32 dst, u32 src) noexcept;

    Vec3fx interpolate_pos(u32 d, fx32 alpha_0_to_1) const noexcept;
    Angle16 interpolate_heading(u32 d, fx32 alpha_0_to_1) const noexcept;
};

static_assert(std::is_trivially_copyable_v<EntityStore>);

} // namespace ron
