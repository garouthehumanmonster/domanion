#pragma once

#include "core/types.hpp"
#include "render/overlay_types.hpp"

namespace ron::render {

struct TexelBox {
    int x0;
    int y0;
    int x1;
    int y1;
};

float crater_extent_m(const CraterSplat& splat);
float crater_offset_m(const CraterSplat& splat, float px, float pz);
float crater_scorch(const CraterSplat& splat, float px, float pz);
TexelBox crater_box(const CraterSplat& splat, int n);
void crater_clear(u8* rgba, int n);
void crater_apply_cpu(u8* rgba, int n, const CraterSplat& splat);
CraterSplat crater_sanitize(CraterSplat splat);

} // namespace ron::render
