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

#include <atomic>
#include <bit>

using namespace ron;

// -------------------------------------------------------------------------
// 1. JobSystem_SingleWorkerDeterministicFallback
// -------------------------------------------------------------------------
RON_TEST(JobSystem_SingleWorkerDeterministicFallback) {
    EngineMemory mem;
    RON_EXPECT_TRUE(mem.init_all(1).is_ok());

    JobSystem js;
    RON_EXPECT_TRUE(js.init(1, &mem).is_ok());
    RON_EXPECT_EQ(js.worker_count(), 1u);

    constexpr u32 JOB_COUNT = 512;
    u32 results[JOB_COUNT]{};

    struct TestPayload {
        JobSystem* js;
        u32* target;
        u32 idx;
    };

    JobCounter counter;

    for (u32 i = 0; i < JOB_COUNT; ++i) {
        TestPayload payload{&js, &results[i], i};
        js.submit(&counter, [payload](u32 wid) noexcept {
            RON_ALWAYS_ASSERT(wid == 0, "Single worker must execute on worker 0");
            RON_ALWAYS_ASSERT(payload.js->current_worker_index() == 0, "Worker index must be 0");

            if (payload.idx == 100) {
                // Spawn 4 nested child jobs and wait
                JobCounter child_counter;
                u32 child_vals[4]{};
                for (u32 c = 0; c < 4; ++c) {
                    payload.js->submit(&child_counter, [&child_vals, c](u32 cwid) noexcept {
                        RON_ALWAYS_ASSERT(cwid == 0, "Child must execute on worker 0");
                        child_vals[c] = (c + 1) * 111;
                    });
                }
                payload.js->wait(child_counter);
                RON_ALWAYS_ASSERT(child_vals[0] == 111 && child_vals[3] == 444, "Child values verified");
            }

            *payload.target = payload.idx * 3;
        });
    }

    js.wait(counter);

    for (u32 i = 0; i < JOB_COUNT; ++i) {
        RON_EXPECT_EQ(results[i], i * 3);
    }

    // Test parallel_for_deterministic on single worker
    u32 pfor_results[1000]{};
    js.parallel_for_deterministic(1000, 64, [&pfor_results, &js](u32 begin, u32 end, u32 chunk_id, MemoryArena& scratch) {
        RON_ALWAYS_ASSERT(js.current_worker_index() == 0, "Worker 0 only");
        u32* temp = scratch.alloc<u32>(end - begin);
        RON_ALWAYS_ASSERT(temp != nullptr, "Scratch alloc valid");
        for (u32 i = begin; i < end; ++i) {
            temp[i - begin] = chunk_id;
            pfor_results[i] = i * 2;
        }
    });

    for (u32 i = 0; i < 1000; ++i) {
        RON_EXPECT_EQ(pfor_results[i], i * 2);
    }

    js.shutdown();
    mem.shutdown_all();
}

// -------------------------------------------------------------------------
// 2. JobSystem_MultiCoreStealingNestedWaitAndQueueOverflow
// -------------------------------------------------------------------------
RON_TEST(JobSystem_MultiCoreStealingNestedWaitAndQueueOverflow) {
    EngineMemory mem;
    RON_EXPECT_TRUE(mem.init_all(8).is_ok());

    JobSystem js;
    RON_EXPECT_TRUE(js.init(8, &mem).is_ok());

    constexpr u32 PARENT_COUNT = 2500;
    constexpr u32 NESTED_CHILDREN = 4;
    constexpr u32 TOTAL_JOBS = PARENT_COUNT + (PARENT_COUNT / 10) * NESTED_CHILDREN;

    std::atomic<u32> hits[TOTAL_JOBS]{};
    std::atomic<u32> worker_mask{0};

    struct ParentContext {
        JobSystem* js;
        u32 parent_idx;
        std::atomic<u32>* hits_arr;
        std::atomic<u32>* mask;
    };

    JobCounter parent_counter;

    for (u32 i = 0; i < PARENT_COUNT; ++i) {
        ParentContext ctx{&js, i, hits, &worker_mask};
        js.submit(&parent_counter, [ctx](u32 wid) noexcept {
            ctx.mask->fetch_or(1u << (wid & 31u), std::memory_order_relaxed);
            ctx.hits_arr[ctx.parent_idx].fetch_add(1, std::memory_order_relaxed);

            if (ctx.parent_idx % 10 == 0) {
                const u32 child_base = PARENT_COUNT + (ctx.parent_idx / 10) * 4;
                JobCounter child_counter;

                for (u32 c = 0; c < 4; ++c) {
                    const u32 child_idx = child_base + c;
                    ctx.js->submit(&child_counter, [&ctx, child_idx](u32 cwid) noexcept {
                        ctx.mask->fetch_or(1u << (cwid & 31u), std::memory_order_relaxed);
                        ctx.hits_arr[child_idx].fetch_add(1, std::memory_order_relaxed);
                    });
                }

                ctx.js->wait(child_counter);
            }
        });
    }

    js.wait(parent_counter);

    for (u32 i = 0; i < TOTAL_JOBS; ++i) {
        RON_EXPECT_EQ(hits[i].load(std::memory_order_relaxed), 1u);
    }

    // Verify multiple workers participated in execution
    const u32 mask = worker_mask.load();
    RON_EXPECT_TRUE(std::popcount(mask) >= 2);

    js.shutdown();
    mem.shutdown_all();
}

// -------------------------------------------------------------------------
// 3. ParallelFor_1WorkerVs8WorkersBitExactSimTickHash
// -------------------------------------------------------------------------
RON_TEST(ParallelFor_1WorkerVs8WorkersBitExactSimTickHash) {
    constexpr u32 ENTITY_COUNT = 16384;
    constexpr u32 TICKS = 40;

    struct RON_CACHE_ALIGN RtsEntity {
        Vec3fx pos;
        Vec2fx vel;
        Angle16 heading;
        u16 pad0;
        fx32 speed;
        u32 id;
        u32 pad1;
    };
    static_assert(sizeof(RtsEntity) == 64);

    const Circle2fx obstacles[4] = {
        Circle2fx{Vec2fx{fx32::from_int(1000), fx32::from_int(1000)}, fx32::from_int(100)},
        Circle2fx{Vec2fx{fx32::from_int(2000), fx32::from_int(1500)}, fx32::from_int(150)},
        Circle2fx{Vec2fx{fx32::from_int(1500), fx32::from_int(3000)}, fx32::from_int(200)},
        Circle2fx{Vec2fx{fx32::from_int(3000), fx32::from_int(3000)}, fx32::from_int(120)}
    };

    auto run_sim = [&](u32 workers) -> u64 {
        EngineMemory mem;
        RON_ALWAYS_ASSERT(mem.init_all(workers).is_ok(), "EngineMemory init");

        JobSystem js;
        RON_ALWAYS_ASSERT(js.init(workers, &mem).is_ok(), "JobSystem init");

        MemoryArena match_arena;
        RON_ALWAYS_ASSERT(match_arena.init(MiB(512), MiB(8), "MatchArena").is_ok(), "MatchArena init");

        RtsEntity* entities = match_arena.alloc<RtsEntity>(ENTITY_COUNT);
        RON_ALWAYS_ASSERT(entities != nullptr, "Entities alloc");

        DeterministicRng init_rng(0xACE12345ULL);
        for (u32 i = 0; i < ENTITY_COUNT; ++i) {
            entities[i].pos = Vec3fx{
                fx32::from_int(static_cast<i32>(init_rng.range_u32(4000))),
                fx32::zero(),
                fx32::from_int(static_cast<i32>(init_rng.range_u32(4000)))
            };
            entities[i].vel = Vec2fx{
                fx32::from_ratio(init_rng.range_i32(-50, 50), 10),
                fx32::from_ratio(init_rng.range_i32(-50, 50), 10)
            };
            entities[i].heading = Angle16{static_cast<u16>(init_rng.next_u32())};
            entities[i].speed = fx32::from_ratio(init_rng.range_i32(10, 30), 10);
            entities[i].id = i;
        }

        for (u32 tick = 0; tick < TICKS; ++tick) {
            DeterministicRng tick_rng(0x12345678ULL + static_cast<u64>(tick) * 0x9E3779B97F4A7C15ULL);

            js.parallel_for_deterministic(ENTITY_COUNT, 96, [&](u32 begin, u32 end, u32 chunk_id, MemoryArena& scratch) {
                DeterministicRng chunk_rng = tick_rng.fork(chunk_id);
                u32* temp_scratch = scratch.alloc<u32>(end - begin);
                RON_ALWAYS_ASSERT(temp_scratch != nullptr, "Scratch alloc valid");

                for (u32 i = begin; i < end; ++i) {
                    RtsEntity& e = entities[i];
                    const Angle16 target_ang = atan2_bam(e.vel.z, e.vel.x);
                    e.heading = turn_towards_bam(e.heading, target_ang, Angle16::from_deg(5).raw);

                    const Vec2fx forward{cos_bam(e.heading), sin_bam(e.heading)};
                    const Vec2fx move = forward * e.speed;

                    const Vec2fx p0 = e.pos.xz();
                    const Vec2fx p1 = p0 + move;

                    for (const auto& obs : obstacles) {
                        fx32 hit_t{};
                        if (segment_circle_hit(p0, p1, obs, hit_t)) {
                            e.heading = e.heading + Angle16::from_deg(90);
                            break;
                        }
                    }

                    e.pos.x = clamp_fx(e.pos.x + move.x, fx32::zero(), fx32::from_int(4095));
                    e.pos.z = clamp_fx(e.pos.z + move.z, fx32::zero(), fx32::from_int(4095));
                    temp_scratch[i - begin] = chunk_rng.next_u32();
                }
            });
        }

        const u64 digest = xxh64(match_arena.used_span(), 0);
        match_arena.shutdown();
        js.shutdown();
        mem.shutdown_all();
        return digest;
    };

    const u64 hash_1w = run_sim(1);
    const u64 hash_8w = run_sim(8);

    RON_EXPECT_EQ(hash_1w, hash_8w);
}

// -------------------------------------------------------------------------
// 4. ParallelReduce_NonCommutativeHashOrderInvariance
// -------------------------------------------------------------------------
RON_TEST(ParallelReduce_NonCommutativeHashOrderInvariance) {
    constexpr u32 COUNT = 20000;
    constexpr u32 GRAIN = 53;

    auto run_reduction = [](u32 workers) -> u64 {
        EngineMemory mem;
        RON_ALWAYS_ASSERT(mem.init_all(workers).is_ok(), "EngineMemory init");

        JobSystem js;
        RON_ALWAYS_ASSERT(js.init(workers, &mem).is_ok(), "JobSystem init");

        MemoryArena arena;
        RON_ALWAYS_ASSERT(arena.init(MiB(8), MiB(2), "ReductionArena").is_ok(), "Arena init");
        u64* items = arena.alloc<u64>(COUNT);
        DeterministicRng rng(0xCAFED00DULL);
        for (u32 i = 0; i < COUNT; ++i) {
            items[i] = rng.next_u64();
        }

        // Verify caller's scratch arena buffer allocated before parallel_reduce is not overwritten
        MemoryArena& caller_scratch = js.current_scratch_arena();
        u64* canary = caller_scratch.alloc<u64>(128);
        for (u32 k = 0; k < 128; ++k) {
            canary[k] = 0xCAFEBABE00000000ULL | k;
        }

        auto map_chunk = [items](u32 begin, u32 end, u32, MemoryArena&) -> u64 {
            u64 chunk_hash = 0xCBF29CE484222325ULL;
            for (u32 i = begin; i < end; ++i) {
                chunk_hash ^= items[i];
                chunk_hash *= 0x100000001B3ULL;
            }
            return chunk_hash;
        };

        // Strictly non-commutative reduction function
        auto reduce_fn = [](u64 acc, u64 chunk_val) -> u64 {
            return std::rotl(acc, 11) ^ (chunk_val * 0x9E3779B185EBCA87ULL + 17ULL);
        };

        const u64 result = js.parallel_reduce_deterministic<u64>(
            COUNT,
            GRAIN,
            0x123456789ABCDEF0ULL,
            map_chunk,
            reduce_fn
        );

        // Verify canary integrity
        for (u32 k = 0; k < 128; ++k) {
            RON_ALWAYS_ASSERT(canary[k] == (0xCAFEBABE00000000ULL | k), "Canary corrupted!");
        }

        arena.shutdown();
        js.shutdown();
        mem.shutdown_all();
        return result;
    };

    const u64 ref_hash = run_reduction(1);

    for (int trial = 0; trial < 10; ++trial) {
        RON_EXPECT_EQ(run_reduction(1), ref_hash);
        RON_EXPECT_EQ(run_reduction(4), ref_hash);
        RON_EXPECT_EQ(run_reduction(8), ref_hash);
    }
}

// -------------------------------------------------------------------------
// 5. TaskGraph_7StageDiamondRtsTickOrdering
// -------------------------------------------------------------------------
RON_TEST(TaskGraph_7StageDiamondRtsTickOrdering) {
    EngineMemory mem;
    RON_EXPECT_TRUE(mem.init_all(8).is_ok());

    JobSystem js;
    RON_EXPECT_TRUE(js.init(8, &mem).is_ok());

    struct StageTiming {
        std::atomic<u64> enter_seq{0};
        std::atomic<u64> exit_seq{0};
        std::atomic<u32> run_count{0};
    } timing[7];

    std::atomic<u64> global_clock{1};

    struct StageContext {
        StageTiming* t;
        std::atomic<u64>* clk;
        u32 stage_id;
    };

    StageContext stage_contexts[7];
    for (u32 i = 0; i < 7; ++i) {
        stage_contexts[i] = StageContext{&timing[i], &global_clock, i};
    }

    TaskGraph graph;

    auto make_stage = [&](const char* name, u32 idx) -> TaskId {
        return graph.add_node(name, &stage_contexts[idx], [](void* user_ctx, JobSystem& node_js, MemoryArena&) noexcept {
            auto* ctx = static_cast<StageContext*>(user_ctx);
            ctx->t->enter_seq.store(ctx->clk->fetch_add(1, std::memory_order_seq_cst), std::memory_order_seq_cst);

            // Stages 4 and 5 internally call parallel_for_deterministic!
            if (ctx->stage_id == 4 || ctx->stage_id == 5) {
                node_js.parallel_for_deterministic(512, 64, [](u32 begin, u32 end, u32, MemoryArena&) {
                    for (u32 k = begin; k < end; ++k) {
                        _mm_pause();
                    }
                });
            } else {
                for (int spin = 0; spin < 500; ++spin) {
                    _mm_pause();
                }
            }

            ctx->t->run_count.fetch_add(1, std::memory_order_relaxed);
            ctx->t->exit_seq.store(ctx->clk->fetch_add(1, std::memory_order_seq_cst), std::memory_order_seq_cst);
        }).value;
    };

    // 7-Stage DAG
    // 0: CmdIngest -> { 1: SensorGrid, 2: FlowFieldNav, 3: EconomyTick }
    // { 1, 2 } -> 4: SteeringRVO
    // { 1, 4 } -> 5: ProjectileSeekers
    // { 3, 5 } -> 6: DamageAndBordersCommit
    const TaskId n0 = make_stage("CmdIngest", 0);
    const TaskId n1 = make_stage("SensorGrid", 1);
    const TaskId n2 = make_stage("FlowFieldNav", 2);
    const TaskId n3 = make_stage("EconomyTick", 3);
    const TaskId n4 = make_stage("SteeringRVO", 4);
    const TaskId n5 = make_stage("ProjectileSeekers", 5);
    const TaskId n6 = make_stage("DamageAndBordersCommit", 6);

    RON_EXPECT_TRUE(graph.add_dependency(n0, n1).is_ok());
    RON_EXPECT_TRUE(graph.add_dependency(n0, n2).is_ok());
    RON_EXPECT_TRUE(graph.add_dependency(n0, n3).is_ok());
    RON_EXPECT_TRUE(graph.add_dependency(n1, n4).is_ok());
    RON_EXPECT_TRUE(graph.add_dependency(n2, n4).is_ok());
    RON_EXPECT_TRUE(graph.add_dependency(n1, n5).is_ok());
    RON_EXPECT_TRUE(graph.add_dependency(n4, n5).is_ok());
    RON_EXPECT_TRUE(graph.add_dependency(n3, n6).is_ok());
    RON_EXPECT_TRUE(graph.add_dependency(n5, n6).is_ok());

    RON_EXPECT_TRUE(graph.compile().is_ok());

    for (int tick = 0; tick < 100; ++tick) {
        graph.execute(js);

        // Strict DAG dependency order verification: exit_seq[u] < enter_seq[v]
        RON_CHECK(timing[0].exit_seq.load() < timing[1].enter_seq.load());
        RON_CHECK(timing[0].exit_seq.load() < timing[2].enter_seq.load());
        RON_CHECK(timing[0].exit_seq.load() < timing[3].enter_seq.load());

        RON_CHECK(timing[1].exit_seq.load() < timing[4].enter_seq.load());
        RON_CHECK(timing[2].exit_seq.load() < timing[4].enter_seq.load());

        RON_CHECK(timing[1].exit_seq.load() < timing[5].enter_seq.load());
        RON_CHECK(timing[4].exit_seq.load() < timing[5].enter_seq.load());

        RON_CHECK(timing[3].exit_seq.load() < timing[6].enter_seq.load());
        RON_CHECK(timing[5].exit_seq.load() < timing[6].enter_seq.load());
    }

    for (u32 i = 0; i < 7; ++i) {
        RON_EXPECT_EQ(timing[i].run_count.load(), 100u);
    }

    js.shutdown();
    mem.shutdown_all();
}

// -------------------------------------------------------------------------
// 6. TaskGraph_CycleAndInvalidEdgeRejection
// -------------------------------------------------------------------------
RON_TEST(TaskGraph_CycleAndInvalidEdgeRejection) {
    TaskGraph g;
    auto noop = [](void*, JobSystem&, MemoryArena&) noexcept {};

    const TaskId t0 = g.add_node("T0", nullptr, noop).value;
    const TaskId t1 = g.add_node("T1", nullptr, noop).value;
    const TaskId t2 = g.add_node("T2", nullptr, noop).value;
    const TaskId t3 = g.add_node("T3", nullptr, noop).value;

    // Self-edge rejection
    RON_EXPECT_TRUE(g.add_dependency(t0, t0).is_err());
    RON_EXPECT_EQ(g.add_dependency(t0, t0).error, ErrorCode::InvalidArgument);

    // Out of bounds edge rejection
    RON_EXPECT_TRUE(g.add_dependency(t0, 99).is_err());
    RON_EXPECT_EQ(g.add_dependency(t0, 99).error, ErrorCode::InvalidArgument);

    // Cycle creation: 0 -> 1 -> 2 -> 3 -> 1
    RON_EXPECT_TRUE(g.add_dependency(t0, t1).is_ok());
    RON_EXPECT_TRUE(g.add_dependency(t1, t2).is_ok());
    RON_EXPECT_TRUE(g.add_dependency(t2, t3).is_ok());
    RON_EXPECT_TRUE(g.add_dependency(t3, t1).is_ok());

    RON_EXPECT_TRUE(g.compile().is_err());
    RON_EXPECT_EQ(g.compile().error, ErrorCode::InvalidArgument);
}

// -------------------------------------------------------------------------
// 7. Profiler_MultiThreadZeroContentionTelemetry
// -------------------------------------------------------------------------
RON_TEST(Profiler_MultiThreadZeroContentionTelemetry) {
    EngineMemory mem;
    RON_EXPECT_TRUE(mem.init_all(8).is_ok());

    JobSystem js;
    RON_EXPECT_TRUE(js.init(8, &mem).is_ok());

    Profiler profiler;
    RON_EXPECT_TRUE(profiler.init(mem.permanent, &js).is_ok());

    TaskGraph graph;
    auto dummy_work = [](void*, JobSystem&, MemoryArena&) noexcept {
        for (int i = 0; i < 2000; ++i) {
            _mm_pause();
        }
    };

    const TaskId n0 = graph.add_node("CmdIngest", nullptr, dummy_work).value;
    const TaskId n1 = graph.add_node("SteeringRVO", nullptr, dummy_work).value;
    const TaskId n2 = graph.add_node("ProjectileSeekers", nullptr, dummy_work).value;

    RON_EXPECT_TRUE(graph.add_dependency(n0, n1).is_ok());
    RON_EXPECT_TRUE(graph.add_dependency(n1, n2).is_ok());
    RON_EXPECT_TRUE(graph.compile().is_ok());

    for (int frame = 0; frame < 10; ++frame) {
        profiler.begin_frame();
        graph.execute(js, &profiler);
        profiler.end_frame();
    }

    const ProfileScopeStat* stat_rvo = profiler.find_stat("SteeringRVO");
    const ProfileScopeStat* stat_proj = profiler.find_stat("ProjectileSeekers");

    RON_EXPECT_TRUE(stat_rvo != nullptr);
    RON_EXPECT_TRUE(stat_proj != nullptr);

    if (stat_rvo != nullptr) {
        RON_EXPECT_TRUE(stat_rvo->last_call_count >= 1);
        RON_EXPECT_TRUE(stat_rvo->rolling_avg_ns > 0);
        RON_EXPECT_TRUE(stat_rvo->min_ns <= stat_rvo->rolling_avg_ns);
        RON_EXPECT_TRUE(stat_rvo->rolling_avg_ns <= stat_rvo->max_ns);
    }

    if (stat_proj != nullptr) {
        RON_EXPECT_TRUE(stat_proj->last_call_count >= 1);
        RON_EXPECT_TRUE(stat_proj->rolling_avg_ns > 0);
        RON_EXPECT_TRUE(stat_proj->min_ns <= stat_proj->rolling_avg_ns);
        RON_EXPECT_TRUE(stat_proj->rolling_avg_ns <= stat_proj->max_ns);
    }

    js.shutdown();
    mem.shutdown_all();
}
