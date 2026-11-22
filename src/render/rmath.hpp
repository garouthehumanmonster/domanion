#pragma once

#include "core/types.hpp"
#include <cmath>
#include <numbers>

namespace ron::render {

struct Vec3 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    constexpr Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    constexpr Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    constexpr Vec3 operator/(float s) const { float inv = 1.0f / s; return Vec3(x * inv, y * inv, z * inv); }

    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    constexpr float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    constexpr Vec3 cross(const Vec3& o) const {
        return Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x);
    }
    float length() const { return std::sqrt(dot(*this)); }
    Vec3 normalize() const {
        float l = length();
        return (l > 1e-15f) ? (*this / l) : Vec3(0.0f, 0.0f, 0.0f);
    }
};

inline constexpr Vec3 operator*(float s, const Vec3& v) { return v * s; }

struct Mat4 {
    float m[16]{0.0f}; // Column-major: m[c * 4 + r]
};

Mat4 mat4_identity();
Mat4 mat4_mul(const Mat4& a, const Mat4& b);
Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up);
Mat4 mat4_perspective_reverse_z(float fovy_rad, float aspect, float n, float f_far);
bool mat4_inverse(const Mat4& in, Mat4& out);
Vec3 transform_point(const Mat4& m, Vec3 p);
void project(const Mat4& m, Vec3 p, float out_ndc[3]);

struct Plane {
    float a{0.0f};
    float b{0.0f};
    float c{0.0f};
    float d{0.0f};
};

struct Frustum {
    Plane p[6]; // left, right, bottom, top, near, far
};

Frustum frustum_from_view_proj(const Mat4& view_proj);
bool frustum_hits_aabb(const Frustum& f, Vec3 mn, Vec3 mx);

void unproject_ray(const Mat4& inv_view_proj, float ndc_x, float ndc_y, Vec3& origin, Vec3& dir);

} // namespace ron::render
