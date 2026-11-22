#pragma once

#include "core/types.hpp"

namespace ron::render {

constexpr u8 ROCK_LAYER = 6;

constexpr const char* TERRAIN_LAYER_NAMES[8] = {
    "seabed",       // 0
    "beach",        // 1
    "grass",        // 2
    "forest_floor", // 3
    "arid",         // 4
    "tundra",       // 5
    "rock",         // 6
    "cliff"         // 7
};

// Biome id -> layer index:
// 0: Ocean  -> 0 (seabed)
// 1: Beach  -> 1 (beach)
// 2: Grass  -> 2 (grass)
// 3: Forest -> 3 (forest_floor)
// 4: Arid   -> 4 (arid)
// 5: Tundra -> 5 (tundra)
// 6: Rock   -> 6 (rock)
// 7..31: unused -> 0
constexpr u8 BIOME_TO_LAYER[32] = {
    0, 1, 2, 3, 4, 5, 6, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

} // namespace ron::render
