#pragma once

#include "fixed_math.hpp"
#include "result.hpp"

namespace ron {

// -------------------------------------------------------------------------
// Vec2fx: 2D Horizontal Vector strictly on (X, Z) axes (Never Y)
// -------------------------------------------------------------------------

struct alignas(8) Vec2fx {
    fx32 x{};
    fx32 z{};

    constexpr auto operator<=>(const Vec2fx&) const = default;
    constexpr bool operator==(const Vec2fx&) const = default;

    constexpr Vec2fx operator+() const noexcept { return *this; }
    constexpr Vec2fx operator-() const noexcept { return {-x, -z}; }

    constexpr Vec2fx operator+(Vec2fx b) const noexcept { return {x + b.x, z + b.z}; }
    constexpr Vec2fx operator-(Vec2fx b) const noexcept { return {x - b.x, z - b.z}; }

    constexpr Vec2fx& operator+=(Vec2fx b) noexcept {
        x += b.x;
        z += b.z;
        return *this;
    }

    constexpr Vec2fx& operator-=(Vec2fx b) noexcept {
        x -= b.x;
        z -= b.z;
        return *this;
    }

    constexpr Vec2fx operator*(fx32 s) const noexcept { return {x * s, z * s}; }
    constexpr Vec2fx operator/(fx32 s) const noexcept { return {x / s, z / s}; }

    constexpr Vec2fx& operator*=(fx32 s) noexcept {
        x *= s;
        z *= s;
        return *this;
    }

    constexpr Vec2fx& operator/=(fx32 s) noexcept {
        x /= s;
        z /= s;
        return *this;
    }

    constexpr fx64 dot_exact(Vec2fx b) const noexcept {
        return fx64::mul_exact(x, b.x) + fx64::mul_exact(z, b.z);
    }

    constexpr fx64 cross_exact(Vec2fx b) const noexcept {
        return fx64::mul_exact(x, b.z) - fx64::mul_exact(z, b.x);
    }

    constexpr fx64 length_sq() const noexcept {
        return dot_exact(*this);
    }

    constexpr fx32 length() const noexcept {
        return sqrt_fx64(length_sq());
    }

    constexpr fx64 distance_sq(Vec2fx b) const noexcept {
        return (*this - b).length_sq();
    }

    constexpr fx32 distance(Vec2fx b) const noexcept {
        return (*this - b).length();
    }

    static constexpr Vec2fx from_angle(Angle16 heading, fx32 mag = fx32::one()) noexcept {
        return { cos_bam(heading) * mag, sin_bam(heading) * mag };
    }

    static Angle16 heading_of(Vec2fx v) noexcept {
        return atan2_bam(v.z, v.x);
    }

    static constexpr Vec2fx rotate_90_cw(Vec2fx v) noexcept {
        return {-v.z, v.x};
    }

    static constexpr Vec2fx rotate_90_ccw(Vec2fx v) noexcept {
        return {v.z, -v.x};
    }

    static constexpr Vec2fx lerp(Vec2fx a, Vec2fx b, fx32 t) noexcept {
        return { lerp_fx(a.x, b.x, t), lerp_fx(a.z, b.z, t) };
    }
};

static_assert(sizeof(Vec2fx) == 8 && std::is_trivially_copyable_v<Vec2fx> && std::has_unique_object_representations_v<Vec2fx>);

RON_FORCE_INLINE Vec2fx normalize_safe(Vec2fx v, Vec2fx fallback = {}) noexcept {
    if (v.x.raw == 0 && v.z.raw == 0) return fallback;
    const u64 ax = static_cast<u64>(v.x.raw < 0 ? -static_cast<i64>(v.x.raw) : static_cast<i64>(v.x.raw));
    const u64 az = static_cast<u64>(v.z.raw < 0 ? -static_cast<i64>(v.z.raw) : static_cast<i64>(v.z.raw));
    const u64 m = (ax > az) ? ax : az;
    if (m == 0) return fallback;

    int shift = 0;
    if (m < (1ULL << 24)) {
        shift = 24 - std::bit_width(m);
    } else if (m > (1ULL << 29)) {
        shift = 29 - std::bit_width(m);
    }

    const i64 sx = (shift >= 0) ? (static_cast<i64>(v.x.raw) << shift) : (static_cast<i64>(v.x.raw) >> (-shift));
    const i64 sz = (shift >= 0) ? (static_cast<i64>(v.z.raw) << shift) : (static_cast<i64>(v.z.raw) >> (-shift));

    const u64 len_sq = static_cast<u64>(sx * sx + sz * sz);
    const u32 len = isqrt_u64(len_sq);
    if (len == 0) return fallback;

    const u64 inv = (1ULL << 48) / len;
    auto scale_comp = [inv](i64 s) noexcept -> i64 {
        const bool neg = s < 0;
        const u64 u = neg ? -s : s;
        const u64 prod = (u * inv + (1ULL << 31)) >> 32;
        return neg ? -static_cast<i64>(prod) : static_cast<i64>(prod);
    };

    return { fx32::from_raw(scale_comp(sx)), fx32::from_raw(scale_comp(sz)) };
}

// -------------------------------------------------------------------------
// Vec3fx: 3D Vector with X=East, Y=Altitude Up, Z=South
// -------------------------------------------------------------------------

struct alignas(4) Vec3fx {
    fx32 x{};
    fx32 y{};
    fx32 z{};

    constexpr auto operator<=>(const Vec3fx&) const = default;
    constexpr bool operator==(const Vec3fx&) const = default;

    constexpr Vec2fx xz() const noexcept { return {x, z}; }

    static constexpr Vec3fx from_xz(Vec2fx p, fx32 elev_y = fx32::zero()) noexcept {
        return {p.x, elev_y, p.z};
    }

    constexpr Vec3fx operator+() const noexcept { return *this; }
    constexpr Vec3fx operator-() const noexcept { return {-x, -y, -z}; }

    constexpr Vec3fx operator+(Vec3fx b) const noexcept { return {x + b.x, y + b.y, z + b.z}; }
    constexpr Vec3fx operator-(Vec3fx b) const noexcept { return {x - b.x, y - b.y, z - b.z}; }

    constexpr Vec3fx& operator+=(Vec3fx b) noexcept {
        x += b.x;
        y += b.y;
        z += b.z;
        return *this;
    }

    constexpr Vec3fx& operator-=(Vec3fx b) noexcept {
        x -= b.x;
        y -= b.y;
        z -= b.z;
        return *this;
    }

    constexpr Vec3fx operator*(fx32 s) const noexcept { return {x * s, y * s, z * s}; }
    constexpr Vec3fx operator/(fx32 s) const noexcept { return {x / s, y / s, z / s}; }

    constexpr Vec3fx& operator*=(fx32 s) noexcept {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }

    constexpr Vec3fx& operator/=(fx32 s) noexcept {
        x /= s;
        y /= s;
        z /= s;
        return *this;
    }

    constexpr fx64 dot_exact(Vec3fx b) const noexcept {
        return fx64::mul_exact(x, b.x) + fx64::mul_exact(y, b.y) + fx64::mul_exact(z, b.z);
    }

    constexpr Vec3fx cross(Vec3fx b) const noexcept {
        return {
            y * b.z - z * b.y,
            z * b.x - x * b.z,
            x * b.y - y * b.x
        };
    }

    constexpr fx64 length_sq() const noexcept {
        return dot_exact(*this);
    }

    constexpr fx32 length() const noexcept {
        return sqrt_fx64(length_sq());
    }

    constexpr fx64 distance_sq(Vec3fx b) const noexcept {
        return (*this - b).length_sq();
    }

    constexpr fx32 distance(Vec3fx b) const noexcept {
        return (*this - b).length();
    }

    static constexpr Vec3fx lerp(Vec3fx a, Vec3fx b, fx32 t) noexcept {
        return { lerp_fx(a.x, b.x, t), lerp_fx(a.y, b.y, t), lerp_fx(a.z, b.z, t) };
    }
};

static_assert(sizeof(Vec3fx) == 12 && std::is_trivially_copyable_v<Vec3fx> && std::has_unique_object_representations_v<Vec3fx>);

RON_FORCE_INLINE Vec3fx normalize_safe(Vec3fx v, Vec3fx fallback = {}) noexcept {
    if (v.x.raw == 0 && v.y.raw == 0 && v.z.raw == 0) return fallback;
    const u64 ax = static_cast<u64>(v.x.raw < 0 ? -static_cast<i64>(v.x.raw) : static_cast<i64>(v.x.raw));
    const u64 ay = static_cast<u64>(v.y.raw < 0 ? -static_cast<i64>(v.y.raw) : static_cast<i64>(v.y.raw));
    const u64 az = static_cast<u64>(v.z.raw < 0 ? -static_cast<i64>(v.z.raw) : static_cast<i64>(v.z.raw));
    u64 m = ax;
    if (ay > m) m = ay;
    if (az > m) m = az;
    if (m == 0) return fallback;

    int shift = 0;
    if (m < (1ULL << 24)) {
        shift = 24 - (64 - std::countl_zero(m));
    } else if (m > (1ULL << 29)) {
        shift = 29 - (64 - std::countl_zero(m));
    }

    i64 sx, sy, sz;
    if (shift >= 0) {
        sx = static_cast<i64>(v.x.raw) << shift;
        sy = static_cast<i64>(v.y.raw) << shift;
        sz = static_cast<i64>(v.z.raw) << shift;
    } else {
        const int rshift = -shift;
        sx = static_cast<i64>(v.x.raw) >> rshift;
        sy = static_cast<i64>(v.y.raw) >> rshift;
        sz = static_cast<i64>(v.z.raw) >> rshift;
    }

    const u64 len_sq = static_cast<u64>(sx * sx + sy * sy + sz * sz);
    const u32 len = isqrt_u64(len_sq);
    if (len == 0) return fallback;

    const u64 inv = (1ULL << 48) / len;
    auto scale_comp = [inv](i64 s) noexcept -> i64 {
        if (s >= 0) {
            return static_cast<i64>((static_cast<u64>(s) * inv + 0x8000'0000ULL) >> 32);
        } else {
            return -static_cast<i64>((static_cast<u64>(-s) * inv + 0x8000'0000ULL) >> 32);
        }
    };

    return { fx32::from_raw(scale_comp(sx)), fx32::from_raw(scale_comp(sy)), fx32::from_raw(scale_comp(sz)) };
}

// -------------------------------------------------------------------------
// Geometry Shapes: AABB2fx & Circle2fx
// -------------------------------------------------------------------------

struct alignas(8) AABB2fx {
    Vec2fx min{};
    Vec2fx max{};

    constexpr bool contains(Vec2fx p) const noexcept {
        return p.x >= min.x && p.x <= max.x && p.z >= min.z && p.z <= max.z;
    }

    constexpr bool overlaps(AABB2fx o) const noexcept {
        return max.x >= o.min.x && min.x <= o.max.x &&
               max.z >= o.min.z && min.z <= o.max.z;
    }

    constexpr Vec2fx closest_point(Vec2fx p) const noexcept {
        return {
            clamp_fx(p.x, min.x, max.x),
            clamp_fx(p.z, min.z, max.z)
        };
    }

    constexpr fx64 distance_sq_to_point(Vec2fx p) const noexcept {
        return p.distance_sq(closest_point(p));
    }

    constexpr AABB2fx expand(fx32 margin) const noexcept {
        return {
            {min.x - margin, min.z - margin},
            {max.x + margin, max.z + margin}
        };
    }
};

struct alignas(8) Circle2fx {
    Vec2fx center{};
    fx32 radius{};

    constexpr bool contains(Vec2fx p) const noexcept {
        return center.distance_sq(p) <= fx64::mul_exact(radius, radius);
    }

    constexpr bool overlaps_circle(Circle2fx o) const noexcept {
        const fx32 r_sum = radius + o.radius;
        return center.distance_sq(o.center) <= fx64::mul_exact(r_sum, r_sum);
    }

    constexpr bool overlaps_aabb(AABB2fx box) const noexcept {
        return box.distance_sq_to_point(center) <= fx64::mul_exact(radius, radius);
    }
};

// -------------------------------------------------------------------------
// Tactical & Spatial Solvers
// -------------------------------------------------------------------------

RON_FORCE_INLINE bool segment_circle_hit(Vec2fx p0, Vec2fx p1, Circle2fx circle, fx32& out_t_0_to_1) noexcept {
    const Vec2fx d = p1 - p0;
    const Vec2fx f = p0 - circle.center;

    const fx64 r_sq = fx64::mul_exact(circle.radius, circle.radius);
    const fx64 f_sq = f.length_sq();
    const fx64 C = f_sq - r_sq;

    if (C.raw <= 0) {
        out_t_0_to_1 = fx32::zero();
        return true;
    }

    const fx64 H = f.dot_exact(d);
    if (H.raw > 0) {
        return false;
    }

    const fx64 A = d.length_sq();
    if (A.raw == 0) {
        return false;
    }

    const i128 A_128 = A.raw;
    const i128 H_128 = H.raw;
    const i128 C_128 = C.raw;

    const i128 disc_128 = H_128 * H_128 - A_128 * C_128;
    if (disc_128 < 0) {
        return false;
    }

    const u64 sqrt_disc = isqrt_u128(static_cast<u128>(disc_128));
    const i128 num = -H_128 - static_cast<i128>(sqrt_disc);

    if (num < 0 || num > A_128) {
        return false;
    }

    i64 t_q16;
    if (num <= (static_cast<i128>(1) << 47)) {
        t_q16 = static_cast<i64>((static_cast<u64>(num) << 16) / static_cast<u64>(A_128));
    } else {
        t_q16 = static_cast<i64>((num << 16) / A_128);
    }
    out_t_0_to_1 = fx32::from_raw(t_q16);
    return true;
}

Vec2fx closest_point_on_segment(Vec2fx p, Vec2fx a, Vec2fx b, fx32* out_t = nullptr) noexcept;

template <typename VisitorFn>
bool grid_raycast_supercover(Vec2fx start, Vec2fx end, u32 cell_size_meters_pow2,
                             i32 grid_w, i32 grid_h, VisitorFn&& visit) noexcept {
    const u32 shift = 16 + cell_size_meters_pow2;
    i32 x0 = start.x.raw >> shift;
    i32 z0 = start.z.raw >> shift;
    i32 x1 = end.x.raw >> shift;
    i32 z1 = end.z.raw >> shift;

    i32 dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    i32 dz = (z1 >= z0) ? (z1 - z0) : (z0 - z1);
    i32 step_x = (x1 >= x0) ? 1 : -1;
    i32 step_z = (z1 >= z0) ? 1 : -1;

    i32 cur_x = x0;
    i32 cur_z = z0;

    if (cur_x >= 0 && cur_x < grid_w && cur_z >= 0 && cur_z < grid_h) {
        if (!visit(cur_x, cur_z)) return false;
    }
    if (cur_x == x1 && cur_z == z1) return true;

    i32 err = dx - dz;
    while (cur_x != x1 || cur_z != z1) {
        i32 err2 = 2 * err;
        if (err2 > -dz && err2 < dx) {
            if (err >= 0) {
                if (cur_x + step_x >= 0 && cur_x + step_x < grid_w && cur_z >= 0 && cur_z < grid_h) {
                    if (!visit(cur_x + step_x, cur_z)) return false;
                }
            }
            if (err <= 0) {
                if (cur_x >= 0 && cur_x < grid_w && cur_z + step_z >= 0 && cur_z + step_z < grid_h) {
                    if (!visit(cur_x, cur_z + step_z)) return false;
                }
            }
            err -= dz;
            cur_x += step_x;
            err += dx;
            cur_z += step_z;
        } else if (err2 > -dz) {
            err -= dz;
            cur_x += step_x;
        } else if (err2 < dx) {
            err += dx;
            cur_z += step_z;
        }

        if (cur_x >= 0 && cur_x < grid_w && cur_z >= 0 && cur_z < grid_h) {
            if (!visit(cur_x, cur_z)) return false;
        }
    }
    return true;
}

Result<Vec2fx> solve_linear_intercept_2d(Vec2fx shooter_pos, fx32 projectile_speed,
                                        Vec2fx target_pos, Vec2fx target_vel) noexcept;

} // namespace ron
