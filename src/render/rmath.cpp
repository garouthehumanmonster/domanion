#include "render/rmath.hpp"
#include <cmath>
#include <algorithm>

namespace ron::render {

Mat4 mat4_identity() {
    Mat4 res{};
    res.m[0] = 1.0f;
    res.m[5] = 1.0f;
    res.m[10] = 1.0f;
    res.m[15] = 1.0f;
    return res;
}

Mat4 mat4_mul(const Mat4& a, const Mat4& b) {
    Mat4 res{};
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + r] * b.m[c * 4 + k];
            }
            res.m[c * 4 + r] = sum;
        }
    }
    return res;
}

Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = (target - eye).normalize();
    Vec3 s = f.cross(up).normalize();
    Vec3 u = s.cross(f);

    Mat4 res{};
    // Column 0
    res.m[0] = s.x;
    res.m[1] = u.x;
    res.m[2] = -f.x;
    res.m[3] = 0.0f;
    // Column 1
    res.m[4] = s.y;
    res.m[5] = u.y;
    res.m[6] = -f.y;
    res.m[7] = 0.0f;
    // Column 2
    res.m[8] = s.z;
    res.m[9] = u.z;
    res.m[10] = -f.z;
    res.m[11] = 0.0f;
    // Column 3
    res.m[12] = -s.dot(eye);
    res.m[13] = -u.dot(eye);
    res.m[14] = f.dot(eye);
    res.m[15] = 1.0f;

    return res;
}

Mat4 mat4_perspective_reverse_z(float fovy_rad, float aspect, float n, float f_far) {
    float f = 1.0f / std::tan(fovy_rad * 0.5f);
    float A = n / (f_far - n);
    float B = (n * f_far) / (f_far - n);

    Mat4 res{};
    res.m[0] = f / aspect;
    res.m[5] = f;
    res.m[10] = A;
    res.m[11] = -1.0f;
    res.m[14] = B;
    return res;
}

bool mat4_inverse(const Mat4& in, Mat4& out) {
    double m[16];
    for (int i = 0; i < 16; ++i) {
        m[i] = static_cast<double>(in.m[i]);
    }

    double inv[16];

    inv[0] = m[5]  * m[10] * m[15] - 
             m[5]  * m[11] * m[14] - 
             m[9]  * m[6]  * m[15] + 
             m[9]  * m[7]  * m[14] +
             m[13] * m[6]  * m[11] - 
             m[13] * m[7]  * m[10];

    inv[4] = -m[4]  * m[10] * m[15] + 
              m[4]  * m[11] * m[14] + 
              m[8]  * m[6]  * m[15] - 
              m[8]  * m[7]  * m[14] - 
              m[12] * m[6]  * m[11] + 
              m[12] * m[7]  * m[10];

    inv[8] = m[4]  * m[9] * m[15] - 
             m[4]  * m[11] * m[13] - 
             m[8]  * m[5] * m[15] + 
             m[8]  * m[7] * m[13] + 
             m[12] * m[5] * m[11] - 
             m[12] * m[7] * m[9];

    inv[12] = -m[4]  * m[9] * m[14] + 
               m[4]  * m[10] * m[13] +
               m[8]  * m[5] * m[14] - 
               m[8]  * m[6] * m[13] - 
               m[12] * m[5] * m[10] + 
               m[12] * m[6] * m[9];

    inv[1] = -m[1]  * m[10] * m[15] + 
              m[1]  * m[11] * m[14] + 
              m[9]  * m[2] * m[15] - 
              m[9]  * m[3] * m[14] - 
              m[13] * m[2] * m[11] + 
              m[13] * m[3] * m[10];

    inv[5] = m[0]  * m[10] * m[15] - 
             m[0]  * m[11] * m[14] - 
             m[8]  * m[2] * m[15] + 
             m[8]  * m[3] * m[14] + 
             m[12] * m[2] * m[11] - 
             m[12] * m[3] * m[10];

    inv[9] = -m[0]  * m[9] * m[15] + 
              m[0]  * m[11] * m[13] + 
              m[8]  * m[1] * m[15] - 
              m[8]  * m[3] * m[13] - 
              m[12] * m[1] * m[11] + 
              m[12] * m[3] * m[9];

    inv[13] = m[0]  * m[9] * m[14] - 
              m[0]  * m[10] * m[13] - 
              m[8]  * m[1] * m[14] + 
              m[8]  * m[2] * m[13] + 
              m[12] * m[1] * m[10] - 
              m[12] * m[2] * m[9];

    inv[2] = m[1]  * m[6] * m[15] - 
             m[1]  * m[7] * m[14] - 
             m[5]  * m[2] * m[15] + 
             m[5]  * m[3] * m[14] + 
             m[13] * m[2] * m[7] - 
             m[13] * m[3] * m[6];

    inv[6] = -m[0]  * m[6] * m[15] + 
              m[0]  * m[7] * m[14] + 
              m[4]  * m[2] * m[15] - 
              m[4]  * m[3] * m[14] - 
              m[12] * m[2] * m[7] + 
              m[12] * m[3] * m[6];

    inv[10] = m[0]  * m[5] * m[15] - 
              m[0]  * m[7] * m[13] - 
              m[4]  * m[1] * m[15] + 
              m[4]  * m[3] * m[13] + 
              m[12] * m[1] * m[7] - 
              m[12] * m[3] * m[5];

    inv[14] = -m[0]  * m[5] * m[14] + 
               m[0]  * m[6] * m[13] + 
               m[4]  * m[1] * m[14] - 
               m[4]  * m[2] * m[13] - 
               m[12] * m[1] * m[6] + 
               m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] + 
              m[1] * m[7] * m[10] + 
              m[5] * m[2] * m[11] - 
              m[5] * m[3] * m[10] - 
              m[9] * m[2] * m[7] + 
              m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] - 
             m[0] * m[7] * m[10] - 
             m[4] * m[2] * m[11] + 
             m[4] * m[3] * m[10] + 
             m[8] * m[2] * m[7] - 
             m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] + 
               m[0] * m[7] * m[9] + 
               m[4] * m[1] * m[11] - 
               m[4] * m[3] * m[9] - 
               m[8] * m[1] * m[7] + 
               m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] - 
              m[0] * m[6] * m[9] - 
              m[4] * m[1] * m[10] + 
              m[4] * m[2] * m[9] + 
              m[8] * m[1] * m[6] - 
              m[8] * m[2] * m[5];

    double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

    if (std::abs(det) < 1e-30) {
        return false;
    }

    double inv_det = 1.0 / det;
    for (int i = 0; i < 16; ++i) {
        out.m[i] = static_cast<float>(inv[i] * inv_det);
    }
    return true;
}

Vec3 transform_point(const Mat4& m, Vec3 p) {
    return Vec3(
        m.m[0] * p.x + m.m[4] * p.y + m.m[8]  * p.z + m.m[12],
        m.m[1] * p.x + m.m[5] * p.y + m.m[9]  * p.z + m.m[13],
        m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]
    );
}

void project(const Mat4& m, Vec3 p, float out_ndc[3]) {
    float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8]  * p.z + m.m[12];
    float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9]  * p.z + m.m[13];
    float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    float w = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15];

    float inv_w = (std::abs(w) > 1e-15f) ? (1.0f / w) : 1.0f;
    out_ndc[0] = x * inv_w;
    out_ndc[1] = y * inv_w;
    out_ndc[2] = z * inv_w;
}

Frustum frustum_from_view_proj(const Mat4& M) {
    Frustum f{};
    auto row = [&](int r, int c) -> float { return M.m[c * 4 + r]; };

    auto make_plane = [](float a, float b, float c, float d) -> Plane {
        float len = std::sqrt(a * a + b * b + c * c);
        float inv = (len > 1e-15f) ? (1.0f / len) : 0.0f;
        return Plane{ a * inv, b * inv, c * inv, d * inv };
    };

    // left = row3 + row0
    f.p[0] = make_plane(row(3,0) + row(0,0), row(3,1) + row(0,1), row(3,2) + row(0,2), row(3,3) + row(0,3));
    // right = row3 - row0
    f.p[1] = make_plane(row(3,0) - row(0,0), row(3,1) - row(0,1), row(3,2) - row(0,2), row(3,3) - row(0,3));
    // bottom = row3 + row1
    f.p[2] = make_plane(row(3,0) + row(1,0), row(3,1) + row(1,1), row(3,2) + row(1,2), row(3,3) + row(1,3));
    // top = row3 - row1
    f.p[3] = make_plane(row(3,0) - row(1,0), row(3,1) - row(1,1), row(3,2) - row(1,2), row(3,3) - row(1,3));
    // near = row3 - row2 (reverse-Z: 1 is near, ZERO_TO_ONE clip control)
    f.p[4] = make_plane(row(3,0) - row(2,0), row(3,1) - row(2,1), row(3,2) - row(2,2), row(3,3) - row(2,3));
    // far = row2 (reverse-Z: 0 is far)
    f.p[5] = make_plane(row(2,0), row(2,1), row(2,2), row(2,3));

    return f;
}

bool frustum_hits_aabb(const Frustum& f, Vec3 mn, Vec3 mx) {
    for (int i = 0; i < 6; ++i) {
        const Plane& pl = f.p[i];
        float px = (pl.a > 0.0f) ? mx.x : mn.x;
        float py = (pl.b > 0.0f) ? mx.y : mn.y;
        float pz = (pl.c > 0.0f) ? mx.z : mn.z;
        if (pl.a * px + pl.b * py + pl.c * pz + pl.d < 0.0f) {
            return false;
        }
    }
    return true;
}

void unproject_ray(const Mat4& inv_view_proj, float ndc_x, float ndc_y, Vec3& origin, Vec3& dir) {
    auto mult_pt = [&](double x, double y, double z) -> Vec3 {
        double vx = static_cast<double>(inv_view_proj.m[0]) * x +
                    static_cast<double>(inv_view_proj.m[4]) * y +
                    static_cast<double>(inv_view_proj.m[8]) * z +
                    static_cast<double>(inv_view_proj.m[12]);
        double vy = static_cast<double>(inv_view_proj.m[1]) * x +
                    static_cast<double>(inv_view_proj.m[5]) * y +
                    static_cast<double>(inv_view_proj.m[9]) * z +
                    static_cast<double>(inv_view_proj.m[13]);
        double vz = static_cast<double>(inv_view_proj.m[2]) * x +
                    static_cast<double>(inv_view_proj.m[6]) * y +
                    static_cast<double>(inv_view_proj.m[10]) * z +
                    static_cast<double>(inv_view_proj.m[14]);
        double vw = static_cast<double>(inv_view_proj.m[3]) * x +
                    static_cast<double>(inv_view_proj.m[7]) * y +
                    static_cast<double>(inv_view_proj.m[11]) * z +
                    static_cast<double>(inv_view_proj.m[15]);
        double inv_w = (std::abs(vw) > 1e-20) ? (1.0 / vw) : 1.0;
        return Vec3(static_cast<float>(vx * inv_w), static_cast<float>(vy * inv_w), static_cast<float>(vz * inv_w));
    };

    // Near point = ndc z 1.0, far point = ndc z 0.0
    Vec3 near_pt = mult_pt(static_cast<double>(ndc_x), static_cast<double>(ndc_y), 1.0);
    Vec3 far_pt  = mult_pt(static_cast<double>(ndc_x), static_cast<double>(ndc_y), 0.0);

    origin = near_pt;
    dir = (far_pt - near_pt).normalize();
}

} // namespace ron::render
