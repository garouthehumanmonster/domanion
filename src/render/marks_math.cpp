#include "render/marks_math.hpp"
#include <cmath>
#include <algorithm>

namespace ron::render {

float icon_alpha(float px) noexcept {
    if (px <= 8.0f) return 1.0f;
    if (px >= 20.0f) return 0.0f;
    float t = (px - 8.0f) / 12.0f;
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

BarRect bar_layout(float centre_px, float centre_py, float radius_px, float ui) noexcept {
    float w = std::clamp(2.0f * radius_px, 14.0f, 64.0f) * ui;
    float h = 4.0f * ui;
    float bar_cy = centre_py - (radius_px + 8.0f * ui);
    float x0 = centre_px - w * 0.5f;
    float y0 = bar_cy - h * 0.5f;
    return BarRect{x0, y0, w, h};
}

void bar_color(float hp, float out[3]) noexcept {
    float c_hp = std::clamp(hp, 0.0f, 1.0f);
    constexpr float RED[3]    = {0.90f, 0.15f, 0.12f};
    constexpr float YELLOW[3] = {0.95f, 0.85f, 0.15f};
    constexpr float GREEN[3]  = {0.20f, 0.85f, 0.25f};

    if (c_hp >= 0.5f) {
        float t = (c_hp - 0.5f) * 2.0f;
        for (int i = 0; i < 3; ++i) {
            out[i] = YELLOW[i] * (1.0f - t) + GREEN[i] * t;
        }
    } else {
        float t = c_hp * 2.0f;
        for (int i = 0; i < 3; ++i) {
            out[i] = RED[i] * (1.0f - t) + YELLOW[i] * t;
        }
    }
}

MarksCullResult marks_cull_reference(
    const EntityInstance& inst,
    const ModelCpu* models,
    const ModelMark* model_marks,
    const EntityUbo& ubo) noexcept
{
    u32 model_id = entity_model_id(inst.ids);
    if (model_id >= ubo.counts[1]) return {false, false, false};

    float alpha = ubo.cam_alpha[3];
    float pos[3];
    pos[0] = inst.pos_prev[0] + (inst.pos_curr[0] - inst.pos_prev[0]) * alpha;
    pos[1] = inst.pos_prev[1] + (inst.pos_curr[1] - inst.pos_prev[1]) * alpha;
    pos[2] = inst.pos_prev[2] + (inst.pos_curr[2] - inst.pos_prev[2]) * alpha;
    float yaw = lerp_yaw(inst.yaw_prev, inst.yaw_curr, alpha);

    float sphere_local[3] = {
        models[model_id].sphere[0] * inst.scale,
        models[model_id].sphere[1] * inst.scale,
        models[model_id].sphere[2] * inst.scale
    };
    float rot_sphere[3];
    entity_rotate_y(yaw, sphere_local, rot_sphere);

    float centre_world[3] = {
        pos[0] + rot_sphere[0],
        pos[1] + rot_sphere[1],
        pos[2] + rot_sphere[2]
    };
    float radius_w = models[model_id].sphere[3] * inst.scale;

    bool frustum_culled = false;
    bool ambiguous = false;
    for (int p = 0; p < 6; ++p) {
        float d = ubo.planes[p][0] * centre_world[0] +
                  ubo.planes[p][1] * centre_world[1] +
                  ubo.planes[p][2] * centre_world[2] +
                  ubo.planes[p][3];
        if (std::abs(d + radius_w) <= 0.05f) {
            ambiguous = true;
        }
        if (d < -radius_w) {
            frustum_culled = true;
            break;
        }
    }

    if (frustum_culled) {
        return {false, false, ambiguous};
    }

    float dx = centre_world[0] - ubo.cam_alpha[0];
    float dy = centre_world[1] - ubo.cam_alpha[1];
    float dz = centre_world[2] - ubo.cam_alpha[2];
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    float px = radius_w * ubo.cull_p[0] / std::max(dist, 0.001f);

    if (std::abs(px - 20.0f) / 20.0f <= 0.002f || std::abs(px - 3.0f) / 3.0f <= 0.002f) {
        ambiguous = true;
    }

    bool has_icon = (model_marks[model_id].icon != 0xFFFFu) && (px < 20.0f);
    bool has_bar = (px >= 3.0f) && (((inst.hp > 0.0f) && (inst.hp < 0.999f)) || ((entity_flags(inst.ids) & FLAG_SELECTED) != 0));

    return {has_icon, has_bar, ambiguous};
}

} // namespace ron::render
