#pragma once

#include "types.hpp"

namespace ron {

inline constexpr u64 PRIME64_1 = 0x9E3779B185EBCA87ULL;
inline constexpr u64 PRIME64_2 = 0xC2B2AE3D27D4EB4FULL;
inline constexpr u64 PRIME64_3 = 0x165667B19E3779F9ULL;
inline constexpr u64 PRIME64_4 = 0x85EBCA77C2B2AE63ULL;
inline constexpr u64 PRIME64_5 = 0x27D4EB2F165667C5ULL;

u64 xxh64(std::span<const u8> data, u64 seed = 0) noexcept;
u64 xxh64(std::string_view sv, u64 seed = 0) noexcept;

struct Xxh64Streamer {
    u64 v[4]{0};
    u64 total_len{0};
    u64 seed{0};
    u32 memsize{0};
    u8 buffer[32]{0};

    void reset(u64 seed = 0) noexcept;
    void update(std::span<const u8> data) noexcept;
    u64 digest() const noexcept;
};

constexpr u64 fnv1a_64(std::string_view sv) noexcept {
    u64 hash = 0xCBF29CE484222325ULL;
    for (char c : sv) {
        hash ^= static_cast<u64>(static_cast<u8>(c));
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

struct StringId {
    u64 hash{0};
    constexpr auto operator<=>(const StringId&) const = default;
};

constexpr StringId operator""_sid(const char* str, usize len) noexcept {
    return StringId{fnv1a_64(std::string_view(str, len))};
}

} // namespace ron
