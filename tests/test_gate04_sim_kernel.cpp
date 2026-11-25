#include "test_framework.hpp"
#include "core/types.hpp"
#include "core/clock.hpp"
#include "core/arena.hpp"
#include "core/hash.hpp"
#include "core/prng.hpp"
#include "core/fixed_math.hpp"
#include "core/fixed_geom.hpp"
#include "core/job_system.hpp"
#include "core/task_graph.hpp"
#include "core/profiler.hpp"
#include "sim/entity_handle.hpp"
#include "sim/entity_soa.hpp"
#include "sim/command_bus.hpp"
#include "sim/sim_world.hpp"

#include <algorithm>
#include <new>

using namespace ron;

// -------------------------------------------------------------------------
// 1. EntityHandle_GenerationalABAAndDanglingTargetCleanup
// -------------------------------------------------------------------------
RON_TEST(EntityHandle_GenerationalABAAndDanglingTargetCleanup) {
    EngineMemory mem;
    RON_EXPECT_TRUE(mem.init_all(1).is_ok());

    JobSystem js;
    RON_EXPECT_TRUE(js.init(1, &mem).is_ok());

    SimWorld world;
    RON_EXPECT_TRUE(world.init(mem.match, 0x12345ULL).is_ok());

    // Spawn Entity A (slot 0, gen 1)
    SpawnParams pa{
        .archetype_id = 10,
        .player_id = 0,
        .pos = Vec3fx{fx32::from_int(100), fx32::zero(), fx32::from_int(100)},
        .move_speed = fx32::from_int(5),
        .max_hp = 100
    };
    auto res_a = world.entities().spawn(pa);
    RON_EXPECT_TRUE(res_a.is_ok());
    const EntityHandle a = res_a.value;
    RON_EXPECT_EQ(a.slot(), 0u);
    RON_EXPECT_EQ(a.generation(), 1u);

    // Spawn Entity B (slot 1, gen 1)
    SpawnParams pb{
        .archetype_id = 20,
        .player_id = 1,
        .pos = Vec3fx{fx32::from_int(200), fx32::zero(), fx32::from_int(200)},
        .move_speed = fx32::from_int(5),
        .max_hp = 100
    };
    auto res_b = world.entities().spawn(pb);
    RON_EXPECT_TRUE(res_b.is_ok());
    const EntityHandle b = res_b.value;
    RON_EXPECT_EQ(b.slot(), 1u);
    RON_EXPECT_EQ(b.generation(), 1u);

    // Order B to AttackTarget A
    SimCommand cmd_attack{
        .execute_tick = 0,
        .subject = b,
        .target = a,
        .type = CommandType::AttackTarget
    };
    RON_EXPECT_TRUE(world.command_bus().enqueue(cmd_attack).is_ok());

    world.step_tick(js);

    const u32 b_idx = world.entities().resolve_dense(b);
    RON_EXPECT_TRUE(b_idx != INVALID_DENSE_IDX);
    RON_EXPECT_EQ(world.entities().target_entity[b_idx].raw, a.raw);
    RON_EXPECT_EQ(world.entities().current_order[b_idx], static_cast<u8>(OrderType::AttackTarget));

    // Destroy Entity A
    SimCommand cmd_destroy{
        .execute_tick = 1,
        .subject = a,
        .type = CommandType::DestroyEntity
    };
    RON_EXPECT_TRUE(world.command_bus().enqueue(cmd_destroy).is_ok());

    world.step_tick(js);

    // Verify A is dead
    RON_EXPECT_TRUE(!world.entities().is_alive(a));
    RON_EXPECT_EQ(world.entities().resolve_dense(a), INVALID_DENSE_IDX);

    // Verify B's dangling target was cleaned up and order reset to Idle
    const u32 b_idx_after = world.entities().resolve_dense(b);
    RON_EXPECT_TRUE(b_idx_after != INVALID_DENSE_IDX);
    RON_EXPECT_EQ(world.entities().target_entity[b_idx_after].raw, NULL_ENTITY.raw);
    RON_EXPECT_EQ(world.entities().current_order[b_idx_after], static_cast<u8>(OrderType::Idle));

    // Spawn Entity C: should reuse slot 0 with generation 2
    SpawnParams pc{
        .archetype_id = 30,
        .player_id = 2,
        .pos = Vec3fx{fx32::from_int(300), fx32::zero(), fx32::from_int(300)},
        .move_speed = fx32::from_int(5),
        .max_hp = 100
    };
    auto res_c = world.entities().spawn(pc);
    RON_EXPECT_TRUE(res_c.is_ok());
    const EntityHandle c = res_c.value;
    RON_EXPECT_EQ(c.slot(), 0u);
    RON_EXPECT_EQ(c.generation(), 2u);
    RON_EXPECT_NE(c.raw, a.raw);

    // Verify A still registers as dead (ABA immunity)
    RON_EXPECT_TRUE(!world.entities().is_alive(a));
    RON_EXPECT_TRUE(world.entities().is_alive(c));

    js.shutdown();
    mem.shutdown_all();
}

// -------------------------------------------------------------------------
// 2. EntityStore_FullCapacity32768ChurnAndZeroTailGarbage
// -------------------------------------------------------------------------
RON_TEST(EntityStore_FullCapacity32768ChurnAndZeroTailGarbage) {
    EngineMemory mem;
    RON_EXPECT_TRUE(mem.init_all(1).is_ok());

    EntityStore* store = mem.match.alloc<EntityStore>(1);
    RON_ALWAYS_ASSERT(store != nullptr, "alloc store failed");
    store->init();

    EntityHandle* handles = mem.match.alloc<EntityHandle>(MAX_ENTITIES);
    RON_ALWAYS_ASSERT(handles != nullptr, "alloc handles failed");

    SpawnParams p{
        .archetype_id = 1,
        .player_id = 0,
        .move_speed = fx32::from_int(10),
        .max_hp = 100
    };

    // Spawn 32,768 entities
    for (u32 i = 0; i < MAX_ENTITIES; ++i) {
        p.pos = Vec3fx{fx32::from_int(static_cast<i32>(i)), fx32::zero(), fx32::from_int(static_cast<i32>(i))};
        auto res = store->spawn(p);
        RON_ALWAYS_ASSERT(res.is_ok(), "Spawn failed under capacity");
        handles[i] = res.value;
    }
    RON_EXPECT_EQ(store->active_count, MAX_ENTITIES);
    RON_EXPECT_EQ(store->free_top, 0u);

    // 32,769th spawn must return OutOfMemory
    auto res_overflow = store->spawn(p);
    RON_EXPECT_TRUE(res_overflow.is_err());
    RON_EXPECT_EQ(res_overflow.error, ErrorCode::OutOfMemory);

    // Destroy 16,384 even entities
    for (u32 i = 0; i < MAX_ENTITIES; i += 2) {
        RON_ALWAYS_ASSERT(store->mark_for_destroy(handles[i]), "mark_for_destroy failed");
    }
    u32 destroyed_first = store->sweep_pending_destroys();
    RON_EXPECT_EQ(destroyed_first, 16384u);
    RON_EXPECT_EQ(store->active_count, 16384u);
    RON_EXPECT_EQ(store->free_top, 16384u);

    // Respawn 16,384 new entities
    for (u32 i = 0; i < 16384; ++i) {
        auto res = store->spawn(p);
        RON_ALWAYS_ASSERT(res.is_ok(), "Respawn failed");
    }
    RON_EXPECT_EQ(store->active_count, MAX_ENTITIES);
    RON_EXPECT_EQ(store->free_top, 0u);

    // Destroy all 32,768 entities
    for (u32 d = 0; d < MAX_ENTITIES; ++d) {
        store->flags[d] |= EntityFlags::PendingDestroy;
    }
    u32 destroyed_all = store->sweep_pending_destroys();
    RON_EXPECT_EQ(destroyed_all, MAX_ENTITIES);
    RON_EXPECT_EQ(store->active_count, 0u);
    RON_EXPECT_EQ(store->free_top, MAX_ENTITIES);

    // Verify that every dense array slot in [0, MAX_ENTITIES) is completely zeroed
    for (u32 i = 0; i < MAX_ENTITIES; ++i) {
        RON_ALWAYS_ASSERT(store->handle[i].is_null(), "handle not zeroed");
        RON_ALWAYS_ASSERT(store->archetype_id[i] == 0, "archetype_id not zeroed");
        RON_ALWAYS_ASSERT(store->player_id[i] == 0, "player_id not zeroed");
        RON_ALWAYS_ASSERT(store->flags[i] == 0, "flags not zeroed");
        RON_ALWAYS_ASSERT(store->hp[i] == 0, "hp not zeroed");
        RON_ALWAYS_ASSERT(store->max_hp[i] == 0, "max_hp not zeroed");
        RON_ALWAYS_ASSERT(store->target_entity[i].is_null(), "target_entity not zeroed");
    }

    mem.shutdown_all();
}

// -------------------------------------------------------------------------
// 3. CommandBus_OutOfOrderIngestionDeterministicSort
// -------------------------------------------------------------------------
RON_TEST(CommandBus_OutOfOrderIngestionDeterministicSort) {
    EngineMemory mem;
    RON_EXPECT_TRUE(mem.init_all(1).is_ok());

    JobSystem js;
    RON_EXPECT_TRUE(js.init(1, &mem).is_ok());

    SimWorld worldA;
    RON_EXPECT_TRUE(worldA.init(mem.match, 0xABCDEFULL).is_ok());

    SimWorld worldB;
    RON_EXPECT_TRUE(worldB.init(mem.match, 0xABCDEFULL).is_ok());

    // Generate 200 commands
    constexpr u32 CMD_COUNT = 200;
    SimCommand cmds[CMD_COUNT];

    DeterministicRng rng(0x99998888ULL);
    for (u32 i = 0; i < CMD_COUNT; ++i) {
        cmds[i] = SimCommand{
            .execute_tick = rng.range_u32(10),
            .sequence_id = i + 1,
            .target_xz = Vec2fx{fx32::from_int(static_cast<i32>(rng.range_u32(1000))),
                                fx32::from_int(static_cast<i32>(rng.range_u32(1000)))},
            .param_id = static_cast<u16>(rng.range_u32(50)),
            .player_id = static_cast<u8>(rng.range_u32(8)),
            .type = CommandType::SpawnEntity,
            .sub_param = 3
        };
    }

    // World A receives forward order
    for (u32 i = 0; i < CMD_COUNT; ++i) {
        RON_EXPECT_TRUE(worldA.command_bus().enqueue(cmds[i]).is_ok());
    }

    // World B receives shuffled order
    SimCommand shuffled[CMD_COUNT];
    std::memcpy(shuffled, cmds, sizeof(cmds));
    for (u32 i = CMD_COUNT - 1; i > 0; --i) {
        u32 j = rng.range_u32(i + 1);
        std::swap(shuffled[i], shuffled[j]);
    }
    for (u32 i = 0; i < CMD_COUNT; ++i) {
        RON_EXPECT_TRUE(worldB.command_bus().enqueue(shuffled[i]).is_ok());
    }

    // Step both worlds 10 ticks
    for (u32 t = 0; t < 10; ++t) {
        worldA.step_tick(js);
        worldB.step_tick(js);
    }

    const u64 hashA = worldA.compute_state_hash();
    const u64 hashB = worldB.compute_state_hash();

    RON_EXPECT_EQ(hashA, hashB);

    js.shutdown();
    mem.shutdown_all();
}

// -------------------------------------------------------------------------
// 4. SimWorld_1WorkerVs8Workers16384EntitiesBitExact
// -------------------------------------------------------------------------
RON_TEST(SimWorld_1WorkerVs8Workers16384EntitiesBitExact) {
    constexpr u32 ENTITY_COUNT = 16384;
    constexpr u32 TICKS = 100;
    constexpr u64 MATCH_SEED = 0xFEDCBA9876543210ULL;

    auto run_match = [](u32 workers) -> u64 {
        EngineMemory mem;
        RON_ALWAYS_ASSERT(mem.init_all(workers).is_ok(), "mem.init_all");

        JobSystem js;
        RON_ALWAYS_ASSERT(js.init(workers, &mem).is_ok(), "js.init");

        SimWorld world;
        RON_ALWAYS_ASSERT(world.init(mem.match, MATCH_SEED).is_ok(), "world.init");

        DeterministicRng init_rng(0x77776666ULL);
        EntityHandle spawned_handles[ENTITY_COUNT];

        for (u32 i = 0; i < ENTITY_COUNT; ++i) {
            SpawnParams p{
                .archetype_id = static_cast<u16>(i % 32),
                .player_id = static_cast<u8>(i % 8),
                .pos = Vec3fx{
                    fx32::from_int(static_cast<i32>(init_rng.range_u32(4000))),
                    fx32::zero(),
                    fx32::from_int(static_cast<i32>(init_rng.range_u32(4000)))
                },
                .heading = Angle16{static_cast<u16>(init_rng.next_u32())},
                .move_speed = fx32::from_ratio(init_rng.range_i32(40, 250), 10),
                .radius = fx32::one(),
                .max_hp = 100,
                .aps_max_charges = static_cast<u8>(init_rng.range_u32(4)),
                .aps_reload_ticks = static_cast<u16>(init_rng.range_i32(40, 100))
            };
            auto res = world.entities().spawn(p);
            RON_ALWAYS_ASSERT(res.is_ok(), "spawn failed");
            spawned_handles[i] = res.value;
        }

        // Inject 4,000 commands
        DeterministicRng cmd_rng(0x55554444ULL);
        for (u32 c = 0; c < 4000; ++c) {
            const u32 target_tick = cmd_rng.range_u32(TICKS);
            const EntityHandle subj = spawned_handles[cmd_rng.range_u32(ENTITY_COUNT)];
            const u32 cmd_kind = cmd_rng.range_u32(4);

            SimCommand cmd{};
            cmd.execute_tick = target_tick;
            cmd.sequence_id = c + 1;
            cmd.subject = subj;

            if (cmd_kind == 0) {
                cmd.type = CommandType::Move;
                cmd.target_xz = Vec2fx{fx32::from_int(static_cast<i32>(cmd_rng.range_u32(4000))),
                                       fx32::from_int(static_cast<i32>(cmd_rng.range_u32(4000)))};
            } else if (cmd_kind == 1) {
                cmd.type = CommandType::AttackMove;
                cmd.target_xz = Vec2fx{fx32::from_int(static_cast<i32>(cmd_rng.range_u32(4000))),
                                       fx32::from_int(static_cast<i32>(cmd_rng.range_u32(4000)))};
            } else if (cmd_kind == 2) {
                cmd.type = CommandType::SetStance;
                cmd.sub_param = static_cast<u8>(cmd_rng.range_u32(4));
            } else {
                cmd.type = CommandType::Stop;
            }

            RON_ALWAYS_ASSERT(world.command_bus().enqueue(cmd).is_ok(), "enqueue failed");
        }

        {
            ron::test::ScopedNoHeapAlloc no_alloc_guard;
            const u64 allocs_before = ron::test::heap_alloc_count();
            for (u32 t = 0; t < TICKS; ++t) {
                world.step_tick(js);
            }
            RON_ALWAYS_ASSERT(ron::test::heap_alloc_count() == allocs_before,
                              "Heap allocation occurred during SimWorld::step_tick");
        }

        const u64 hash = world.compute_state_hash();
        js.shutdown();
        mem.shutdown_all();
        return hash;
    };

    const u64 hash_1w = run_match(1);
    const u64 hash_8w = run_match(8);

    RON_EXPECT_EQ(hash_1w, hash_8w);
}

// -------------------------------------------------------------------------
// 5. SimWorld_SnapshotSaveLoadMidMatchForkEquality
// -------------------------------------------------------------------------
RON_TEST(SimWorld_SnapshotSaveLoadMidMatchForkEquality) {
    EngineMemory mem;
    RON_EXPECT_TRUE(mem.init_all(4).is_ok());

    JobSystem js;
    RON_EXPECT_TRUE(js.init(4, &mem).is_ok());

    constexpr u64 SEED = 0x1122334455667788ULL;
    constexpr u32 ENTITY_COUNT = 4096;

    SimWorld worldA;
    RON_EXPECT_TRUE(worldA.init(mem.match, SEED).is_ok());

    DeterministicRng rng(0x123456ULL);
    EntityHandle* handles = mem.match.alloc<EntityHandle>(ENTITY_COUNT);
    RON_ALWAYS_ASSERT(handles != nullptr, "alloc handles failed");

    for (u32 i = 0; i < ENTITY_COUNT; ++i) {
        SimCommand cmd_spawn{
            .execute_tick = 0,
            .sequence_id = i + 1,
            .target_xz = Vec2fx{fx32::from_int(static_cast<i32>(rng.range_u32(3000))),
                                fx32::from_int(static_cast<i32>(rng.range_u32(3000)))},
            .param_id = static_cast<u16>(i % 16),
            .player_id = static_cast<u8>(i % 8),
            .type = CommandType::SpawnEntity,
            .sub_param = 0
        };
        RON_ALWAYS_ASSERT(worldA.command_bus().enqueue(cmd_spawn).is_ok(), "spawn enqueue failed");
    }

    // Process tick 0 spawns
    worldA.step_tick(js);
    for (u32 i = 0; i < ENTITY_COUNT; ++i) {
        handles[i] = worldA.entities().handle[i];
    }

    // Move orders
    for (u32 i = 0; i < ENTITY_COUNT; ++i) {
        SimCommand cmd{
            .execute_tick = 1,
            .sequence_id = ENTITY_COUNT + i + 1,
            .subject = handles[i],
            .target_xz = Vec2fx{fx32::from_int(static_cast<i32>(rng.range_u32(4000))),
                                fx32::from_int(static_cast<i32>(rng.range_u32(4000)))},
            .type = CommandType::Move
        };
        RON_ALWAYS_ASSERT(worldA.command_bus().enqueue(cmd).is_ok(), "enqueue failed");
    }

    // Run worldA to tick 50
    for (u32 t = 1; t < 50; ++t) {
        worldA.step_tick(js);
    }

    // Save snapshot at tick 50
    const u64 save_start = clock_now_ns();
    auto snap_res = worldA.save_snapshot_lz4(mem.render_staging);
    const u64 save_dur_ns = clock_now_ns() - save_start;
    const f64 save_dur_ms = clock_ns_to_ms(save_dur_ns);

    RON_EXPECT_TRUE(snap_res.is_ok());
    const std::span<u8> snapshot = snap_res.value;

    // Snapshot size < sizeof(SimState) / 2 and save time < 10 ms
    RON_EXPECT_TRUE(snapshot.size() < sizeof(SimState) / 2);
    RON_EXPECT_TRUE(save_dur_ms < 10.0);

    // New commands at tick 60 and 75
    SimCommand cmd60{
        .execute_tick = 60,
        .subject = handles[10],
        .target_xz = Vec2fx{fx32::from_int(500), fx32::from_int(500)},
        .type = CommandType::Move
    };
    SimCommand cmd75{
        .execute_tick = 75,
        .subject = handles[20],
        .target_xz = Vec2fx{fx32::from_int(800), fx32::from_int(800)},
        .type = CommandType::Move
    };

    RON_EXPECT_TRUE(worldA.command_bus().enqueue(cmd60).is_ok());
    RON_EXPECT_TRUE(worldA.command_bus().enqueue(cmd75).is_ok());

    // Continue worldA to tick 100
    for (u32 t = 50; t < 100; ++t) {
        worldA.step_tick(js);
    }
    const u64 final_hash_a = worldA.compute_state_hash();

    // Initialize brand-new worldB, pollute CommandBus with stale history + pending commands,
    // then load snapshot and verify CommandBus resets cleanly before applying post-snapshot commands.
    SimWorld worldB;
    RON_EXPECT_TRUE(worldB.init(mem.match, SEED).is_ok());

    SimCommand stale_past{
        .execute_tick = 0,
        .target_xz = Vec2fx{fx32::from_int(123), fx32::from_int(456)},
        .param_id = 7,
        .player_id = 2,
        .type = CommandType::SpawnEntity
    };
    RON_EXPECT_TRUE(worldB.command_bus().enqueue(stale_past).is_ok());
    worldB.step_tick(js);

    SimCommand stale_future{
        .execute_tick = 65,
        .target_xz = Vec2fx{fx32::from_int(789), fx32::from_int(321)},
        .param_id = 9,
        .player_id = 4,
        .type = CommandType::SpawnEntity
    };
    RON_EXPECT_TRUE(worldB.command_bus().enqueue(stale_future).is_ok());
    RON_EXPECT_EQ(worldB.command_bus().pending_count(), 1u);
    RON_EXPECT_EQ(worldB.command_bus().history_count(), 1u);

    RON_EXPECT_TRUE(worldB.load_snapshot_lz4(snapshot, mem.render_staging).is_ok());
    RON_EXPECT_EQ(worldB.state().current_tick, 50u);
    RON_EXPECT_EQ(worldB.command_bus().pending_count(), 0u);
    RON_EXPECT_EQ(worldB.command_bus().history_count(), 0u);

    RON_EXPECT_TRUE(worldB.command_bus().enqueue(cmd60).is_ok());
    RON_EXPECT_TRUE(worldB.command_bus().enqueue(cmd75).is_ok());

    // Run worldB to tick 100
    {
        ron::test::ScopedNoHeapAlloc no_alloc_guard;
        for (u32 t = 50; t < 100; ++t) {
            worldB.step_tick(js);
        }
    }
    const u64 final_hash_b = worldB.compute_state_hash();

    RON_EXPECT_EQ(final_hash_a, final_hash_b);

    js.shutdown();
    mem.shutdown_all();
}

// -------------------------------------------------------------------------
// 6. SimWorld_ReplayRoundtripDesyncVerification
// -------------------------------------------------------------------------
RON_TEST(SimWorld_ReplayRoundtripDesyncVerification) {
    EngineMemory mem;
    RON_EXPECT_TRUE(mem.init_all(4).is_ok());

    JobSystem js;
    RON_EXPECT_TRUE(js.init(4, &mem).is_ok());

    constexpr u64 SEED = 0x8899AABBCCDDEEFFULL;
    constexpr u32 TICKS = 120;
    constexpr u32 CMD_COUNT = 1000;

    SimWorld worldA;
    RON_EXPECT_TRUE(worldA.init(mem.match, SEED).is_ok());

    DeterministicRng rng(0x44332211ULL);
    for (u32 i = 0; i < CMD_COUNT; ++i) {
        SimCommand cmd{
            .execute_tick = (i < 200) ? 0u : rng.range_u32(TICKS),
            .sequence_id = i + 1,
            .target_xz = Vec2fx{fx32::from_int(static_cast<i32>(rng.range_u32(2000))),
                                fx32::from_int(static_cast<i32>(rng.range_u32(2000)))},
            .param_id = static_cast<u16>(rng.range_u32(20)),
            .player_id = static_cast<u8>(rng.range_u32(8)),
            .type = CommandType::SpawnEntity,
            .sub_param = 2
        };
        RON_ALWAYS_ASSERT(worldA.command_bus().enqueue(cmd).is_ok(), "enqueue failed");
    }

    for (u32 t = 0; t < TICKS; ++t) {
        worldA.step_tick(js);
    }

    const u64 final_hash_a = worldA.compute_state_hash();

    auto replay_res = worldA.command_bus().serialize_replay_lz4(
        SEED,
        TICKS,
        final_hash_a,
        mem.render_staging,
        mem.match
    );
    RON_EXPECT_TRUE(replay_res.is_ok());
    const std::span<u8> replay_blob = replay_res.value;

    // Load replay into fresh worldB
    SimWorld worldB;
    RON_EXPECT_TRUE(worldB.init(mem.match, SEED).is_ok());

    auto load_res = worldB.command_bus().load_replay_lz4(replay_blob, mem.render_staging);
    RON_EXPECT_TRUE(load_res.is_ok());
    const ReplayHeader header = load_res.value;

    RON_EXPECT_EQ(header.match_seed, SEED);
    RON_EXPECT_EQ(header.total_ticks, TICKS);
    RON_EXPECT_EQ(header.total_commands, CMD_COUNT);
    RON_EXPECT_EQ(header.final_state_xxh64, final_hash_a);

    // Step worldB 120 ticks
    for (u32 t = 0; t < TICKS; ++t) {
        worldB.step_tick(js);
    }

    const u64 final_hash_b = worldB.compute_state_hash();
    RON_EXPECT_EQ(final_hash_b, header.final_state_xxh64);

    js.shutdown();
    mem.shutdown_all();
}

// -------------------------------------------------------------------------
// 7. SimWorld_RenderInterpolationAndPerformanceBudget
// -------------------------------------------------------------------------
RON_TEST(SimWorld_RenderInterpolationAndPerformanceBudget) {
    EngineMemory mem;
    RON_EXPECT_TRUE(mem.init_all(8).is_ok());

    JobSystem js;
    RON_EXPECT_TRUE(js.init(8, &mem).is_ok());

    SimWorld world;
    RON_EXPECT_TRUE(world.init(mem.match, 0x55667788ULL).is_ok());

    // 1. Verify render interpolation
    SpawnParams p{
        .archetype_id = 1,
        .pos = Vec3fx{fx32::from_int(100), fx32::zero(), fx32::from_int(200)},
        .heading = Angle16::from_deg(350),
        .move_speed = fx32::from_int(10),
        .max_hp = 100
    };
    auto res = world.entities().spawn(p);
    RON_EXPECT_TRUE(res.is_ok());
    const u32 d = world.entities().resolve_dense(res.value);

    world.entities().prev_pos[d] = Vec3fx{fx32::from_int(100), fx32::zero(), fx32::from_int(200)};
    world.entities().curr_pos[d] = Vec3fx{fx32::from_int(120), fx32::zero(), fx32::from_int(250)};

    const Vec3fx p_zero = world.entities().interpolate_pos(d, fx32::zero());
    const Vec3fx p_half = world.entities().interpolate_pos(d, fx32::half());
    const Vec3fx p_one  = world.entities().interpolate_pos(d, fx32::one());

    RON_EXPECT_EQ(p_zero.x.raw, fx32::from_int(100).raw);
    RON_EXPECT_EQ(p_half.x.raw, fx32::from_int(110).raw);
    RON_EXPECT_EQ(p_one.x.raw,  fx32::from_int(120).raw);

    // Heading interpolation across 350° -> 10° seam (shortest arc is +20°)
    world.entities().prev_heading[d] = Angle16::from_deg(350);
    world.entities().curr_heading[d] = Angle16::from_deg(10);

    const Angle16 h_half = world.entities().interpolate_heading(d, fx32::half());
    // Halfway along shortest +20° arc is 360°/0°
    RON_EXPECT_TRUE(h_half.raw == 0 || h_half.raw == 65535 || h_half.raw == 1);

    // 2. Performance benchmark with 16,384 active moving entities on 8 workers
    world.entities().init();
    constexpr u32 BENCH_COUNT = 16384;
    DeterministicRng rng(0x11112222ULL);

    for (u32 i = 0; i < BENCH_COUNT; ++i) {
        SpawnParams bp{
            .archetype_id = static_cast<u16>(i % 32),
            .player_id = static_cast<u8>(i % 8),
            .pos = Vec3fx{fx32::from_int(static_cast<i32>(rng.range_u32(4000))),
                          fx32::zero(),
                          fx32::from_int(static_cast<i32>(rng.range_u32(4000)))},
            .heading = Angle16{static_cast<u16>(rng.next_u32())},
            .move_speed = fx32::from_int(10),
            .radius = fx32::one(),
            .max_hp = 100
        };
        auto b_res = world.entities().spawn(bp);
        RON_ALWAYS_ASSERT(b_res.is_ok(), "bench spawn failed");

        SimCommand mcmd{
            .execute_tick = 0,
            .subject = b_res.value,
            .target_xz = Vec2fx{fx32::from_int(static_cast<i32>(rng.range_u32(4000))),
                                fx32::from_int(static_cast<i32>(rng.range_u32(4000)))},
            .type = CommandType::Move
        };
        RON_ALWAYS_ASSERT(world.command_bus().enqueue(mcmd).is_ok(), "bench enqueue failed");
    }

    constexpr u32 BENCH_TICKS = 200;
    f64 tick_ms[BENCH_TICKS];
    {
        ron::test::ScopedNoHeapAlloc no_alloc_guard;
        const u64 allocs_before = ron::test::heap_alloc_count();
        RON_EXPECT_EQ(ron::test::no_heap_alloc_depth(), 1);

        // Warm-up 20 ticks
        for (u32 t = 0; t < 20; ++t) {
            world.step_tick(js);
        }

        // Benchmark 200 consecutive ticks
        for (u32 t = 0; t < BENCH_TICKS; ++t) {
            const u64 t0 = clock_now_ns();
            world.step_tick(js);
            const u64 t1 = clock_now_ns();
            tick_ms[t] = clock_ns_to_ms(t1 - t0);
        }

        RON_EXPECT_EQ(ron::test::heap_alloc_count(), allocs_before);
    }
    RON_EXPECT_EQ(ron::test::no_heap_alloc_depth(), 0);
    std::sort(tick_ms, tick_ms + BENCH_TICKS);
    const f64 p50 = tick_ms[BENCH_TICKS / 2];
    const f64 p95 = tick_ms[static_cast<usize>(BENCH_TICKS * 0.95)];
    std::printf("SimWorld 16k entities 200 ticks: p50 = %.3f ms, p95 = %.3f ms\n", p50, p95);

    // Release performance budget: median tick < 3.0 ms
#if !RON_DEBUG
    RON_EXPECT_TRUE(p50 < 3.0);
#else
    RON_EXPECT_TRUE(p50 < 25.0);
#endif

    js.shutdown();
    mem.shutdown_all();
}

// -------------------------------------------------------------------------
// 8. SimWorld_LayoutAndZeroHeapAllocHook
// -------------------------------------------------------------------------
RON_TEST(SimWorld_LayoutAndZeroHeapAllocHook) {
    static_assert(std::has_unique_object_representations_v<EntityStore>);
    static_assert(std::has_unique_object_representations_v<SimState>);
    static_assert(sizeof(EntityStore) % CACHE_LINE_BYTES == 0);
    static_assert(sizeof(SimState) % CACHE_LINE_BYTES == 0);
    static_assert(offsetof(SimState, entities) == CACHE_LINE_BYTES);

    std::printf("Layout proof: sizeof(EntityStore)=%zu, sizeof(SimState)=%zu, offsetof(entities)=%zu\n",
                sizeof(EntityStore), sizeof(SimState), offsetof(SimState, entities));

    RON_EXPECT_EQ(ron::test::no_heap_alloc_depth(), 0);

    // Verify global operator new/delete overrides are active outside ScopedNoHeapAlloc
    const u64 before = ron::test::heap_alloc_count();
    void* raw_p = ::operator new(64);
    RON_EXPECT_TRUE(raw_p != nullptr);
    ::operator delete(raw_p);

    void* aligned_p = ::operator new(128, std::align_val_t{64});
    RON_EXPECT_TRUE(aligned_p != nullptr);
    RON_EXPECT_EQ(reinterpret_cast<uintptr_t>(aligned_p) % 64, 0u);
    ::operator delete(aligned_p, std::align_val_t{64});
    RON_EXPECT_EQ(ron::test::heap_alloc_count(), before + 2);

    // Verify ScopedNoHeapAlloc depth nesting and zero-alloc arena + multi-worker tick execution
    {
        ron::test::ScopedNoHeapAlloc outer_guard;
        RON_EXPECT_EQ(ron::test::no_heap_alloc_depth(), 1);
        {
            ron::test::ScopedNoHeapAlloc inner_guard;
            RON_EXPECT_EQ(ron::test::no_heap_alloc_depth(), 2);

            EngineMemory mem;
            RON_EXPECT_TRUE(mem.init_all(4).is_ok());

            JobSystem js;
            RON_EXPECT_TRUE(js.init(4, &mem).is_ok());

            SimWorld world;
            RON_EXPECT_TRUE(world.init(mem.match, 0xCAFEBABEULL).is_ok());

            for (u32 i = 0; i < 64; ++i) {
                SimCommand cmd{
                    .execute_tick = i % 10,
                    .target_xz = Vec2fx{fx32::from_int(100 + static_cast<i32>(i)), fx32::from_int(200)},
                    .param_id = 1,
                    .player_id = static_cast<u8>(i % 4),
                    .type = CommandType::SpawnEntity
                };
                RON_EXPECT_TRUE(world.command_bus().enqueue(cmd).is_ok());
            }

            const u64 allocs_before_ticks = ron::test::heap_alloc_count();
            for (u32 t = 0; t < 20; ++t) {
                world.step_tick(js);
            }
            RON_EXPECT_EQ(ron::test::heap_alloc_count(), allocs_before_ticks);

            js.shutdown();
            mem.shutdown_all();
        }
        RON_EXPECT_EQ(ron::test::no_heap_alloc_depth(), 1);
    }
    RON_EXPECT_EQ(ron::test::no_heap_alloc_depth(), 0);
}
