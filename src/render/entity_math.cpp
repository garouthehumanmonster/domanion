#include "render/entity_math.hpp"
#include <cstring>
#include <algorithm>

namespace ron::render {

float half_to_float(u16 h) noexcept {
    u32 sign = (static_cast<u32>(h) & 0x8000u) << 16;
    u32 exp  = (static_cast<u32>(h) >> 10) & 0x1Fu;
    u32 mant = static_cast<u32>(h) & 0x03FFu;

    u32 f = 0;
    if (exp == 0) {
        if (mant == 0) {
            f = sign;
        } else {
            int e = 0;
            while ((mant & 0x0400u) == 0) {
                mant <<= 1;
                e--;
            }
            mant &= 0x03FFu;
            u32 exp32 = static_cast<u32>(127 - 14 + e);
            f = sign | (exp32 << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        f = sign | (0xFFu << 23) | (mant << 13);
    } else {
        u32 exp32 = exp + (127 - 15);
        f = sign | (exp32 << 23) | (mant << 13);
    }

    float res = 0.0f;
    std::memcpy(&res, &f, sizeof(float));
    return res;
}

void entity_model_matrix(const float pos[3], float yaw, float scale, float out_col_major[16]) noexcept {
    float s = std::sin(yaw);
    float c = std::cos(yaw);

    // Column 0: R_y * (1, 0, 0) * scale = (sin, 0, -cos) * scale
    out_col_major[0]  =  s * scale;
    out_col_major[1]  =  0.0f;
    out_col_major[2]  = -c * scale;
    out_col_major[3]  =  0.0f;

    // Column 1: R_y * (0, 1, 0) * scale = (0, 1, 0) * scale
    out_col_major[4]  =  0.0f;
    out_col_major[5]  =  scale;
    out_col_major[6]  =  0.0f;
    out_col_major[7]  =  0.0f;

    // Column 2: R_y * (0, 0, 1) * scale = (cos, 0, sin) * scale
    out_col_major[8]  =  c * scale;
    out_col_major[9]  =  0.0f;
    out_col_major[10] =  s * scale;
    out_col_major[11] =  0.0f;

    // Column 3: Translation
    out_col_major[12] =  pos[0];
    out_col_major[13] =  pos[1];
    out_col_major[14] =  pos[2];
    out_col_major[15] =  1.0f;
}

void entity_planes_from_viewproj(const float vp[16], float planes[6][4]) noexcept {
    // Column-major vp: m[col * 4 + row]
    // Row r = (vp[r], vp[4 + r], vp[8 + r], vp[12 + r])
    float row0[4] = { vp[0], vp[4], vp[8],  vp[12] };
    float row1[4] = { vp[1], vp[5], vp[9],  vp[13] };
    float row2[4] = { vp[2], vp[6], vp[10], vp[14] };
    float row3[4] = { vp[3], vp[7], vp[11], vp[15] };

    // Reverse-Z + ZERO_TO_ONE:
    // L = row3 + row0
    // R = row3 - row0
    // B = row3 + row1
    // T = row3 - row1
    // N = row2
    // F = row3 - row2
    for (int i = 0; i < 4; ++i) {
        planes[0][i] = row3[i] + row0[i]; // L
        planes[1][i] = row3[i] - row0[i]; // R
        planes[2][i] = row3[i] + row1[i]; // B
        planes[3][i] = row3[i] - row1[i]; // T
        planes[4][i] = row2[i];           // N
        planes[5][i] = row3[i] - row2[i]; // F
    }

    // Normalise by |xyz|
    for (int p = 0; p < 6; ++p) {
        float len_sq = planes[p][0] * planes[p][0] +
                       planes[p][1] * planes[p][1] +
                       planes[p][2] * planes[p][2];
        if (len_sq > 0.0f) {
            float inv_len = 1.0f / std::sqrt(len_sq);
            planes[p][0] *= inv_len;
            planes[p][1] *= inv_len;
            planes[p][2] *= inv_len;
            planes[p][3] *= inv_len;
        }
    }
}

void entity_fill_ubo(
    const FrameUniforms& frame,
    float alpha,
    u32 instance_count,
    u32 model_count,
    float time_s,
    EntityUbo* out_ubo) noexcept
{
    if (!out_ubo) return;

    entity_planes_from_viewproj(frame.view_proj, out_ubo->planes);

    out_ubo->cam_alpha[0] = frame.cam_pos[0];
    out_ubo->cam_alpha[1] = frame.cam_pos[1];
    out_ubo->cam_alpha[2] = frame.cam_pos[2];
    out_ubo->cam_alpha[3] = alpha;

    // proj_scale_px = 0.5 * screen_height_px * proj[5] (proj column-major, proj[5] = 1/tan(fovy/2))
    float proj_scale_px = 0.5f * frame.screen[1] * frame.proj[5];
    out_ubo->cull_p[0] = proj_scale_px;
    out_ubo->cull_p[1] = LOD0_PX;
    out_ubo->cull_p[2] = LOD1_PX;
    out_ubo->cull_p[3] = CULL_PX;

    out_ubo->counts[0] = instance_count;
    out_ubo->counts[1] = model_count;
    out_ubo->counts[2] = model_count * 3;
    out_ubo->counts[3] = 0;

    out_ubo->time_p[0] = time_s;
    out_ubo->time_p[1] = ANIM_FPS;
    out_ubo->time_p[2] = 0.0f;
    out_ubo->time_p[3] = 0.0f;
}

CullResult entity_cull_reference(
    const EntityInstance& inst,
    const ModelCpu* models,
    const EntityUbo& ubo) noexcept
{
    CullResult res{};
    res.bucket = 0xFFFFFFFFu;
    res.reason = 0;
    res.ambiguous = false;

    u32 model_id = entity_model_id(inst.ids);
    if (!models || model_id >= ubo.counts[1]) {
        res.reason = 1; // invalid model
        return res;
    }

    float alpha = ubo.cam_alpha[3];

    // Alpha-interpolate: pos = mix(prev, curr, alpha)
    float pos[3] = {
        inst.pos_prev[0] + (inst.pos_curr[0] - inst.pos_prev[0]) * alpha,
        inst.pos_prev[1] + (inst.pos_curr[1] - inst.pos_prev[1]) * alpha,
        inst.pos_prev[2] + (inst.pos_curr[2] - inst.pos_prev[2]) * alpha
    };

    // yaw = prev + wrap_pi(curr - prev) * alpha
    float yaw = lerp_yaw(inst.yaw_prev, inst.yaw_curr, alpha);

    const ModelCpu& model = models[model_id];

    // centre_world = pos + R_y(yaw) * (sphere.xyz * scale)
    float sphere_scaled[3] = {
        model.sphere[0] * inst.scale,
        model.sphere[1] * inst.scale,
        model.sphere[2] * inst.scale
    };
    float rot_sphere[3];
    entity_rotate_y(yaw, sphere_scaled, rot_sphere);

    float centre_world[3] = {
        pos[0] + rot_sphere[0],
        pos[1] + rot_sphere[1],
        pos[2] + rot_sphere[2]
    };
    float radius_w = model.sphere[3] * inst.scale + ubo.time_p[2];

    // Frustum cull: culled if any plane has dot(n, centre_world) + d < -radius_w
    for (int p = 0; p < 6; ++p) {
        float dot_p = ubo.planes[p][0] * centre_world[0] +
                      ubo.planes[p][1] * centre_world[1] +
                      ubo.planes[p][2] * centre_world[2];
        float val = dot_p + ubo.planes[p][3] + radius_w;
        if (std::abs(val) <= 0.05f) {
            res.ambiguous = true;
        }
        if (val < 0.0f) {
            res.reason = 2; // frustum culled
            return res;
        }
    }

    // Size cull: dist = length(centre_world - cam_pos); px = radius_w * proj_scale_px / max(dist, 0.001)
    float dx = centre_world[0] - ubo.cam_alpha[0];
    float dy = centre_world[1] - ubo.cam_alpha[1];
    float dz = centre_world[2] - ubo.cam_alpha[2];
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    float px = radius_w * ubo.cull_p[0] / std::max(dist, 0.001f);

    // Ambiguity on size thresholds
    const float thresholds[3] = { ubo.cull_p[1], ubo.cull_p[2], ubo.cull_p[3] }; // LOD0_PX, LOD1_PX, CULL_PX
    for (float T : thresholds) {
        if (std::abs(px - T) / T < 0.002f) {
            res.ambiguous = true;
        }
    }

    int lod = -1;
    if (px >= ubo.cull_p[1]) {
        lod = 0;
    } else if (px >= ubo.cull_p[2]) {
        lod = 1;
    } else if (px >= ubo.cull_p[3]) {
        lod = 2;
    } else {
        res.reason = 3; // size culled
        return res;
    }

    res.reason = 0; // visible
    res.bucket = model_id * 3 + static_cast<u32>(lod);
    return res;
}

void vat_part_matrix(
    const VatCpu& vat,
    int model,
    int clip,
    float anim_t,
    int part,
    float out3x4[12]) noexcept
{
    if (!vat.halfs || clip < 0 || clip >= 4 || part < 0 || part >= 16) {
        return;
    }

    const ClipDesc& c = ENTITY_CLIPS[clip];
    float f = anim_t * ANIM_FPS;
    float f0_fl;
    float fr;
    int f0 = 0;
    int f1 = 0;

    if (c.loop) {
        f = std::fmod(f, static_cast<float>(c.count));
        if (f < 0.0f) f += static_cast<float>(c.count);
        f0_fl = std::floor(f);
        fr = f - f0_fl;
        f0 = static_cast<int>(f0_fl);
        f1 = (f0 + 1) % c.count;
    } else {
        f = std::clamp(f, 0.0f, static_cast<float>(c.count - 1));
        f0_fl = std::floor(f);
        fr = f - f0_fl;
        f0 = static_cast<int>(f0_fl);
        f1 = std::min(f0 + 1, static_cast<int>(c.count - 1));
    }

    u32 row_base = static_cast<u32>(model) * VAT_FRAMES;
    u32 row0 = row_base + c.start + static_cast<u32>(f0);
    u32 row1 = row_base + c.start + static_cast<u32>(f1);

    if (row0 >= vat.rows || row1 >= vat.rows) {
        return;
    }

    for (int r = 0; r < 3; ++r) {
        int col = part * 3 + r;
        u32 idx0 = (row0 * VAT_COLS + static_cast<u32>(col)) * 4;
        u32 idx1 = (row1 * VAT_COLS + static_cast<u32>(col)) * 4;

        for (int k = 0; k < 4; ++k) {
            float v0 = half_to_float(vat.halfs[idx0 + k]);
            float v1 = half_to_float(vat.halfs[idx1 + k]);
            out3x4[r * 4 + k] = v0 + (v1 - v0) * fr;
        }
    }
}

} // namespace ron::render
