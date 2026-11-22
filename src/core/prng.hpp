#pragma once

#include "types.hpp"

namespace ron {

struct DeterministicRng {
    u32 s[4]{1, 0, 0, 0};

    constexpr explicit DeterministicRng(u64 seed = 1) noexcept {
        u64 state = (seed == 0) ? 0x9E3779B97F4A7C15ULL : seed;
        auto splitmix64 = [](u64& z) constexpr -> u64 {
            z += 0x9E3779B97F4A7C15ULL;
            u64 val = z;
            val = (val ^ (val >> 30)) * 0xBF58476D1CE4E5B9ULL;
            val = (val ^ (val >> 27)) * 0x94D049BB133111EBULL;
            return val ^ (val >> 31);
        };
        const u64 z0 = splitmix64(state);
        const u64 z1 = splitmix64(state);
        s[0] = static_cast<u32>(z0);
        s[1] = static_cast<u32>(z0 >> 32);
        s[2] = static_cast<u32>(z1);
        s[3] = static_cast<u32>(z1 >> 32);
        if (s[0] == 0 && s[1] == 0 && s[2] == 0 && s[3] == 0) {
            s[0] = 1;
        }
    }

    constexpr u32 next_u32() noexcept {
        const u32 result = std::rotl(s[0] + s[3], 7) + s[0];
        const u32 t = s[1] << 9;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = std::rotl(s[3], 11);
        return result;
    }

    constexpr u64 next_u64() noexcept {
        const u64 hi = static_cast<u64>(next_u32());
        const u64 lo = static_cast<u64>(next_u32());
        return (hi << 32) | lo;
    }

    constexpr u32 range_u32(u32 bound) noexcept {
        if (bound <= 1) return 0;
        u64 m = static_cast<u64>(next_u32()) * static_cast<u64>(bound);
        u32 l = static_cast<u32>(m);
        if (l < bound) {
            const u32 threshold = (~bound + 1u) % bound;
            while (l < threshold) {
                m = static_cast<u64>(next_u32()) * static_cast<u64>(bound);
                l = static_cast<u32>(m);
            }
        }
        return static_cast<u32>(m >> 32);
    }

    constexpr i32 range_i32(i32 min_inclusive, i32 max_inclusive) noexcept {
        if (min_inclusive >= max_inclusive) return min_inclusive;
        const u32 span = static_cast<u32>(static_cast<i64>(max_inclusive) - static_cast<i64>(min_inclusive) + 1);
        return min_inclusive + static_cast<i32>(range_u32(span));
    }

    constexpr bool chance_permille(u32 permille_0_to_1000) noexcept {
        return range_u32(1000u) < permille_0_to_1000;
    }

    constexpr DeterministicRng fork(u64 stream_salt) const noexcept {
        const u64 h0 = static_cast<u64>(s[0]) | (static_cast<u64>(s[1]) << 32);
        const u64 h1 = static_cast<u64>(s[2]) | (static_cast<u64>(s[3]) << 32);
        const u64 mixed = h0 ^ (h1 + 0x9E3779B97F4A7C15ULL + (h0 << 6) + (h0 >> 2)) ^ stream_salt;
        return DeterministicRng(mixed);
    }
};

static_assert(sizeof(DeterministicRng) == 16);
static_assert(std::is_trivially_copyable_v<DeterministicRng>);
static_assert(std::has_unique_object_representations_v<DeterministicRng>);

} // namespace ron
