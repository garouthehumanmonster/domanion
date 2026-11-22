#include "render/cascade_math.hpp"
#include "render/rmath.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace ron::render {

void cascade_splits(float n, float f, float lambda, float out[5]) noexcept {
    for (int i = 0; i <= 4; ++i) {
        float fi = static_cast<float>(i) / 4.0f;
        float linear = n + (f - n) * fi;
        float log_val = n * std::pow(f / n, fi);
        out[i] = linear * (1.0f - lambda) + log_val * lambda;
    }
}

void cascade_fit(const FrameUniforms& fu, const float L[3], int res, CascadeSet* out) noexcept {
    cascade_splits(SHADOW_NEAR, SHADOW_DISTANCE, SPLIT_LAMBDA, out->split);

    // View basis from column-major fu.view
    Vec3 right(fu.view[0], fu.view[4], fu.view[8]);
    Vec3 up(fu.view[1], fu.view[5], fu.view[9]);
    Vec3 fwd(-fu.view[2], -fu.view[6], -fu.view[10]);

    Vec3 eye(fu.cam_pos[0], fu.cam_pos[1], fu.cam_pos[2]);

    float tx = (std::abs(fu.proj[0]) > 1e-6f) ? (1.0f / fu.proj[0]) : 1.0f;
    float ty = (std::abs(fu.proj[5]) > 1e-6f) ? (1.0f / fu.proj[5]) : 1.0f;

    // Light basis:
    // upref = (|L.y|<0.99) ? (0,1,0) : (0,0,-1); r = normalize(cross(upref,L)); u = cross(L,r)
    Vec3 sun_L(L[0], L[1], L[2]);
    sun_L = sun_L.normalize();

    Vec3 upref = (std::abs(sun_L.y) < 0.99f) ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(0.0f, 0.0f, -1.0f);
    Vec3 r = upref.cross(sun_L).normalize();
    Vec3 u = sun_L.cross(r);

    for (int c = 0; c < 4; ++c) {
        float z_near = out->split[c];
        float z_far  = out->split[c + 1];

        // Slice corners at depth z: eye + fwd*z +- right*tx*z +- up*ty*z.
        Vec3 corners[8];
        int idx = 0;
        float depths[2] = { z_near, z_far };
        for (float z : depths) {
            float hx = tx * z;
            float hy = ty * z;
            Vec3 p_center = eye + fwd * z;
            corners[idx++] = p_center - right * hx - up * hy;
            corners[idx++] = p_center + right * hx - up * hy;
            corners[idx++] = p_center - right * hx + up * hy;
            corners[idx++] = p_center + right * hx + up * hy;
        }

        // C = mean of the 8 corners
        Vec3 C(0.0f, 0.0f, 0.0f);
        for (int k = 0; k < 8; ++k) {
            C = C + corners[k];
        }
        C = C * (1.0f / 8.0f);

        // R = max |corner-C|, then R = ceil(R*16)/16
        float max_dist = 0.0f;
        for (int k = 0; k < 8; ++k) {
            float d = (corners[k] - C).length();
            if (d > max_dist) max_dist = d;
        }
        float R = std::ceil(max_dist * 16.0f) / 16.0f;
        out->radius[c] = R;

        // cx=dot(r,C), cy=dot(u,C), cz=dot(L,C)
        float cx = r.dot(C);
        float cy = u.dot(C);
        float cz = sun_L.dot(C);

        // T=2R/SHADOW_RES; cx=floor(cx/T+0.5)*T; cy=floor(cy/T+0.5)*T.
        float T = (2.0f * R) / static_cast<float>(res);
        cx = std::floor(cx / T + 0.5f) * T;
        cy = std::floor(cy / T + 0.5f) * T;

        out->centre_ls[c][0] = cx;
        out->centre_ls[c][1] = cy;
        out->centre_ls[c][2] = cz;

        out->texel_world[c] = T;

        // zn_v=cz+R+700, zf_v=cz-R-400, range=zn_v-zf_v (=2R+1100), inv_range=1/range.
        float zn_v = cz + R + CASCADE_Z_UP;
        float zf_v = cz - R - CASCADE_Z_DOWN;
        float range = zn_v - zf_v;
        float inv_range = (range > 1e-6f) ? (1.0f / range) : 0.0f;
        out->inv_range[c] = inv_range;

        // light_vp rows (column-major storage):
        // row0=(r/R, -cx/R), row1=(u/R, -cy/R), row2=(L/range, -zf_v/range), row3=(0,0,0,1)
        float* m = out->light_vp[c];
        m[0]  = r.x / R;
        m[1]  = u.x / R;
        m[2]  = sun_L.x * inv_range;
        m[3]  = 0.0f;

        m[4]  = r.y / R;
        m[5]  = u.y / R;
        m[6]  = sun_L.y * inv_range;
        m[7]  = 0.0f;

        m[8]  = r.z / R;
        m[9]  = u.z / R;
        m[10] = sun_L.z * inv_range;
        m[11] = 0.0f;

        m[12] = -cx / R;
        m[13] = -cy / R;
        m[14] = -zf_v * inv_range;
        m[15] = 1.0f;
    }
}

int cascade_select(const CascadeSet& cs, float view_depth) noexcept {
    for (int c = 0; c < 4; ++c) {
        if (view_depth < cs.split[c + 1]) {
            return c;
        }
    }
    return -1;
}

void lighting_fill_ubo(const CascadeSet& cs, const FrameUniforms& fu, u32 flags, LightingUbo* out) noexcept {
    std::memcpy(out->light_vp, cs.light_vp, sizeof(out->light_vp));

    out->splits[0] = cs.split[1];
    out->splits[1] = cs.split[2];
    out->splits[2] = cs.split[3];
    out->splits[3] = cs.split[4];

    out->texel[0] = cs.texel_world[0];
    out->texel[1] = cs.texel_world[1];
    out->texel[2] = cs.texel_world[2];
    out->texel[3] = cs.texel_world[3];

    out->inv_range[0] = cs.inv_range[0];
    out->inv_range[1] = cs.inv_range[1];
    out->inv_range[2] = cs.inv_range[2];
    out->inv_range[3] = cs.inv_range[3];

    Vec3 fwd(-fu.view[2], -fu.view[6], -fu.view[10]);
    fwd = fwd.normalize();

    out->cam_fwd[0] = fwd.x;
    out->cam_fwd[1] = fwd.y;
    out->cam_fwd[2] = fwd.z;
    out->cam_fwd[3] = static_cast<float>(flags);

    out->ambient_sky[0] = AMBIENT_SKY[0];
    out->ambient_sky[1] = AMBIENT_SKY[1];
    out->ambient_sky[2] = AMBIENT_SKY[2];
    out->ambient_sky[3] = AMBIENT_INTENSITY;

    out->ambient_ground[0] = AMBIENT_GROUND[0];
    out->ambient_ground[1] = AMBIENT_GROUND[1];
    out->ambient_ground[2] = AMBIENT_GROUND[2];
    out->ambient_ground[3] = 0.0f;

    out->shadow_p[0] = 1.0f;
    out->shadow_p[1] = static_cast<float>(TERRAIN_SHADOW_STEPS);
    out->shadow_p[2] = TERRAIN_SHADOW_REACH;
    out->shadow_p[3] = 0.0f;
}

} // namespace ron::render
