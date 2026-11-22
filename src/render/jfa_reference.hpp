#pragma once

#include "core/types.hpp"

namespace ron::render {

void jfa_seed_reference(const u8* owner, int owner_n, float* out_xy /*G*G*2*/);
void jfa_brute_d2(const float* seeds_xy, int g, float* out_d2 /*G*G*/);

} // namespace ron::render
