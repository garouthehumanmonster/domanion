#include "render/entity_crowd.hpp"
#include "render/entity_math.hpp"
#include <cmath>
#include <algorithm>

namespace ron::render {

void init_crowd(
    std::vector<CrowdEntity>& crowd,
    u32 count,
    const EntityAssets& assets,
    const HeightSampler& sampler,
    float focus_x,
    float focus_z)
{
    crowd.resize(count);
    if (count == 0) return;

    CrowdRng rng{2026};

    // Partition available models by category (0..7)
    std::vector<u16> models_by_cat[8];
    for (u16 m = 0; m < static_cast<u16>(assets.model_count()); ++m) {
        u8 cat = assets.source().model_cpu(m).category;
        if (cat < 8) {
            models_by_cat[cat].push_back(m);
        }
    }

    for (u32 i = 0; i < count; ++i) {
        CrowdEntity& ent = crowd[i];
        float roll = rng.next_float(0.0f, 1.0f);
        u8 cat = 0;
        float speed = 0.0f;
        u32 clip = 1; // Move

        if (roll < 0.50f) {
            // Infantry 50%
            cat = 0;
            speed = rng.next_float(3.0f, 5.0f);
            clip = 1;
        } else if (roll < 0.85f) {
            // Vehicle 35%
            cat = 1;
            speed = rng.next_float(6.0f, 10.0f);
            clip = 1;
        } else if (roll < 0.95f) {
            // Air 10%
            cat = 2;
            speed = rng.next_float(25.0f, 40.0f);
            clip = 1;
        } else {
            // Building 5%
            cat = 4;
            speed = 0.0f;
            clip = 0; // Idle
        }

        u16 model_id = 0;
        if (!models_by_cat[cat].empty()) {
            int idx = rng.next_int(0, static_cast<int>(models_by_cat[cat].size()) - 1);
            model_id = models_by_cat[cat][idx];
        } else {
            model_id = static_cast<u16>(rng.next_int(0, assets.model_count() - 1));
        }

        u32 owner = 1u + (i % 8u);
        u32 flags = ((i % 256u) == 0u) ? FLAG_HIT_FLASH : 0u;

        ent.inst.ids = entity_pack_ids(model_id, owner, flags);
        ent.inst.clip = clip;
        ent.speed = speed;
        ent.category = cat;

        // Clip duration: Move = 16 frames / 16 fps = 1.0s; Idle = 8 frames / 16 fps = 0.5s
        float clip_duration = (clip == 0) ? (8.0f / ANIM_FPS) : (16.0f / ANIM_FPS);
        ent.inst.anim_t = std::fmod(static_cast<float>(i) * 0.37f, clip_duration);

        ent.inst.scale = 1.0f;
        ent.inst.hp = 0.0f;
        ent.inst.tint = 0;
        ent.inst.r0 = 0;
        ent.inst.r1 = 0;

        // Spawn position: within 500m radius around focus on land (height >= 2.0m, rejection sampling max 50 tries)
        float spawn_x = focus_x;
        float spawn_z = focus_z;
        float ground_y = sampler.height_m(spawn_x, spawn_z);

        for (int t = 0; t < 50; ++t) {
            float r = std::sqrt(rng.next_float(0.0f, 1.0f)) * 500.0f;
            float theta = rng.next_float(0.0f, 6.2831853f);
            float cand_x = focus_x + r * std::cos(theta);
            float cand_z = focus_z + r * std::sin(theta);
            float h = sampler.height_m(cand_x, cand_z);
            if (h >= 2.0f) {
                spawn_x = cand_x;
                spawn_z = cand_z;
                ground_y = h;
                break;
            }
        }

        float yaw = rng.next_float(-3.14159265f, 3.14159265f);
        float spawn_y = (cat == 2) ? (ground_y + 60.0f) : ground_y;

        ent.inst.pos_prev[0] = spawn_x;
        ent.inst.pos_prev[1] = spawn_y;
        ent.inst.pos_prev[2] = spawn_z;
        ent.inst.yaw_prev = yaw;

        ent.inst.pos_curr[0] = spawn_x;
        ent.inst.pos_curr[1] = spawn_y;
        ent.inst.pos_curr[2] = spawn_z;
        ent.inst.yaw_curr = yaw;
    }
}

void update_crowd(
    std::vector<CrowdEntity>& crowd,
    const HeightSampler& sampler,
    float focus_x,
    float focus_z,
    float dt,
    float& sim_accumulator,
    float& out_alpha)
{
    constexpr float SIM_DT = 0.05f; // 20 Hz
    sim_accumulator += dt;

    if (sim_accumulator > 0.25f) {
        sim_accumulator = 0.25f;
    }

    while (sim_accumulator >= SIM_DT) {
        sim_accumulator -= SIM_DT;

        for (size_t i = 0; i < crowd.size(); ++i) {
            CrowdEntity& ent = crowd[i];
            ent.inst.pos_prev[0] = ent.inst.pos_curr[0];
            ent.inst.pos_prev[1] = ent.inst.pos_curr[1];
            ent.inst.pos_prev[2] = ent.inst.pos_curr[2];
            ent.inst.yaw_prev    = ent.inst.yaw_curr;

            if (ent.speed > 0.0f) {
                float sy = std::sin(ent.inst.yaw_curr);
                float cy = std::cos(ent.inst.yaw_curr);

                ent.inst.pos_curr[0] += sy * ent.speed * SIM_DT;
                ent.inst.pos_curr[2] -= cy * ent.speed * SIM_DT;

                float probe_x = ent.inst.pos_curr[0] + sy * 15.0f;
                float probe_z = ent.inst.pos_curr[2] - cy * 15.0f;

                float dx = ent.inst.pos_curr[0] - focus_x;
                float dz = ent.inst.pos_curr[2] - focus_z;
                float dist_sq = dx * dx + dz * dz;

                bool turn = false;
                if (ent.category != 2) { // ground units
                    if (sampler.height_m(probe_x, probe_z) < 1.0f) {
                        turn = true;
                    }
                }
                if (dist_sq > 700.0f * 700.0f) {
                    turn = true;
                }

                if (turn) {
                    ent.inst.yaw_curr += 3.14159265f;
                    ent.inst.yaw_curr = wrap_pi(ent.inst.yaw_curr);
                    ent.inst.yaw_prev = ent.inst.yaw_curr; // instant turn
                }

                float gh = sampler.height_m(ent.inst.pos_curr[0], ent.inst.pos_curr[2]);
                ent.inst.pos_curr[1] = (ent.category == 2) ? (gh + 60.0f) : gh;
                ent.inst.anim_t += SIM_DT;
            }
        }
    }

    out_alpha = std::clamp(sim_accumulator / SIM_DT, 0.0f, 1.0f);
}

} // namespace ron::render
