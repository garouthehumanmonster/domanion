#pragma once

#include "core/types.hpp"
#include "render/entity_types.hpp"
#include "render/entity_math.hpp"

namespace ron::render {

struct MarksCullResult {
    bool icon;
    bool bar;
    bool ambiguous;
};

float icon_alpha(float px) noexcept;

struct BarRect {
    float x0, y0, w, h;
};

BarRect bar_layout(float centre_px, float centre_py, float radius_px, float ui) noexcept;
void bar_color(float hp, float out[3]) noexcept;

struct ModelMark {
    u16 icon;  // 0..31 or 0xFFFF
    u8  shape; // 0=ground, 1=air, 2=naval, 3=building
};

MarksCullResult marks_cull_reference(
    const EntityInstance& inst,
    const ModelCpu* models,
    const ModelMark* model_marks,
    const EntityUbo& ubo) noexcept;

} // namespace ron::render
