#pragma once

#include "types.hpp"
#include "assert_log.hpp"
#include <bit>
#include <compare>
#include <algorithm>

namespace ron {

// Forward declarations
struct fx32;
struct fx64;
struct Angle16;

// -------------------------------------------------------------------------
// Integer Square Root Primitives (3-step LUT-seeded Newton & Bit-normalized Newton)
// -------------------------------------------------------------------------

namespace detail {

struct Isqrt512Lut {
    u8 table[512];
    consteval Isqrt512Lut() : table{} {
        for (u32 i = 0; i < 512; ++i) {
            u32 r = 0;
            while ((r + 1) * (r + 1) <= i) ++r;
            table[i] = static_cast<u8>(r);
        }
    }
};

inline constexpr Isqrt512Lut G_ISQRT_512_LUT{};

} // namespace detail

RON_FORCE_INLINE constexpr u32 isqrt_u64(u64 n) noexcept {
    if (n == 0) return 0;
    const int lz = std::countl_zero(n);
    const int bits = 64 - lz;
    if (bits <= 8) {
        return detail::G_ISQRT_512_LUT.table[n];
    }
    const int s = (bits - 8) & ~1;
    const u64 top = n >> s;
    u64 x = static_cast<u64>(detail::G_ISQRT_512_LUT.table[top]) << (s / 2);
    x = (x + n / x) >> 1;
    x = (x + n / x) >> 1;
    x = (x + n / x) >> 1;
    if (x > 0xFFFF'FFFFULL || x * x > n) --x;
    return static_cast<u32>(x);
}

#if defined(__x86_64__) || defined(_M_X64)
RON_FORCE_INLINE constexpr u64 div128_by_64(u128 n, u64 divisor) noexcept {
    if !consteval {
        const u64 hi = static_cast<u64>(n >> 64);
        const u64 lo = static_cast<u64>(n);
        if (hi < divisor) {
            u64 quot, rem;
            asm("divq %4" : "=a"(quot), "=d"(rem) : "0"(lo), "1"(hi), "rm"(divisor));
            return quot;
        }
    }
    return static_cast<u64>(n / divisor);
}
#else
RON_FORCE_INLINE constexpr u64 div128_by_64(u128 n, u64 divisor) noexcept {
    return static_cast<u64>(n / divisor);
}
#endif

RON_FORCE_INLINE constexpr u64 isqrt_u128(u128 n) noexcept {
    if (n == 0) return 0;
    const u64 hi = static_cast<u64>(n >> 64);
    if (hi == 0) {
        return isqrt_u64(static_cast<u64>(n));
    }

    const int lz = std::countl_zero(hi);
    const int k = 128 - lz;
    const int s = (k - 62) & ~1;
    const u64 n64 = static_cast<u64>(n >> s);
    u128 x = static_cast<u128>(isqrt_u64(n64)) << (s / 2);

    auto div_n = [n, hi](u128 d) constexpr noexcept -> u128 {
        if !consteval {
            if (d <= 0xFFFF'FFFF'FFFF'FFFFULL && hi < static_cast<u64>(d)) {
                return static_cast<u128>(div128_by_64(n, static_cast<u64>(d)));
            }
        }
        return n / d;
    };

    x = (x + div_n(x)) >> 1;
    x = (x + div_n(x)) >> 1;
    while (x > div_n(x)) {
        --x;
    }
    return static_cast<u64>(x);
}

// -------------------------------------------------------------------------
// fx32: Q16.16 Fixed-Point Scalar
// -------------------------------------------------------------------------

struct alignas(4) fx32 {
    i32 raw{0};

    static constexpr i32 RAW_ONE  = 65536;
    static constexpr i32 RAW_HALF = 32768;
    static constexpr i32 RAW_MAX  = 0x7FFF'FFFF;
    static constexpr i32 RAW_MIN  = -0x7FFF'FFFF;

    static constexpr fx32 zero()      noexcept { return fx32{0}; }
    static constexpr fx32 one()       noexcept { return fx32{RAW_ONE}; }
    static constexpr fx32 half()      noexcept { return fx32{RAW_HALF}; }
    static constexpr fx32 minus_one() noexcept { return fx32{-RAW_ONE}; }
    static constexpr fx32 epsilon()   noexcept { return fx32{1}; }
    static constexpr fx32 max_val()   noexcept { return fx32{RAW_MAX}; }
    static constexpr fx32 min_val()   noexcept { return fx32{RAW_MIN}; }
    static constexpr fx32 pi()        noexcept { return fx32{205887}; }
    static constexpr fx32 two_pi()    noexcept { return fx32{411775}; }
    static constexpr fx32 half_pi()   noexcept { return fx32{102944}; }

    static constexpr fx32 from_raw(i64 r) noexcept {
        if (r > RAW_MAX) r = RAW_MAX;
        if (r < RAW_MIN) r = RAW_MIN;
        return fx32{static_cast<i32>(r)};
    }

    static constexpr fx32 from_int(i32 v) noexcept {
        return from_raw(static_cast<i64>(v) * RAW_ONE);
    }

    static constexpr fx32 div_sat(fx32 a, fx32 b) noexcept {
        if (b.raw == 0) {
            if (a.raw == 0) return zero();
            return (a.raw > 0) ? max_val() : min_val();
        }
        const bool neg = (a.raw < 0) ^ (b.raw < 0);
        const u64 ua = static_cast<u64>(a.raw < 0 ? -static_cast<i64>(a.raw) : static_cast<i64>(a.raw));
        const u64 ub = static_cast<u64>(b.raw < 0 ? -static_cast<i64>(b.raw) : static_cast<i64>(b.raw));
        const u64 q = ((ua << 16) + (ub >> 1)) / ub;
        const i64 clamped = (q > static_cast<u64>(RAW_MAX)) ? static_cast<i64>(RAW_MAX) : static_cast<i64>(q);
        return from_raw(neg ? -clamped : clamped);
    }

    static constexpr fx32 from_ratio(i32 num, i32 den) noexcept {
        return div_sat(from_int(num), from_int(den));
    }

    static consteval fx32 lit(double v) noexcept {
        const double scaled = v * 65536.0;
        const double rounded = (scaled >= 0.0) ? (scaled + 0.5) : (scaled - 0.5);
        i64 r = static_cast<i64>(rounded);
        if (r > RAW_MAX) r = RAW_MAX;
        if (r < RAW_MIN) r = RAW_MIN;
        return fx32{static_cast<i32>(r)};
    }

    constexpr i32 to_int_trunc() const noexcept {
        return raw / RAW_ONE;
    }

    constexpr i32 to_int_floor() const noexcept {
        return raw >> 16;
    }

#if !defined(RON_SIM_NO_FLOAT)
    constexpr f32 to_render_f32() const noexcept {
        return static_cast<f32>(raw) * (1.0f / 65536.0f);
    }
#endif

    constexpr fx32 operator+() const noexcept { return *this; }
    constexpr fx32 operator-() const noexcept {
        return (raw <= RAW_MIN) ? max_val() : fx32{static_cast<i32>(0u - static_cast<u32>(raw))};
    }

    constexpr fx32 operator+(fx32 b) const noexcept {
        return from_raw(static_cast<i64>(raw) + b.raw);
    }

    constexpr fx32 operator-(fx32 b) const noexcept {
        return from_raw(static_cast<i64>(raw) - b.raw);
    }

    constexpr fx32& operator+=(fx32 b) noexcept {
        *this = *this + b;
        return *this;
    }

    constexpr fx32& operator-=(fx32 b) noexcept {
        *this = *this - b;
        return *this;
    }

    constexpr fx32 operator*(fx32 b) const noexcept {
        const i64 prod = static_cast<i64>(raw) * static_cast<i64>(b.raw);
        const i64 mag = (prod < 0) ? -prod : prod;
        const i64 shifted = (mag + 0x8000LL) >> 16;
        return from_raw((prod < 0) ? -shifted : shifted);
    }

    constexpr fx32& operator*=(fx32 b) noexcept {
        *this = *this * b;
        return *this;
    }

    constexpr fx32 operator/(fx32 b) const noexcept {
        RON_ASSERT(b.raw != 0, "fx32 division by zero");
        return div_sat(*this, b);
    }

    constexpr fx32& operator/=(fx32 b) noexcept {
        *this = *this / b;
        return *this;
    }

    constexpr auto operator<=>(const fx32&) const = default;
    constexpr bool operator==(const fx32&) const = default;
};

static_assert(sizeof(fx32) == 4 && std::is_trivially_copyable_v<fx32> && std::has_unique_object_representations_v<fx32>);

constexpr fx32 abs_fx(fx32 a) noexcept {
    return (a.raw < 0) ? -a : a;
}

constexpr fx32 min_fx(fx32 a, fx32 b) noexcept {
    return a.raw < b.raw ? a : b;
}

constexpr fx32 max_fx(fx32 a, fx32 b) noexcept {
    return a.raw > b.raw ? a : b;
}

constexpr fx32 clamp_fx(fx32 v, fx32 lo, fx32 hi) noexcept {
    return v.raw < lo.raw ? lo : (v.raw > hi.raw ? hi : v);
}

constexpr fx32 lerp_fx(fx32 a, fx32 b, fx32 t) noexcept {
    const i64 diff = static_cast<i64>(b.raw) - static_cast<i64>(a.raw);
    const i64 prod = diff * static_cast<i64>(t.raw);
    const i64 mag = (prod < 0) ? -prod : prod;
    const i64 shifted = (mag + 0x8000LL) >> 16;
    const i64 delta = (prod < 0) ? -shifted : shifted;
    return fx32::from_raw(static_cast<i64>(a.raw) + delta);
}

// -------------------------------------------------------------------------
// fx64: Q32.32 Fixed-Point Scalar
// -------------------------------------------------------------------------

struct alignas(8) fx64 {
    i64 raw{0};

    static constexpr i64 RAW_ONE = 1LL << 32;
    static constexpr i64 RAW_MAX = 0x7FFF'FFFF'FFFF'FFFFLL;
    static constexpr i64 RAW_MIN = -0x7FFF'FFFF'FFFF'FFFFLL;

    static constexpr fx64 zero()    noexcept { return fx64{0}; }
    static constexpr fx64 one()     noexcept { return fx64{RAW_ONE}; }
    static constexpr fx64 max_val() noexcept { return fx64{RAW_MAX}; }
    static constexpr fx64 min_val() noexcept { return fx64{RAW_MIN}; }

    static constexpr fx64 from_raw(i128 r) noexcept {
        if (r > RAW_MAX) r = RAW_MAX;
        if (r < RAW_MIN) r = RAW_MIN;
        return fx64{static_cast<i64>(r)};
    }

    static constexpr fx64 from_fx32(fx32 v) noexcept {
        return fx64{static_cast<i64>(v.raw) << 16};
    }

    static constexpr fx64 from_int(i64 v) noexcept {
        return from_raw(static_cast<i128>(v) << 32);
    }

    constexpr fx32 to_fx32_sat() const noexcept {
        const bool neg = raw < 0;
        const u64 mag = neg ? (0ULL - static_cast<u64>(raw)) : static_cast<u64>(raw);
        const u64 rounded = (mag + 0x8000ULL) >> 16;
        const i64 r = neg ? -static_cast<i64>(rounded) : static_cast<i64>(rounded);
        return fx32::from_raw(r);
    }

    static constexpr fx64 mul_exact(fx32 a, fx32 b) noexcept {
        return fx64{static_cast<i64>(a.raw) * static_cast<i64>(b.raw)};
    }

    constexpr fx64 operator+() const noexcept { return *this; }
    constexpr fx64 operator-() const noexcept {
        return (raw <= RAW_MIN) ? max_val() : fx64{static_cast<i64>(0ULL - static_cast<u64>(raw))};
    }

    constexpr fx64 operator+(fx64 b) const noexcept {
        return from_raw(static_cast<i128>(raw) + b.raw);
    }

    constexpr fx64 operator-(fx64 b) const noexcept {
        return from_raw(static_cast<i128>(raw) - b.raw);
    }

    constexpr fx64& operator+=(fx64 b) noexcept {
        *this = *this + b;
        return *this;
    }

    constexpr fx64& operator-=(fx64 b) noexcept {
        *this = *this - b;
        return *this;
    }

    constexpr fx64 operator*(fx64 b) const noexcept {
        const bool neg = (raw < 0) ^ (b.raw < 0);
        const u64 ua = (raw < 0) ? (0ULL - static_cast<u64>(raw)) : static_cast<u64>(raw);
        const u64 ub = (b.raw < 0) ? (0ULL - static_cast<u64>(b.raw)) : static_cast<u64>(b.raw);
        u128 prod = static_cast<u128>(ua) * static_cast<u128>(ub);
        prod = (prod + 0x8000'0000ULL) >> 32;
        const i128 res = neg ? -static_cast<i128>(prod) : static_cast<i128>(prod);
        return from_raw(res);
    }

    constexpr fx64& operator*=(fx64 b) noexcept {
        *this = *this * b;
        return *this;
    }

    static constexpr fx64 div_sat(fx64 a, fx64 b) noexcept {
        if (b.raw == 0) {
            if (a.raw > 0) return max_val();
            if (a.raw < 0) return min_val();
            return zero();
        }
        const bool neg = (a.raw < 0) ^ (b.raw < 0);
        const u128 ua = (a.raw < 0) ? (0ULL - static_cast<u64>(a.raw)) : static_cast<u64>(a.raw);
        const u128 ub = (b.raw < 0) ? (0ULL - static_cast<u64>(b.raw)) : static_cast<u64>(b.raw);
        const u128 num = (ua << 32) + (ub >> 1);
        const u128 q = num / ub;
        const i128 res = neg ? -static_cast<i128>(q) : static_cast<i128>(q);
        return from_raw(res);
    }

    constexpr fx64 operator/(fx64 b) const noexcept {
        RON_ASSERT(b.raw != 0, "fx64 division by zero");
        return div_sat(*this, b);
    }

    constexpr fx64& operator/=(fx64 b) noexcept {
        *this = *this / b;
        return *this;
    }

    constexpr auto operator<=>(const fx64&) const = default;
    constexpr bool operator==(const fx64&) const = default;
};

static_assert(sizeof(fx64) == 8 && std::is_trivially_copyable_v<fx64> && std::has_unique_object_representations_v<fx64>);

constexpr fx64 abs_fx(fx64 a) noexcept {
    return (a.raw < 0) ? -a : a;
}

constexpr fx64 min_fx(fx64 a, fx64 b) noexcept {
    return a.raw < b.raw ? a : b;
}

constexpr fx64 max_fx(fx64 a, fx64 b) noexcept {
    return a.raw > b.raw ? a : b;
}

constexpr fx64 clamp_fx(fx64 v, fx64 lo, fx64 hi) noexcept {
    return v.raw < lo.raw ? lo : (v.raw > hi.raw ? hi : v);
}

// -------------------------------------------------------------------------
// Fixed Sqrt Functions
// -------------------------------------------------------------------------

constexpr fx32 sqrt_fx64(fx64 x) noexcept {
    RON_ASSERT(x.raw >= 0, "sqrt_fx64 negative input");
    if (x.raw <= 0) return fx32::zero();
    const u32 r = isqrt_u64(static_cast<u64>(x.raw));
    return fx32::from_raw(static_cast<i64>(r));
}

constexpr fx32 sqrt_fx32(fx32 x) noexcept {
    if (x.raw <= 0) return fx32::zero();
    return sqrt_fx64(fx64{static_cast<i64>(x.raw) << 16});
}

// -------------------------------------------------------------------------
// Angle16 & BAM Trigonometry
// -------------------------------------------------------------------------

struct alignas(2) Angle16 {
    u16 raw{0};

    static const Angle16 DEG_0;
    static const Angle16 DEG_45;
    static const Angle16 DEG_90;
    static const Angle16 DEG_180;
    static const Angle16 DEG_270;

    static constexpr Angle16 from_degrees_int(i32 deg) noexcept {
        i64 d = deg % 360;
        if (d < 0) d += 360;
        const u32 r = static_cast<u32>((d * 65536LL + 180) / 360);
        return Angle16{static_cast<u16>(r & 0xFFFF)};
    }

    static constexpr Angle16 from_deg(i32 deg) noexcept {
        return from_degrees_int(deg);
    }

    constexpr auto operator<=>(const Angle16&) const = default;
    constexpr bool operator==(const Angle16&) const = default;

    constexpr Angle16 operator+(Angle16 b) const noexcept {
        return Angle16{static_cast<u16>(raw + b.raw)};
    }

    constexpr Angle16 operator-(Angle16 b) const noexcept {
        return Angle16{static_cast<u16>(raw - b.raw)};
    }

    constexpr Angle16& operator+=(Angle16 b) noexcept {
        raw = static_cast<u16>(raw + b.raw);
        return *this;
    }

    constexpr Angle16& operator-=(Angle16 b) noexcept {
        raw = static_cast<u16>(raw - b.raw);
        return *this;
    }
};

inline constexpr Angle16 Angle16::DEG_0{0};
inline constexpr Angle16 Angle16::DEG_45{8192};
inline constexpr Angle16 Angle16::DEG_90{16384};
inline constexpr Angle16 Angle16::DEG_180{32768};
inline constexpr Angle16 Angle16::DEG_270{49152};

inline constexpr Angle16 DEG_0{0};
inline constexpr Angle16 DEG_45{8192};
inline constexpr Angle16 DEG_90{16384};
inline constexpr Angle16 DEG_180{32768};
inline constexpr Angle16 DEG_270{49152};

static_assert(sizeof(Angle16) == 2 && std::is_trivially_copyable_v<Angle16> && std::has_unique_object_representations_v<Angle16>);

constexpr i16 shortest_delta_bam(Angle16 from, Angle16 to) noexcept {
    return static_cast<i16>(static_cast<u16>(to.raw - from.raw));
}

constexpr Angle16 turn_towards_bam(Angle16 current, Angle16 target, u16 max_step_bam) noexcept {
    if (max_step_bam >= 32768u) {
        return target;
    }
    const i32 delta = static_cast<i32>(shortest_delta_bam(current, target));
    const i32 step = static_cast<i32>(max_step_bam);
    if (delta > step) {
        return current + Angle16{max_step_bam};
    }
    if (delta < -step) {
        return current - Angle16{max_step_bam};
    }
    return target;
}

// 1026-entry quarter wave compile-time LUT via pure __int128 Taylor series (NO <cmath>!)
namespace detail {

inline constexpr u64 HALF_PI_Q52 = 0x1921FB54442D18ULL;

consteval u32 compute_sin_q16_entry(u32 idx) noexcept {
    if (idx == 0) return 0;
    if (idx >= 1024) return 65536;

    const i128 x = (static_cast<i128>(idx) * HALF_PI_Q52) / 1024;
    const i128 x2 = (x * x) >> 52;

    const i128 t1 = x;
    const i128 t3 = ((t1 * x2) >> 52) / 6;
    const i128 t5 = ((t3 * x2) >> 52) / 20;
    const i128 t7 = ((t5 * x2) >> 52) / 42;
    const i128 t9 = ((t7 * x2) >> 52) / 72;
    const i128 t11 = ((t9 * x2) >> 52) / 110;
    const i128 t13 = ((t11 * x2) >> 52) / 156;

    const i128 sum = t1 - t3 + t5 - t7 + t9 - t11 + t13;
    const i128 q16_val = (sum + (static_cast<i128>(1) << 35)) >> 36;

    if (q16_val > 65536) return 65536;
    if (q16_val < 0) return 0;
    return static_cast<u32>(q16_val);
}

struct SinLutQuarter {
    u32 table[1026];
    consteval SinLutQuarter() : table{} {
        for (u32 i = 0; i <= 1024; ++i) {
            table[i] = compute_sin_q16_entry(i);
        }
        table[1025] = 65536;
    }
};

inline constexpr SinLutQuarter G_SIN_Q16_LUT_GEN{};

} // namespace detail

inline constexpr const u32* const SIN_Q16_LUT = detail::G_SIN_Q16_LUT_GEN.table;

RON_FORCE_INLINE fx32 sin_bam(Angle16 a) noexcept {
    const u32 q = a.raw >> 14;              // Quadrant 0..3
    const u32 offset = a.raw & 0x3FFFu;     // 0..16383
    const u32 idx16 = (q & 1u) ? (16384u - offset) : offset;
    const u32 i = idx16 >> 4;               // 0..1024
    const u32 frac = idx16 & 0xFu;          // 0..15

    const i32 v0 = static_cast<i32>(SIN_Q16_LUT[i]);
    const i32 v1 = static_cast<i32>(SIN_Q16_LUT[i + 1]);
    const i32 val = v0 + (((v1 - v0) * static_cast<i32>(frac) + 8) >> 4);

    return fx32{(q & 2u) ? -val : val};
}

RON_FORCE_INLINE fx32 cos_bam(Angle16 a) noexcept {
    return sin_bam(Angle16{static_cast<u16>(a.raw + 16384u)});
}

// 18-step CORDIC constants for atan2
inline constexpr i32 CORDIC_ATAN_SUB16[18] = {
    131072, 77376, 40884, 20753, 10417, 5214, 2608, 1304,
    652,    326,   163,   81,    41,    20,   10,   5,
    3,      1
};

// Deterministic CORDIC atan2 (branchless unrolled CORDIC loop)
RON_FORCE_INLINE Angle16 atan2_bam(i64 z_sin, i64 x_cos) noexcept {
    if (x_cos == 0 && z_sin == 0) {
        return Angle16::DEG_0;
    }
    if (x_cos == 0) {
        return (z_sin > 0) ? Angle16::DEG_90 : Angle16::DEG_270;
    }
    if (z_sin == 0) {
        return (x_cos > 0) ? Angle16::DEG_0 : Angle16::DEG_180;
    }

    const u64 ax = (x_cos < 0) ? (0ULL - static_cast<u64>(x_cos)) : static_cast<u64>(x_cos);
    const u64 az = (z_sin < 0) ? (0ULL - static_cast<u64>(z_sin)) : static_cast<u64>(z_sin);

    i64 cur_x = 0;
    i64 cur_z = 0;

    const u64 m = (ax > az) ? ax : az;
    if (m > (1ULL << 56)) {
        const int rshift = std::bit_width(m) - 56;
        cur_x = static_cast<i64>(ax >> rshift);
        cur_z = static_cast<i64>(az >> rshift);
    } else {
        const int s = std::clamp(47 - std::bit_width(m), 0, 47);
        cur_x = static_cast<i64>(ax << s);
        cur_z = static_cast<i64>(az << s);
    }

    i32 acc = 0;
    #pragma GCC unroll 18
    for (int i = 0; i < 18; ++i) {
        const bool neg = (cur_z < 0);
        const i64 dx = cur_z >> i;
        const i64 dz = cur_x >> i;
        cur_x += neg ? -dx : dx;
        cur_z += neg ? dz : -dz;
        acc += neg ? -CORDIC_ATAN_SUB16[i] : CORDIC_ATAN_SUB16[i];
    }

    const u32 ang = static_cast<u32>((acc + 8) >> 4);

    u32 final_bam = 0;
    if (x_cos >= 0 && z_sin >= 0) {
        final_bam = ang;
    } else if (x_cos < 0 && z_sin >= 0) {
        final_bam = 32768u - ang;
    } else if (x_cos < 0 && z_sin < 0) {
        final_bam = 32768u + ang;
    } else {
        final_bam = 65536u - ang;
    }

    return Angle16{static_cast<u16>(final_bam & 0xFFFFu)};
}

RON_FORCE_INLINE Angle16 atan2_bam(fx32 dz, fx32 dx) noexcept {
    return atan2_bam(static_cast<i64>(dz.raw), static_cast<i64>(dx.raw));
}

} // namespace ron
