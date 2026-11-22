#pragma once

#include "core/types.hpp"
#include "render/height_sampler.hpp"

namespace ron::render {

struct TerritorySource {
    float x;
    float z;
    float radius;
    u8 owner;
};

void territory_rasterize(const TerritorySource* src, int n, const HeightSampler* land_or_null, u8* out /*512*512*/);

struct Site {
    float x;
    float z;
};

int pick_land_sites(const HeightSampler& sampler, int count, float min_sep_m, Site* out);

} // namespace ron::render
