#include "entity_soa.hpp"

namespace ron {

void EntityStore::init() noexcept {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
    std::memset(this, 0, sizeof(EntityStore));
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    active_count = 0;
    free_top = MAX_ENTITIES;

    for (u32 i = 0; i < MAX_ENTITIES; ++i) {
        sparse_to_dense[i] = INVALID_DENSE_IDX;
        slot_generation[i] = 1u;
        free_slots[i] = (MAX_ENTITIES - 1u) - i;
    }
}

bool EntityStore::is_alive(EntityHandle h) const noexcept {
    if (h.is_null()) return false;
    const u32 slot = h.slot();
    if (slot >= MAX_ENTITIES) return false;
    if (slot_generation[slot] != h.generation()) return false;
    const u32 d = sparse_to_dense[slot];
    return (d < active_count) && (handle[d] == h) && ((flags[d] & EntityFlags::Alive) != 0);
}

u32 EntityStore::resolve_dense(EntityHandle h) const noexcept {
    if (!is_alive(h)) return INVALID_DENSE_IDX;
    return sparse_to_dense[h.slot()];
}

Result<EntityHandle> EntityStore::spawn(const SpawnParams& p) noexcept {
    if (free_top == 0 || active_count >= MAX_ENTITIES) {
        return Result<EntityHandle>::err(ErrorCode::OutOfMemory);
    }

    const u32 slot = free_slots[--free_top];
    const u32 d = active_count++;
    const u16 gen = slot_generation[slot];
    const EntityHandle h = EntityHandle::pack(slot, gen);

    sparse_to_dense[slot] = d;

    handle[d]                = h;
    archetype_id[d]          = p.archetype_id;
    player_id[d]             = p.player_id;
    domain[d]                = static_cast<u8>(p.domain);
    flags[d]                 = EntityFlags::Alive;
    prev_pos[d]              = p.pos;
    curr_pos[d]              = p.pos;
    velocity_xz[d]           = {};
    prev_heading[d]          = p.heading;
    curr_heading[d]          = p.heading;
    turret_yaw[d]            = p.heading;
    barrel_pitch[d]          = {0};
    move_speed[d]            = p.move_speed;
    radius[d]                = p.radius;
    hp[d]                    = p.max_hp;
    max_hp[d]                = p.max_hp;
    aps_charges[d]           = p.aps_max_charges;
    aps_max_charges[d]       = p.aps_max_charges;
    aps_reload_timer[d]      = 0;
    aps_reload_period[d]     = p.aps_reload_ticks;
    capacitor_energy[d]      = p.max_capacitor;
    current_order[d]         = static_cast<u8>(OrderType::Idle);
    stance[d]                = static_cast<u8>(StanceType::Aggressive);
    target_entity[d]         = NULL_ENTITY;
    target_pos_xz[d]         = p.pos.xz();
    weapon_cooldown_ticks[d] = 0;
    anim_state_ticks[d]      = 0;

    return Result<EntityHandle>::ok(h);
}

bool EntityStore::mark_for_destroy(EntityHandle h) noexcept {
    const u32 d = resolve_dense(h);
    if (d == INVALID_DENSE_IDX) return false;
    flags[d] |= EntityFlags::PendingDestroy;
    return true;
}

void EntityStore::clear_dense_slot(u32 d) noexcept {
    handle[d]                = NULL_ENTITY;
    archetype_id[d]          = 0;
    player_id[d]             = 0;
    domain[d]                = 0;
    flags[d]                 = 0;
    prev_pos[d]              = {};
    curr_pos[d]              = {};
    velocity_xz[d]           = {};
    prev_heading[d]          = {0};
    curr_heading[d]          = {0};
    turret_yaw[d]            = {0};
    barrel_pitch[d]          = {0};
    move_speed[d]            = {};
    radius[d]                = {};
    hp[d]                    = 0;
    max_hp[d]                = 0;
    aps_charges[d]           = 0;
    aps_max_charges[d]       = 0;
    aps_reload_timer[d]      = 0;
    aps_reload_period[d]     = 0;
    capacitor_energy[d]      = 0;
    current_order[d]         = 0;
    stance[d]                = 0;
    target_entity[d]         = NULL_ENTITY;
    target_pos_xz[d]         = {};
    weapon_cooldown_ticks[d] = 0;
    anim_state_ticks[d]      = 0;
}

void EntityStore::copy_dense_slot(u32 dst, u32 src) noexcept {
    handle[dst]                = handle[src];
    archetype_id[dst]          = archetype_id[src];
    player_id[dst]             = player_id[src];
    domain[dst]                = domain[src];
    flags[dst]                 = flags[src];
    prev_pos[dst]              = prev_pos[src];
    curr_pos[dst]              = curr_pos[src];
    velocity_xz[dst]           = velocity_xz[src];
    prev_heading[dst]          = prev_heading[src];
    curr_heading[dst]          = curr_heading[src];
    turret_yaw[dst]            = turret_yaw[src];
    barrel_pitch[dst]          = barrel_pitch[src];
    move_speed[dst]            = move_speed[src];
    radius[dst]                = radius[src];
    hp[dst]                    = hp[src];
    max_hp[dst]                = max_hp[src];
    aps_charges[dst]           = aps_charges[src];
    aps_max_charges[dst]       = aps_max_charges[src];
    aps_reload_timer[dst]      = aps_reload_timer[src];
    aps_reload_period[dst]     = aps_reload_period[src];
    capacitor_energy[dst]      = capacitor_energy[src];
    current_order[dst]         = current_order[src];
    stance[dst]                = stance[src];
    target_entity[dst]         = target_entity[src];
    target_pos_xz[dst]         = target_pos_xz[src];
    weapon_cooldown_ticks[dst] = weapon_cooldown_ticks[src];
    anim_state_ticks[dst]      = anim_state_ticks[src];
}

void EntityStore::destroy_at_dense(u32 d) noexcept {
    RON_ALWAYS_ASSERT(d < active_count, "destroy_at_dense out of bounds");
    const EntityHandle victim_h = handle[d];
    const u32 victim_slot = victim_h.slot();

    u16 next_gen = static_cast<u16>((slot_generation[victim_slot] + 1u) & 0x0FFFu);
    if (next_gen == 0u) next_gen = 1u;
    slot_generation[victim_slot] = next_gen;

    sparse_to_dense[victim_slot] = INVALID_DENSE_IDX;
    free_slots[free_top++] = victim_slot;

    const u32 last = active_count - 1u;
    if (d != last) {
        copy_dense_slot(d, last);
        sparse_to_dense[handle[d].slot()] = d;
    }
    clear_dense_slot(last);
    active_count = last;
}

u32 EntityStore::sweep_pending_destroys() noexcept {
    u32 count = 0;
    u32 d = 0;
    while (d < active_count) {
        if ((flags[d] & EntityFlags::PendingDestroy) != 0 || hp[d] <= 0) {
            destroy_at_dense(d);
            count++;
        } else {
            ++d;
        }
    }
    return count;
}

Vec3fx EntityStore::interpolate_pos(u32 d, fx32 alpha_0_to_1) const noexcept {
    RON_ALWAYS_ASSERT(d < active_count, "interpolate_pos out of bounds");
    return Vec3fx::lerp(prev_pos[d], curr_pos[d], alpha_0_to_1);
}

Angle16 EntityStore::interpolate_heading(u32 d, fx32 alpha_0_to_1) const noexcept {
    RON_ALWAYS_ASSERT(d < active_count, "interpolate_heading out of bounds");
    const i16 delta = shortest_delta_bam(prev_heading[d], curr_heading[d]);
    const i32 scaled = static_cast<i32>((static_cast<i64>(delta) * alpha_0_to_1.raw + 32768LL) >> 16);
    return prev_heading[d] + Angle16{static_cast<u16>(scaled)};
}

} // namespace ron
