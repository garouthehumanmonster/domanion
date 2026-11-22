#pragma once

#include "core/types.hpp"
#include "render/height_sampler.hpp"

namespace ron::render {

struct PxRect {
    float x;
    float y;
    float w;
    float h;
};

bool minimap_pick(float px, float py, const PxRect& rect, float* wx, float* wz);
void minimap_world_to_px(float wx, float wz, const PxRect& rect, float* px, float* py);
void minimap_bake(const HeightSampler& sampler, u8* rgba /*512*512*4*/);
void camera_ground_quad(const float inv_view_proj[16], const float cam_pos[3], const HeightSampler& sampler, float out_xz[8]);

} // namespace ron::render
