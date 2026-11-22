#include "fixed_geom.hpp"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

namespace ron {

Vec2fx closest_point_on_segment(Vec2fx p, Vec2fx a, Vec2fx b, fx32* out_t) noexcept {
    const Vec2fx d = b - a;
    const fx64 len_sq = d.length_sq();
    if (len_sq.raw == 0) {
        if (out_t) *out_t = fx32::zero();
        return a;
    }

    const Vec2fx ap = p - a;
    const fx64 proj = ap.dot_exact(d);
    if (proj.raw <= 0) {
        if (out_t) *out_t = fx32::zero();
        return a;
    }
    if (proj.raw >= len_sq.raw) {
        if (out_t) *out_t = fx32::one();
        return b;
    }

    const fx32 t = fx64::div_sat(proj, len_sq).to_fx32_sat();
    if (out_t) *out_t = t;
    return a + d * t;
}

Result<Vec2fx> solve_linear_intercept_2d(Vec2fx shooter_pos, fx32 projectile_speed,
                                        Vec2fx target_pos, Vec2fx target_vel) noexcept {
    if (projectile_speed.raw <= 0) {
        return Result<Vec2fx>::err(ErrorCode::InvalidArgument);
    }

    const Vec2fx D = target_pos - shooter_pos;
    if (D.x.raw == 0 && D.z.raw == 0) {
        return Result<Vec2fx>::ok(target_pos);
    }

    const fx64 v_target_sq = target_vel.length_sq();
    const fx64 v_proj_sq = fx64::mul_exact(projectile_speed, projectile_speed);
    const fx64 A = v_target_sq - v_proj_sq;
    const fx64 H = D.dot_exact(target_vel);
    const fx64 C = D.length_sq();

    const i128 A_128 = A.raw;
    const i128 H_128 = H.raw;
    const i128 C_128 = C.raw;

    if (A_128 == 0) {
        // Equal speed linear degenerate case: 2 * H * t + C = 0
        if (H_128 >= 0) {
            return Result<Vec2fx>::err(ErrorCode::OutOfBounds);
        }
        const i128 num = -C_128;
        const i128 den = 2 * H_128;
        const i64 t_raw = static_cast<i64>((num << 16) / den);
        const fx32 t = fx32::from_raw(t_raw);
        const Vec2fx pt = target_pos + target_vel * t;
        return Result<Vec2fx>::ok(pt);
    }

    const i128 disc_128 = H_128 * H_128 - A_128 * C_128;
    if (disc_128 < 0) {
        return Result<Vec2fx>::err(ErrorCode::OutOfBounds);
    }

    const u64 sqrt_disc = isqrt_u128(static_cast<u128>(disc_128));
    const i128 s_disc = static_cast<i128>(sqrt_disc);

    const i128 num1 = -H_128 - s_disc;
    const i128 num2 = -H_128 + s_disc;

    i64 best_t_q16 = -1;

    if (A_128 < 0) {
        // Projectile faster than target: num1 / A_128 is positive root
        best_t_q16 = static_cast<i64>((num1 << 16) / A_128);
    } else {
        // Target faster than projectile: intercept only if heading towards shooter
        const i64 t1 = (num1 > 0) ? static_cast<i64>((num1 << 16) / A_128) : -1;
        const i64 t2 = (num2 > 0) ? static_cast<i64>((num2 << 16) / A_128) : -1;
        if (t1 > 0 && t2 > 0) {
            best_t_q16 = (t1 < t2) ? t1 : t2;
        } else if (t1 > 0) {
            best_t_q16 = t1;
        } else if (t2 > 0) {
            best_t_q16 = t2;
        }
    }

    if (best_t_q16 <= 0) {
        return Result<Vec2fx>::err(ErrorCode::OutOfBounds);
    }

    const fx32 t = fx32::from_raw(best_t_q16);
    const Vec2fx pt = target_pos + target_vel * t;

    // Check theater bounds [-512m, 4608m]
    constexpr i32 B_MIN = -512 * fx32::RAW_ONE;
    constexpr i32 B_MAX = 4608 * fx32::RAW_ONE;
    if (pt.x.raw < B_MIN || pt.x.raw > B_MAX || pt.z.raw < B_MIN || pt.z.raw > B_MAX) {
        return Result<Vec2fx>::err(ErrorCode::OutOfBounds);
    }

    return Result<Vec2fx>::ok(pt);
}

} // namespace ron

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
