#include "test_framework.hpp"
#include "core/types.hpp"
#include "core/fixed_math.hpp"
#include "core/fixed_geom.hpp"
#include "core/hash.hpp"
#include "core/prng.hpp"
#include "core/arena.hpp"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

using namespace ron;

// -------------------------------------------------------------------------
// 1. FixedScalar_SymmetrySaturationAndZeroSafety
// -------------------------------------------------------------------------
RON_TEST(FixedScalar_SymmetrySaturationAndZeroSafety) {
    // 1. Symmetric saturation bounds for fx32
    static_assert(fx32::RAW_MIN == -fx32::RAW_MAX);
    RON_EXPECT_EQ(fx32::min_val().raw, -fx32::max_val().raw);

    // Raw -2147483648 (0x80000000) clamped to RAW_MIN (-0x7FFFFFFF)
    const fx32 clamped_min = fx32::from_raw(static_cast<i64>(-2147483648LL));
    RON_EXPECT_EQ(clamped_min.raw, fx32::RAW_MIN);

    const fx32 clamped_max = fx32::from_raw(static_cast<i64>(2147483648LL));
    RON_EXPECT_EQ(clamped_max.raw, fx32::RAW_MAX);

    // Unary negation symmetry
    RON_EXPECT_EQ((-fx32::min_val()).raw, fx32::max_val().raw);
    RON_EXPECT_EQ((-fx32::max_val()).raw, fx32::min_val().raw);
    RON_EXPECT_EQ(abs_fx(fx32::min_val()).raw, fx32::max_val().raw);

    // 2. Zero-safe symmetric division
    RON_EXPECT_EQ(fx32::div_sat(fx32::one(), fx32::zero()).raw, fx32::RAW_MAX);
    RON_EXPECT_EQ(fx32::div_sat(fx32::minus_one(), fx32::zero()).raw, fx32::RAW_MIN);
    RON_EXPECT_EQ(fx32::div_sat(fx32::zero(), fx32::zero()).raw, 0);

    // 3. Across 25,000 random (a, b) pairs from DeterministicRng, assert exact odd-symmetry
    DeterministicRng rng(0x1337CAFEULL);
    for (int i = 0; i < 25000; ++i) {
        const fx32 a = fx32::from_raw(static_cast<i32>(rng.next_u32() & 0x7FFF'FFFF));
        const fx32 b = fx32::from_raw(static_cast<i32>(rng.next_u32() & 0x7FFF'FFFF));

        // (a * (-b)) == -(a * b)
        const fx32 prod1 = a * (-b);
        const fx32 prod2 = -(a * b);
        RON_EXPECT_EQ(prod1.raw, prod2.raw);

        // fx32::div_sat(-a, b) == -fx32::div_sat(a, b)
        const fx32 div1 = fx32::div_sat(-a, b);
        const fx32 div2 = -fx32::div_sat(a, b);
        RON_EXPECT_EQ(div1.raw, div2.raw);
    }

    // Ratio constructor
    const fx32 half = fx32::from_ratio(1, 2);
    RON_EXPECT_EQ(half.raw, fx32::RAW_HALF);
    const fx32 neg_half = fx32::from_ratio(-1, 2);
    RON_EXPECT_EQ(neg_half.raw, -fx32::RAW_HALF);

    // 4. fx64 symmetry and saturation
    static_assert(fx64::RAW_MIN == -fx64::RAW_MAX);
    RON_EXPECT_EQ(fx64::min_val().raw, -fx64::max_val().raw);

    const fx64 clamped_64_min = fx64::from_raw(static_cast<__int128>(-0x7FFF'FFFF'FFFF'FFFFLL) - 100);
    RON_EXPECT_EQ(clamped_64_min.raw, fx64::RAW_MIN);

    // mul_exact between two fx32 produces exact fx64
    const fx64 exact_prod = fx64::mul_exact(fx32::lit(2.5), fx32::lit(4.0));
    RON_EXPECT_EQ(exact_prod.to_fx32_sat().raw, fx32::lit(10.0).raw);

    // fx64 division zero safety
    RON_EXPECT_EQ(fx64::div_sat(fx64::one(), fx64::zero()).raw, fx64::RAW_MAX);
    RON_EXPECT_EQ(fx64::div_sat(-fx64::one(), fx64::zero()).raw, fx64::RAW_MIN);
}

// -------------------------------------------------------------------------
// 2. FixedSqrt_ContinentalDistancesAndMicroVectors
// -------------------------------------------------------------------------
RON_TEST(FixedSqrt_ContinentalDistancesAndMicroVectors) {
    // Exact floor checks for isqrt_u64
    RON_EXPECT_EQ(isqrt_u64(0), 0U);
    RON_EXPECT_EQ(isqrt_u64(1), 1U);
    RON_EXPECT_EQ(isqrt_u64(3), 1U);
    RON_EXPECT_EQ(isqrt_u64(4), 2U);
    RON_EXPECT_EQ(isqrt_u64(8), 2U);
    RON_EXPECT_EQ(isqrt_u64(9), 3U);
    RON_EXPECT_EQ(isqrt_u64(15), 3U);
    RON_EXPECT_EQ(isqrt_u64(16), 4U);
    RON_EXPECT_EQ(isqrt_u64(0xFFFF'FFFF'FFFF'FFFFULL), 0xFFFF'FFFFU);

    // Continental distance test across full 4096m theater of war
    // distance({0, 0}, {4096, 4096}) equals 5792.6187m within <= 2 ULP (379,625,062)
    const Vec2fx origin{fx32::from_int(0), fx32::from_int(0)};
    const Vec2fx corner{fx32::from_int(4096), fx32::from_int(4096)};
    const fx64 continental_dist_sq = origin.distance_sq(corner);
    RON_EXPECT_EQ(continental_dist_sq.raw, (1LL << 57));
    const fx32 continental_dist = origin.distance(corner);
    RON_EXPECT_TRUE(continental_dist.raw > fx32::lit(181.02).raw);

    constexpr i32 EXPECTED_RAW = 379625062; // 5792.61875m in Q16.16
    const i32 diff = (continental_dist.raw > EXPECTED_RAW) ? (continental_dist.raw - EXPECTED_RAW) : (EXPECTED_RAW - continental_dist.raw);
    RON_EXPECT_TRUE(diff <= 2);

    // Verify normalize_safe on 1mm micro-vector and 4km macro-vector yields length within <= 3 ULP of one()
    const Vec2fx micro{fx32::from_ratio(1, 1000), fx32::from_ratio(2, 1000)};
    const Vec2fx norm_micro = normalize_safe(micro);
    const fx32 len_micro = norm_micro.length();
    const i32 diff_micro = (len_micro.raw > fx32::RAW_ONE) ? (len_micro.raw - fx32::RAW_ONE) : (fx32::RAW_ONE - len_micro.raw);
    RON_EXPECT_TRUE(diff_micro <= 3);

    const Vec2fx macro{fx32::from_int(3500), fx32::from_int(2100)};
    const Vec2fx norm_macro = normalize_safe(macro);
    const fx32 len_macro = norm_macro.length();
    const i32 diff_macro = (len_macro.raw > fx32::RAW_ONE) ? (len_macro.raw - fx32::RAW_ONE) : (fx32::RAW_ONE - len_macro.raw);
    RON_EXPECT_TRUE(diff_macro <= 3);
}

// -------------------------------------------------------------------------
// 3. TrigBam_ExhaustiveAll65536AnglesAndCordic
// -------------------------------------------------------------------------
RON_TEST(TrigBam_ExhaustiveAll65536AnglesAndCordic) {
    // Cardinal angles verification
    RON_EXPECT_EQ(cos_bam(Angle16::DEG_0).raw, fx32::one().raw);
    RON_EXPECT_EQ(sin_bam(Angle16::DEG_0).raw, fx32::zero().raw);

    RON_EXPECT_EQ(cos_bam(Angle16::DEG_90).raw, fx32::zero().raw);
    RON_EXPECT_EQ(sin_bam(Angle16::DEG_90).raw, fx32::one().raw);

    RON_EXPECT_EQ(cos_bam(Angle16::DEG_180).raw, fx32::minus_one().raw);
    RON_EXPECT_EQ(sin_bam(Angle16::DEG_180).raw, fx32::zero().raw);

    RON_EXPECT_EQ(cos_bam(Angle16::DEG_270).raw, fx32::zero().raw);
    RON_EXPECT_EQ(sin_bam(Angle16::DEG_270).raw, fx32::minus_one().raw);

    // Exhaustive unit-circle verification for all 65,536 BAM angles
    // Assert sin^2(a) + cos^2(a) within <= 3 ULP of fx32::one()
    // Assert atan2_bam(sin, cos) matches a within <= 2 BAM units
    for (u32 a = 0; a < 65536; ++a) {
        const Angle16 ang{static_cast<u16>(a)};
        const fx32 s = sin_bam(ang);
        const fx32 c = cos_bam(ang);

        const fx64 sum_sq = fx64::mul_exact(s, s) + fx64::mul_exact(c, c);
        const fx32 radius = sqrt_fx64(sum_sq);

        const i32 rad_err = (radius.raw > fx32::RAW_ONE) ? (radius.raw - fx32::RAW_ONE) : (fx32::RAW_ONE - radius.raw);
        RON_CHECK(rad_err <= 3);

        const Angle16 recovered = atan2_bam(s, c);
        const i16 delta = shortest_delta_bam(ang, recovered);
        const i32 abs_delta = (delta < 0) ? -delta : delta;
        RON_CHECK(abs_delta <= 2);
    }

    // Shortest delta across seam: 350 deg to 10 deg is +20 deg (not -340 deg)
    const Angle16 a350 = Angle16::from_degrees_int(350);
    const Angle16 a10  = Angle16::from_degrees_int(10);
    const i16 d_seam = shortest_delta_bam(a350, a10);
    const i32 d_deg = (static_cast<i64>(d_seam) * 360 + 32768) / 65536;
    RON_EXPECT_EQ(d_deg, 20);
}

// -------------------------------------------------------------------------
// 4. SweptCollision_HypersonicAntiTunneling
// -------------------------------------------------------------------------
RON_TEST(SweptCollision_HypersonicAntiTunneling) {
    // Mach-8 Hypersonic Glide Vehicle stepping 140.0m in 1 tick (p0 = {100, 200}, p1 = {240, 200})
    const Vec2fx p0{fx32::from_int(100), fx32::from_int(200)};
    const Vec2fx p1{fx32::from_int(240), fx32::from_int(200)};

    // ABM target at {170, 201.95} (radius 2.0m) -> grazing hit
    const Circle2fx circle_hit{
        Vec2fx{fx32::from_int(170), fx32::lit(201.95)},
        fx32::from_int(2)
    };

    fx32 hit_t{};
    const bool is_hit = segment_circle_hit(p0, p1, circle_hit, hit_t);
    RON_EXPECT_TRUE(is_hit);

    // Exact entry chord: t ≈ (70 - sqrt(4 - 1.95^2)) / 140
    // 4 - 1.95^2 = 4 - 3.8025 = 0.1975; sqrt(0.1975) ≈ 0.4444097
    // t ≈ (70 - 0.4444097) / 140 ≈ 69.55559 / 140 ≈ 0.4968256
    constexpr i32 EXPECTED_T_RAW = static_cast<i32>(0.4968256 * 65536.0);
    const i32 diff_t = (hit_t.raw > EXPECTED_T_RAW) ? (hit_t.raw - EXPECTED_T_RAW) : (EXPECTED_T_RAW - hit_t.raw);
    RON_EXPECT_TRUE(diff_t <= 5);

    // ABM target at {170, 202.05} (radius 2.0m) -> clean miss
    const Circle2fx circle_miss{
        Vec2fx{fx32::from_int(170), fx32::lit(202.05)},
        fx32::from_int(2)
    };
    fx32 miss_t{};
    const bool is_miss = segment_circle_hit(p0, p1, circle_miss, miss_t);
    RON_EXPECT_TRUE(!is_miss);

    // Starting inside circle (p0 = {170, 200.5}) returns true with out_t == 0
    const Vec2fx p0_inside{fx32::from_int(170), fx32::lit(200.5)};
    fx32 inside_t{};
    const bool is_inside_hit = segment_circle_hit(p0_inside, p1, circle_hit, inside_t);
    RON_EXPECT_TRUE(is_inside_hit);
    RON_EXPECT_EQ(inside_t.raw, 0);
}

// -------------------------------------------------------------------------
// 5. GridRaycast_SupercoverWallIntegrity
// -------------------------------------------------------------------------
RON_TEST(GridRaycast_SupercoverWallIntegrity) {
    // 64x64 grid (4.0m cells, pow2 = 2) with diagonal checkerboard wall (x + z == 32)
    auto is_blocked = [](i32 x, i32 z) {
        return (x + z) == 32;
    };

    DeterministicRng rng(0x88889999ULL);
    constexpr int NUM_RAYS = 500;
    int blocked_count = 0;

    for (int i = 0; i < NUM_RAYS; ++i) {
        fx32 start_x, start_z;
        do {
            start_x = fx32::from_int(4 + static_cast<i32>(rng.range_u32(53)));
            start_z = fx32::from_int(4 + static_cast<i32>(rng.range_u32(53)));
        } while (((start_x.to_int_floor() >> 2) + (start_z.to_int_floor() >> 2)) > 28);

        fx32 end_x, end_z;
        do {
            end_x = fx32::from_int(150 + static_cast<i32>(rng.range_u32(91)));
            end_z = fx32::from_int(150 + static_cast<i32>(rng.range_u32(91)));
        } while (((end_x.to_int_floor() >> 2) + (end_z.to_int_floor() >> 2)) < 36);

        const Vec2fx start{start_x, start_z};
        const Vec2fx end{end_x, end_z};

        bool was_blocked = false;
        grid_raycast_supercover(start, end, 2, 64, 64, [&](i32 cx, i32 cz) {
            RON_CHECK(cx >= 0 && cx < 64);
            RON_CHECK(cz >= 0 && cz < 64);
            if (is_blocked(cx, cz)) {
                was_blocked = true;
                return false; // Stop ray on barrier contact
            }
            return true;
        });

        if (was_blocked) {
            blocked_count++;
        }
    }

    // 100% of rays crossing the diagonal barrier must be blocked (zero diagonal corner leaks!)
    RON_EXPECT_EQ(blocked_count, NUM_RAYS);
}

// -------------------------------------------------------------------------
// 6. BallisticIntercept_MovingTargetLeadAccuracy
// -------------------------------------------------------------------------
RON_TEST(BallisticIntercept_MovingTargetLeadAccuracy) {
    // Shooter at {500, 500} fires 800 m/s C-RAM rounds
    const Vec2fx shooter{fx32::from_int(500), fx32::from_int(500)};
    const fx32 proj_speed = fx32::from_int(800);

    // FPV drone at {820, 410} flying { -45 m/s, +65 m/s }
    const Vec2fx target_pos{fx32::from_int(820), fx32::from_int(410)};
    const Vec2fx target_vel{fx32::from_int(-45), fx32::from_int(65)};

    const auto res = solve_linear_intercept_2d(shooter, proj_speed, target_pos, target_vel);
    RON_EXPECT_TRUE(res.is_ok());

    const Vec2fx aim = res.value;

    // Flight time t = distance(shooter, aim) / 800
    const fx32 dist_to_aim = shooter.distance(aim);
    const fx32 t = dist_to_aim / proj_speed;

    // Advance drone: drone_at_t = target_pos + target_vel * t
    const Vec2fx drone_at_t = target_pos + target_vel * t;

    // Assert distance(aim, drone_at_t) < 0.02m (< 2 cm error!)
    const fx32 error_dist = aim.distance(drone_at_t);
    const fx32 max_allowed_error = fx32::from_ratio(2, 100);
    RON_EXPECT_TRUE(error_dist < max_allowed_error);

    // Supersonic jet fleeing at 950 m/s away from a 600 m/s missile -> OutOfBounds
    const Vec2fx jet_pos{fx32::from_int(1000), fx32::from_int(1000)};
    const Vec2fx jet_vel{fx32::from_int(950), fx32::from_int(0)};
    const auto flee_res = solve_linear_intercept_2d(shooter, fx32::from_int(600), jet_pos, jet_vel);
    RON_EXPECT_TRUE(flee_res.is_err());
    RON_EXPECT_EQ(flee_res.error, ErrorCode::OutOfBounds);
}

// -------------------------------------------------------------------------
// 7. Determinism_MillionOpHashAndBenchmark
// -------------------------------------------------------------------------
RON_TEST(Determinism_MillionOpHashAndBenchmark) {
    MemoryArena arena;
    RON_EXPECT_TRUE(arena.init(MiB(8), MiB(4), "BenchArena").is_ok());

    auto run_ops = [&arena]() -> u64 {
        arena.reset();
        u64* out_hashes = arena.alloc<u64>(250000);

        DeterministicRng rng(0xDEADBEEFCAFEULL);
        for (u32 i = 0; i < 250000; ++i) {
            const Vec3fx v3{
                fx32::from_raw(static_cast<i32>(rng.next_u32() & 0x7FFF'FFFF)),
                fx32::from_raw(static_cast<i32>(rng.next_u32() & 0x7FFF'FFFF)),
                fx32::from_raw(static_cast<i32>(rng.next_u32() & 0x7FFF'FFFF))
            };
            const Vec3fx n3 = normalize_safe(v3);

            const Angle16 ang = atan2_bam(n3.z, n3.x);
            const fx32 s = sin_bam(ang);

            const Vec2fx p0{fx32::from_int(100), fx32::from_int(200)};
            const Vec2fx p1{p0.x + s * fx32::from_int(10), p0.z + n3.y * fx32::from_int(10)};
            const Circle2fx circle{Vec2fx{fx32::from_int(105), fx32::from_int(205)}, fx32::from_int(3)};

            fx32 out_t{};
            const bool hit = segment_circle_hit(p0, p1, circle, out_t);

            out_hashes[i] = (static_cast<u64>(n3.x.raw) << 32) ^ static_cast<u64>(ang.raw) ^ (hit ? 0xFULL : 0ULL);
        }

        return xxh64(arena.used_span(), 0);
    };

    // Warm-up run
    const u64 hash_warmup = run_ops();
    (void)hash_warmup;

    // Time 5 sample runs, compute median
    f64 samples[5];
    u64 last_hash = 0;
    for (int s = 0; s < 5; ++s) {
        const u64 t0 = clock_now_ns();
        last_hash = run_ops();
        const u64 t1 = clock_now_ns();
        samples[s] = clock_ns_to_ms(t1 - t0);
    }
    std::sort(samples, samples + 5);
    const f64 median_ms = samples[2];
    std::printf("Gate 02 benchmark median: %.3f ms\n", median_ms);

    // Deterministic state equality
    RON_EXPECT_EQ(hash_warmup, last_hash);

    // Release performance budget: 250,000 complex math ops median < 40 ms
#if !RON_DEBUG
    RON_EXPECT_TRUE(median_ms < 40.0);
#else
    RON_EXPECT_TRUE(median_ms < 200.0);
#endif

    arena.shutdown();
}

// -------------------------------------------------------------------------
// 8. FixedMath_RegressionIsqrtMinUbAndTurnBounds
// -------------------------------------------------------------------------
RON_TEST(FixedMath_RegressionIsqrtMinUbAndTurnBounds) {
    static_assert(isqrt_u128((static_cast<u128>(0xFFFF'FFFF'FFFF'FFFFULL) << 64) | 0xFFFF'FFFF'FFFF'FFFFULL) == 0xFFFF'FFFF'FFFF'FFFFULL);
    static_assert(isqrt_u128(static_cast<u128>(1) << 126) == (1ULL << 63));
    static_assert(isqrt_u128((static_cast<u128>(1) << 126) - 1) == ((1ULL << 63) - 1));

    constexpr u128 u128_max = (static_cast<u128>(0xFFFF'FFFF'FFFF'FFFFULL) << 64) | 0xFFFF'FFFF'FFFF'FFFFULL;
    constexpr u128 pow2_126 = static_cast<u128>(1) << 126;
    constexpr u128 max_u64_sq = static_cast<u128>(0xFFFF'FFFF'FFFF'FFFFULL) * static_cast<u128>(0xFFFF'FFFF'FFFF'FFFFULL);
    struct SqrtCase {
        u128 in;
        u64 expected;
    };
    const SqrtCase cases[] = {
        {u128_max, 0xFFFF'FFFF'FFFF'FFFFULL},
        {pow2_126, 1ULL << 63},
        {pow2_126 - 1, (1ULL << 63) - 1},
        {max_u64_sq, 0xFFFF'FFFF'FFFF'FFFFULL},
        {max_u64_sq - 1, 0xFFFF'FFFF'FFFF'FFFEULL},
    };
    for (usize i = 0; i < 5; ++i) {
        const volatile usize idx = i;
        RON_EXPECT_EQ(isqrt_u128(cases[idx].in), cases[idx].expected);
    }

    constexpr fx32 min32{INT32_MIN};
    static_assert((-min32).raw == fx32::RAW_MAX);
    static_assert(abs_fx(min32).raw == fx32::RAW_MAX);
    RON_EXPECT_EQ((-min32).raw, fx32::max_val().raw);
    RON_EXPECT_EQ(abs_fx(min32).raw, fx32::max_val().raw);

    constexpr fx64 min64{INT64_MIN};
    static_assert((-min64).raw == fx64::RAW_MAX);
    static_assert(abs_fx(min64).raw == fx64::RAW_MAX);
    static_assert(min64.to_fx32_sat().raw == fx32::RAW_MIN);
    static_assert((min64 * fx64::zero()).raw == 0);
    static_assert(fx64::div_sat(min64, fx64::one()).raw == fx64::RAW_MIN);
    RON_EXPECT_EQ((-min64).raw, fx64::max_val().raw);
    RON_EXPECT_EQ(abs_fx(min64).raw, fx64::max_val().raw);
    RON_EXPECT_EQ(min64.to_fx32_sat().raw, fx32::min_val().raw);
    RON_EXPECT_EQ((min64 * fx64::zero()).raw, fx64::zero().raw);
    RON_EXPECT_EQ(fx64::div_sat(min64, fx64::one()).raw, fx64::min_val().raw);

    RON_EXPECT_EQ(atan2_bam(INT64_MIN, i64{0}).raw, Angle16::DEG_270.raw);
    RON_EXPECT_EQ(atan2_bam(i64{0}, INT64_MIN).raw, Angle16::DEG_180.raw);
    const Angle16 diag_min = atan2_bam(INT64_MIN, INT64_MIN);
    const i16 diag_err = shortest_delta_bam(diag_min, Angle16{40960});
    RON_EXPECT_TRUE(diag_err >= -2 && diag_err <= 2);

    RON_EXPECT_EQ(turn_towards_bam(Angle16{0}, Angle16{32768}, 40000).raw, 32768U);
    RON_EXPECT_EQ(turn_towards_bam(Angle16{0}, Angle16{32768}, 32768).raw, 32768U);
    RON_EXPECT_EQ(turn_towards_bam(Angle16{0}, Angle16{16384}, 40000).raw, 16384U);
    RON_EXPECT_EQ(turn_towards_bam(Angle16{0}, Angle16{49152}, 40000).raw, 49152U);
    RON_EXPECT_EQ(turn_towards_bam(Angle16{0}, Angle16{1000}, 500).raw, 500U);
    RON_EXPECT_EQ(turn_towards_bam(Angle16{0}, Angle16{64536}, 500).raw, 65036U);
}
