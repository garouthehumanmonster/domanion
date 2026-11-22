#include "sim_world.hpp"

namespace ron {

Result<void> SimWorld::init(MemoryArena& match_arena, u64 match_seed) noexcept {
    state_ = match_arena.alloc<SimState>(1);
    if (state_ == nullptr) {
        return Result<void>::err(ErrorCode::OutOfMemory);
    }

    state_->magic           = 0x534D3236u; // 'SM26'
    state_->current_tick    = 0;
    state_->match_seed      = match_seed;
    state_->tick_rng        = DeterministicRng(match_seed);
    state_->total_spawned   = 0;
    state_->total_destroyed = 0;
    std::memset(state_->pad_header, 0, sizeof(state_->pad_header));
    state_->entities.init();

    auto cmd_res = cmd_bus_.init(match_arena);
    if (!cmd_res.is_ok()) {
        return cmd_res;
    }

    tick_graph_.reset();

    // Stage 0: Sim_BeginTick
    auto n0 = tick_graph_.add_node("Sim_BeginTick", this, [](void* user_ctx, JobSystem& js, MemoryArena&) noexcept {
        auto* world = static_cast<SimWorld*>(user_ctx);
        const u32 count = world->state_->entities.active_count;
        if (count == 0) return;

        js.parallel_for_deterministic(count, 256, [world](u32 begin, u32 end, u32, MemoryArena&) noexcept {
            EntityStore& entities = world->state_->entities;
            for (u32 d = begin; d < end; ++d) {
                entities.prev_pos[d] = entities.curr_pos[d];
                entities.prev_heading[d] = entities.curr_heading[d];

                if (entities.weapon_cooldown_ticks[d] > 0) {
                    entities.weapon_cooldown_ticks[d]--;
                }

                if (entities.aps_charges[d] < entities.aps_max_charges[d]) {
                    entities.aps_reload_timer[d]++;
                    if (entities.aps_reload_timer[d] >= entities.aps_reload_period[d]) {
                        entities.aps_charges[d]++;
                        entities.aps_reload_timer[d] = 0;
                    }
                }

                entities.anim_state_ticks[d]++;
            }
        });
    });
    if (!n0.is_ok()) return Result<void>::err(n0.error);

    // Stage 1: Sim_CommandIngest
    auto n1 = tick_graph_.add_node("Sim_CommandIngest", this, [](void* user_ctx, JobSystem&, MemoryArena& scratch) noexcept {
        auto* world = static_cast<SimWorld*>(user_ctx);
        auto cmds = world->cmd_bus_.extract_sorted_for_tick(world->state_->current_tick, scratch);
        EntityStore& entities = world->state_->entities;

        for (const auto& cmd : cmds) {
            switch (cmd.type) {
                case CommandType::SpawnEntity: {
                    SpawnParams p{
                        .archetype_id = cmd.param_id,
                        .player_id = cmd.player_id,
                        .domain = MovementDomain::Land_Infantry,
                        .pos = Vec3fx::from_xz(cmd.target_xz, fx32::zero()),
                        .heading = Angle16{0},
                        .move_speed = fx32::from_int(8),
                        .radius = fx32::one(),
                        .max_hp = 100,
                        .aps_max_charges = cmd.sub_param,
                        .aps_reload_ticks = 200,
                        .max_capacitor = 0
                    };
                    auto res = entities.spawn(p);
                    if (res.is_ok()) {
                        world->state_->total_spawned++;
                    }
                    break;
                }
                case CommandType::DestroyEntity: {
                    entities.mark_for_destroy(cmd.subject);
                    break;
                }
                case CommandType::Move:
                case CommandType::AttackMove: {
                    u32 d = entities.resolve_dense(cmd.subject);
                    if (d != INVALID_DENSE_IDX) {
                        entities.current_order[d] = static_cast<u8>(
                            cmd.type == CommandType::Move ? OrderType::Move : OrderType::AttackMove);
                        entities.target_pos_xz[d] = cmd.target_xz;
                        entities.flags[d] |= EntityFlags::Moving;
                    }
                    break;
                }
                case CommandType::AttackTarget: {
                    u32 d = entities.resolve_dense(cmd.subject);
                    if (d != INVALID_DENSE_IDX) {
                        if (entities.is_alive(cmd.target)) {
                            u32 td = entities.resolve_dense(cmd.target);
                            entities.current_order[d] = static_cast<u8>(OrderType::AttackTarget);
                            entities.target_entity[d] = cmd.target;
                            entities.target_pos_xz[d] = entities.curr_pos[td].xz();
                            entities.flags[d] |= (EntityFlags::Moving | EntityFlags::Attacking);
                        }
                    }
                    break;
                }
                case CommandType::Stop: {
                    u32 d = entities.resolve_dense(cmd.subject);
                    if (d != INVALID_DENSE_IDX) {
                        entities.current_order[d] = static_cast<u8>(OrderType::Idle);
                        entities.velocity_xz[d] = {};
                        entities.flags[d] &= ~(EntityFlags::Moving | EntityFlags::Attacking);
                    }
                    break;
                }
                case CommandType::SetStance: {
                    u32 d = entities.resolve_dense(cmd.subject);
                    if (d != INVALID_DENSE_IDX) {
                        entities.stance[d] = cmd.sub_param;
                    }
                    break;
                }
                default:
                    break;
            }
        }
    });
    if (!n1.is_ok()) return Result<void>::err(n1.error);

    // Stage 2: Sim_Kinematics
    auto n2 = tick_graph_.add_node("Sim_Kinematics", this, [](void* user_ctx, JobSystem& js, MemoryArena&) noexcept {
        auto* world = static_cast<SimWorld*>(user_ctx);
        const u32 count = world->state_->entities.active_count;
        if (count == 0) return;

        js.parallel_for_deterministic(count, 128, [world](u32 begin, u32 end, u32, MemoryArena&) noexcept {
            EntityStore& entities = world->state_->entities;
            for (u32 d = begin; d < end; ++d) {
                const u8 order = entities.current_order[d];
                if (order == static_cast<u8>(OrderType::Move) ||
                    order == static_cast<u8>(OrderType::AttackMove) ||
                    order == static_cast<u8>(OrderType::AttackTarget)) {

                    if (order == static_cast<u8>(OrderType::AttackTarget)) {
                        EntityHandle th = entities.target_entity[d];
                        if (entities.is_alive(th)) {
                            u32 td = entities.resolve_dense(th);
                            entities.target_pos_xz[d] = entities.curr_pos[td].xz();
                        }
                    }

                    const Vec2fx delta = entities.target_pos_xz[d] - entities.curr_pos[d].xz();
                    const fx32 dist = delta.length();
                    const fx32 step = integrate_dt_20hz(entities.move_speed[d]);

                    if (dist.raw > 0) {
                        const Angle16 target_heading = Vec2fx::heading_of(delta);
                        entities.curr_heading[d] = turn_towards_bam(entities.curr_heading[d], target_heading, 4096u);
                    }

                    if (dist <= step) {
                        entities.curr_pos[d].x = entities.target_pos_xz[d].x;
                        entities.curr_pos[d].z = entities.target_pos_xz[d].z;
                        entities.velocity_xz[d] = {};
                        entities.flags[d] &= ~EntityFlags::Moving;
                        if (order == static_cast<u8>(OrderType::Move)) {
                            entities.current_order[d] = static_cast<u8>(OrderType::Idle);
                        }
                    } else {
                        const Vec2fx dir = normalize_safe(delta);
                        entities.velocity_xz[d] = dir * entities.move_speed[d];
                        const fx32 move_x = integrate_dt_20hz(entities.velocity_xz[d].x);
                        const fx32 move_z = integrate_dt_20hz(entities.velocity_xz[d].z);
                        entities.curr_pos[d].x = clamp_fx(entities.curr_pos[d].x + move_x, fx32::zero(), fx32::from_int(4095));
                        entities.curr_pos[d].z = clamp_fx(entities.curr_pos[d].z + move_z, fx32::zero(), fx32::from_int(4095));
                    }
                }
            }
        });
    });
    if (!n2.is_ok()) return Result<void>::err(n2.error);

    // Stage 3: Sim_TargetCleanupAndSweep
    auto n3 = tick_graph_.add_node("Sim_TargetCleanupAndSweep", this, [](void* user_ctx, JobSystem& js, MemoryArena&) noexcept {
        auto* world = static_cast<SimWorld*>(user_ctx);
        const u32 count = world->state_->entities.active_count;

        if (count > 0) {
            js.parallel_for_deterministic(count, 256, [world](u32 begin, u32 end, u32, MemoryArena&) noexcept {
                EntityStore& entities = world->state_->entities;
                for (u32 d = begin; d < end; ++d) {
                    EntityHandle th = entities.target_entity[d];
                    if (!th.is_null()) {
                        if (!entities.is_alive(th) || ((entities.flags[entities.resolve_dense(th)] & EntityFlags::PendingDestroy) != 0)) {
                            entities.target_entity[d] = NULL_ENTITY;
                            if (entities.current_order[d] == static_cast<u8>(OrderType::AttackTarget)) {
                                entities.current_order[d] = static_cast<u8>(OrderType::Idle);
                                entities.flags[d] &= ~(EntityFlags::Attacking | EntityFlags::Moving);
                                entities.velocity_xz[d] = {};
                            }
                        }
                    }
                }
            });
        }

        world->state_->total_destroyed += world->state_->entities.sweep_pending_destroys();
        world->state_->current_tick += 1u;
        world->state_->tick_rng.next_u64();
    });
    if (!n3.is_ok()) return Result<void>::err(n3.error);

    auto d0 = tick_graph_.add_dependency(n0.value, n1.value);
    if (!d0.is_ok()) return d0;
    auto d1 = tick_graph_.add_dependency(n1.value, n2.value);
    if (!d1.is_ok()) return d1;
    auto d2 = tick_graph_.add_dependency(n2.value, n3.value);
    if (!d2.is_ok()) return d2;

    return tick_graph_.compile();
}

void SimWorld::step_tick(JobSystem& js, Profiler* profiler) noexcept {
    tick_graph_.execute(js, profiler);
}

u64 SimWorld::compute_state_hash() const noexcept {
    RON_ALWAYS_ASSERT(state_ != nullptr, "SimState not initialized");
    return xxh64(std::span<const u8>{reinterpret_cast<const u8*>(state_), sizeof(SimState)}, state_->match_seed);
}

Result<std::span<u8>> SimWorld::save_snapshot_lz4(MemoryArena& dst_arena) const noexcept {
    RON_ALWAYS_ASSERT(state_ != nullptr, "SimState not initialized");
    return lz4_compress_to_arena(
        std::span<const u8>{reinterpret_cast<const u8*>(state_), sizeof(SimState)},
        dst_arena
    );
}

Result<void> SimWorld::load_snapshot_lz4(std::span<const u8> compressed_snapshot, MemoryArena& scratch) noexcept {
    RON_ALWAYS_ASSERT(state_ != nullptr, "SimState not initialized");
    auto decomp = lz4_decompress_to_arena(compressed_snapshot, scratch);
    if (!decomp.is_ok()) {
        return Result<void>::err(decomp.error);
    }

    if (decomp.value.size() != sizeof(SimState)) {
        return Result<void>::err(ErrorCode::CorruptData);
    }

    const auto* loaded = reinterpret_cast<const SimState*>(decomp.value.data());
    if (loaded->magic != 0x534D3236u) {
        return Result<void>::err(ErrorCode::InvalidMagic);
    }

    std::memcpy(state_, loaded, sizeof(SimState));
    return Result<void>::ok();
}

} // namespace ron
