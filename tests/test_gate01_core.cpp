#include "test_framework.hpp"
#include "core/types.hpp"
#include "core/result.hpp"
#include "core/assert_log.hpp"
#include "core/clock.hpp"
#include "core/arena.hpp"
#include "core/hash.hpp"
#include "core/prng.hpp"
#include "core/compression.hpp"

using namespace ron;

// 1. Hash_CanonicalVectorsAndStreaming
RON_TEST(Hash_CanonicalVectorsAndStreaming) {
    // Canonical empty string vector for xxHash64 with seed 0:
    const u64 empty_seed0 = xxh64(std::span<const u8>{}, 0);
    RON_EXPECT_EQ(empty_seed0, 0xEF46DB3751D8E999ULL);

    // Canonical empty string vector for xxHash64 with seed 2654435761:
    const u64 empty_seed_prime = xxh64(std::span<const u8>{}, 2654435761ULL);
    RON_EXPECT_EQ(empty_seed_prime, 0xAC75FDA2929B17EFULL);

    // Hash specific string and verify streaming match
    constexpr std::string_view rts_banner = "Dominion 2026 Deterministic RTS Kernel — Ancient Age to Autonomous Warfare";
    const u64 banner_hash = xxh64(rts_banner, 0);

    Xxh64Streamer banner_streamer;
    banner_streamer.reset(0);
    for (char c : rts_banner) {
        banner_streamer.update(std::span<const u8>{reinterpret_cast<const u8*>(&c), 1});
    }
    RON_EXPECT_EQ(banner_hash, banner_streamer.digest());

    // 131,072-byte buffer pseudo-random test across chunk sizes: 1, 7, 31, 64
    constexpr usize LARGE_BUF_SIZE = 131072;
    MemoryArena temp_arena;
    RON_EXPECT_TRUE(temp_arena.init(MiB(2), MiB(1), "HashTestArena").is_ok());

    u8* large_buf = temp_arena.alloc<u8>(LARGE_BUF_SIZE);
    DeterministicRng rng(0x12345678ULL);
    for (usize i = 0; i < LARGE_BUF_SIZE; ++i) {
        large_buf[i] = static_cast<u8>(rng.next_u32());
    }

    std::span<const u8> large_span{large_buf, LARGE_BUF_SIZE};
    const u64 ref_hash = xxh64(large_span, 0);

    const usize chunk_sizes[] = {1, 7, 31, 64};
    for (usize chunk_size : chunk_sizes) {
        Xxh64Streamer streamer;
        streamer.reset(0);

        usize offset = 0;
        while (offset < LARGE_BUF_SIZE) {
            const usize len = (offset + chunk_size <= LARGE_BUF_SIZE) ? chunk_size : (LARGE_BUF_SIZE - offset);
            streamer.update(std::span<const u8>{large_buf + offset, len});
            offset += len;
        }

        const u64 streamed_hash = streamer.digest();
        RON_EXPECT_EQ(ref_hash, streamed_hash);
    }

    temp_arena.shutdown();

    // Compile-time string interning check
    static_assert("unit_2026_mbt"_sid != "unit_ancient_hoplite"_sid);
    static_assert("unit_2026_mbt"_sid == "unit_2026_mbt"_sid);
    RON_EXPECT_NE("unit_2026_mbt"_sid, "unit_ancient_hoplite"_sid);
    RON_EXPECT_EQ("unit_2026_mbt"_sid, "unit_2026_mbt"_sid);
}

// 2. Arena_ReserveCommitZeroPaddingDeterminism
RON_TEST(Arena_ReserveCommitZeroPaddingDeterminism) {
    MemoryArena arenaA;
    MemoryArena arenaB;
    RON_EXPECT_TRUE(arenaA.init(MiB(64), KiB(64), "ArenaA").is_ok());
    RON_EXPECT_TRUE(arenaB.init(MiB(64), KiB(64), "ArenaB").is_ok());

    auto allocate_sequence = [](MemoryArena& a) {
        // Allocate sequence: 13B, 1B, 128B, 4099B, 256 KiB
        void* p1 = a.alloc_raw(13, 1);
        std::memset(p1, 0x11, 13);

        void* p2 = a.alloc_raw(1, 1);
        std::memset(p2, 0x22, 1);

        void* p3 = a.alloc_raw(128, CACHE_LINE_BYTES);
        std::memset(p3, 0x33, 128);

        void* p4 = a.alloc_raw(4099, 16);
        std::memset(p4, 0x44, 4099);

        // Triggers dynamic MEM_COMMIT expansion beyond 64 KiB
        void* p5 = a.alloc_raw(KiB(256), CACHE_LINE_BYTES);
        std::memset(p5, 0x55, KiB(256));

        // Rewind a marker via ArenaTempScope
        {
            ArenaTempScope scope(a);
            void* p_tmp = a.alloc_raw(KiB(32), CACHE_LINE_BYTES);
            std::memset(p_tmp, 0xAA, KiB(32));
        }

        // Allocate again after restore
        void* p6 = a.alloc_raw(1024, CACHE_LINE_BYTES);
        std::memset(p6, 0x66, 1024);
    };

    allocate_sequence(arenaA);
    allocate_sequence(arenaB);

    RON_EXPECT_EQ(arenaA.used_bytes(), arenaB.used_bytes());
    RON_EXPECT_TRUE(arenaA.committed_bytes() > KiB(64));
    RON_EXPECT_EQ(arenaA.committed_bytes(), arenaB.committed_bytes());

    const u64 hashA = xxh64(arenaA.used_span(), 0);
    const u64 hashB = xxh64(arenaB.used_span(), 0);
    RON_EXPECT_EQ(hashA, hashB);

    // Verify decommit_excess = true shrinks committed_bytes() back to 64 KiB
    arenaA.reset(true);
    RON_EXPECT_EQ(arenaA.used_bytes(), 0ULL);
    RON_EXPECT_EQ(arenaA.committed_bytes(), OS_PAGE_GRANULARITY);

    arenaA.shutdown();
    arenaB.shutdown();
}

// 3. EngineMemory_FullBudgetInit
RON_TEST(EngineMemory_FullBudgetInit) {
    EngineMemory mem;
    const u64 start_ns = clock_now_ns();
    auto res = mem.init_all(8);
    const u64 duration_ns = clock_now_ns() - start_ns;
    const f64 duration_ms = clock_ns_to_ms(duration_ns);

    RON_EXPECT_TRUE(res.is_ok());
    // verify init_all succeeds in < 5 ms
    RON_EXPECT_TRUE(duration_ms < 5.0);

    RON_EXPECT_EQ(mem.permanent.reserved_bytes(), MiB(256));
    RON_EXPECT_EQ(mem.match.reserved_bytes(), MiB(512));
    RON_EXPECT_EQ(mem.render_staging.reserved_bytes(), MiB(128));

    for (u32 i = 0; i < MAX_WORKER_THREADS; ++i) {
        RON_EXPECT_EQ(mem.worker_scratch[i].reserved_bytes(), MiB(32));
    }

    // Allocate from every arena
    void* p_perm = mem.permanent.alloc_raw(1024);
    void* p_match = mem.match.alloc_raw(2048);
    void* p_render = mem.render_staging.alloc_raw(4096);
    RON_EXPECT_TRUE(p_perm != nullptr);
    RON_EXPECT_TRUE(p_match != nullptr);
    RON_EXPECT_TRUE(p_render != nullptr);

    for (u32 i = 0; i < MAX_WORKER_THREADS; ++i) {
        void* p_w = mem.worker_scratch[i].alloc_raw(512);
        RON_EXPECT_TRUE(p_w != nullptr);
    }

    mem.permanent.reset();
    mem.match.reset();
    mem.render_staging.reset();
    for (u32 i = 0; i < MAX_WORKER_THREADS; ++i) {
        mem.worker_scratch[i].reset();
    }

    mem.shutdown_all();
    RON_EXPECT_EQ(mem.permanent.reserved_bytes(), 0ULL);
    RON_EXPECT_EQ(mem.match.reserved_bytes(), 0ULL);
}

// 4. Prng_ReproducibilityAndDistribution
RON_TEST(Prng_ReproducibilityAndDistribution) {
    DeterministicRng rngA(0x2026CAFEULL);
    DeterministicRng rngB(0x2026CAFEULL);

    // Generate 1,000,000 numbers from two DeterministicRng instances; assert exact sequence equality
    constexpr usize SAMPLES = 1000000;
    bool all_equal = true;
    for (usize i = 0; i < SAMPLES; ++i) {
        if (rngA.next_u32() != rngB.next_u32()) {
            all_equal = false;
            break;
        }
    }
    RON_EXPECT_TRUE(all_equal);

    // Verify range_u32(7) across 700,000 draws lands in each bucket [0..6] within ±1.5% of 100,000
    constexpr usize DRAWS = 700000;
    u32 buckets[7] = {0};
    for (usize i = 0; i < DRAWS; ++i) {
        const u32 val = rngA.range_u32(7);
        buckets[val]++;
    }

    // Expected per bucket = 100,000; ±1.5% => [98,500, 101,500]
    for (u32 b = 0; b < 7; ++b) {
        RON_EXPECT_TRUE(buckets[b] >= 98500 && buckets[b] <= 101500);
    }

    // Errata 04: Assert s[0]!=s[2] || s[1]!=s[3] across 1000 seeds
    for (u64 seed = 1; seed <= 1000; ++seed) {
        DeterministicRng r(seed);
        RON_EXPECT_TRUE(r.s[0] != r.s[2] || r.s[1] != r.s[3]);
    }
}

// 5. Compression_Lz4RoundtripAndCorruptionDetection
RON_TEST(Compression_Lz4RoundtripAndCorruptionDetection) {
    MemoryArena match_arena;
    MemoryArena scratch_arena;
    MemoryArena decomp_arena;

    RON_EXPECT_TRUE(match_arena.init(MiB(16), MiB(4), "MatchArena").is_ok());
    RON_EXPECT_TRUE(scratch_arena.init(MiB(16), MiB(4), "ScratchArena").is_ok());
    RON_EXPECT_TRUE(decomp_arena.init(MiB(16), MiB(4), "DecompArena").is_ok());

    // Populate a 2 MiB simulated entity buffer inside MatchArena
    constexpr usize ENTITY_BUF_SIZE = MiB(2);
    struct alignas(16) SimulatedEntity {
        u32 id;
        u16 type;
        u16 health;
        i32 x, y, z;
        u32 flags;
        u32 target_id;
        u32 padding;
    };
    static_assert(sizeof(SimulatedEntity) == 32);

    const usize entity_count = ENTITY_BUF_SIZE / sizeof(SimulatedEntity);
    SimulatedEntity* entities = match_arena.alloc<SimulatedEntity>(entity_count);
    for (usize i = 0; i < entity_count; ++i) {
        entities[i].id = static_cast<u32>(i);
        entities[i].type = static_cast<u16>(i % 16);
        entities[i].health = 1000;
        entities[i].x = static_cast<i32>((i % 512) * 64);
        entities[i].y = 0;
        entities[i].z = static_cast<i32>((i / 512) * 64);
        entities[i].flags = 1;
        entities[i].target_id = 0;
        entities[i].padding = 0;
    }

    std::span<const u8> orig_span{reinterpret_cast<const u8*>(entities), ENTITY_BUF_SIZE};
    const u64 orig_hash = xxh64(orig_span, 0);

    // Compress with lz4_compress_to_arena into ScratchArena
    auto comp_res = lz4_compress_to_arena(orig_span, scratch_arena);
    RON_EXPECT_TRUE(comp_res.is_ok());
    std::span<u8> comp_span = comp_res.value;

    // Verify compressed size < 1 MiB
    RON_EXPECT_TRUE(comp_span.size() < MiB(1));

    // Decompress into a third buffer
    auto decomp_res = lz4_decompress_to_arena(comp_span, decomp_arena);
    RON_EXPECT_TRUE(decomp_res.is_ok());
    std::span<u8> decomp_span = decomp_res.value;

    // Assert xxh64 equality with original 2 MiB buffer
    RON_EXPECT_EQ(decomp_span.size(), ENTITY_BUF_SIZE);
    const u64 decomp_hash = xxh64(decomp_span, 0);
    RON_EXPECT_EQ(orig_hash, decomp_hash);

    // Flip 1 bit in the compressed payload and assert lz4_decompress_to_arena cleanly returns error
    comp_span[sizeof(Lz4BlockHeader) + 64] ^= 0x01;
    auto corrupt_res = lz4_decompress_to_arena(comp_span, decomp_arena);
    RON_EXPECT_TRUE(corrupt_res.is_err());
    RON_EXPECT_TRUE(corrupt_res.error == ErrorCode::CorruptData || corrupt_res.error == ErrorCode::DecompressionFailed);

    match_arena.shutdown();
    scratch_arena.shutdown();
    decomp_arena.shutdown();
}

// 6. Clock_MonotonicPrecision
RON_TEST(Clock_MonotonicPrecision) {
    clock_init();

    // Verify 1,000 consecutive calls to clock_now_ns() are strictly non-decreasing (t[i+1] >= t[i])
    u64 prev = clock_now_ns();
    bool monotonic = true;
    for (int i = 0; i < 1000; ++i) {
        const u64 cur = clock_now_ns();
        if (cur < prev) {
            monotonic = false;
            break;
        }
        prev = cur;
    }
    RON_EXPECT_TRUE(monotonic);
}
