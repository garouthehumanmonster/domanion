#pragma once

#include "core/types.hpp"
#include "render/height_sampler.hpp"

namespace ron::render {

float terrain_shadow_ref(const HeightSampler& hs, float x, float y, float z, const float L[3], int steps) noexcept;

} // namespace ron::render
