#include "hash.hpp"

namespace ron {

static RON_FORCE_INLINE u64 read64_le(const u8* p) noexcept {
    u64 val;
    std::memcpy(&val, p, sizeof(val));
    return val;
}

static RON_FORCE_INLINE u32 read32_le(const u8* p) noexcept {
    u32 val;
    std::memcpy(&val, p, sizeof(val));
    return val;
}

static RON_FORCE_INLINE u64 xxh64_round(u64 acc, u64 input) noexcept {
    acc += input * PRIME64_2;
    acc = std::rotl(acc, 31);
    acc *= PRIME64_1;
    return acc;
}

static RON_FORCE_INLINE u64 xxh64_merge_round(u64 acc, u64 val) noexcept {
    val = xxh64_round(0, val);
    acc ^= val;
    acc = acc * PRIME64_1 + PRIME64_4;
    return acc;
}

u64 xxh64(std::span<const u8> data, u64 seed) noexcept {
    const u8* p = data.data();
    usize remaining = data.size();
    const usize total_len = remaining;
    u64 h64 = 0;

    if (total_len >= 32) {
        u64 v1 = seed + PRIME64_1 + PRIME64_2;
        u64 v2 = seed + PRIME64_2;
        u64 v3 = seed + 0;
        u64 v4 = seed - PRIME64_1;

        while (remaining >= 32) {
            v1 = xxh64_round(v1, read64_le(p)); p += 8;
            v2 = xxh64_round(v2, read64_le(p)); p += 8;
            v3 = xxh64_round(v3, read64_le(p)); p += 8;
            v4 = xxh64_round(v4, read64_le(p)); p += 8;
            remaining -= 32;
        }

        h64 = std::rotl(v1, 1) + std::rotl(v2, 7) + std::rotl(v3, 12) + std::rotl(v4, 18);
        h64 = xxh64_merge_round(h64, v1);
        h64 = xxh64_merge_round(h64, v2);
        h64 = xxh64_merge_round(h64, v3);
        h64 = xxh64_merge_round(h64, v4);
    } else {
        h64 = seed + PRIME64_5;
    }

    h64 += static_cast<u64>(total_len);

    while (remaining >= 8) {
        u64 k1 = xxh64_round(0, read64_le(p));
        h64 ^= k1;
        h64 = std::rotl(h64, 27) * PRIME64_1 + PRIME64_4;
        p += 8;
        remaining -= 8;
    }

    if (remaining >= 4) {
        u64 k1 = static_cast<u64>(read32_le(p)) * PRIME64_1;
        h64 ^= k1;
        h64 = std::rotl(h64, 23) * PRIME64_2 + PRIME64_3;
        p += 4;
        remaining -= 4;
    }

    while (remaining > 0) {
        u64 k1 = static_cast<u64>(*p) * PRIME64_5;
        h64 ^= k1;
        h64 = std::rotl(h64, 11) * PRIME64_1;
        p += 1;
        remaining -= 1;
    }

    h64 ^= h64 >> 33;
    h64 *= PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= PRIME64_3;
    h64 ^= h64 >> 32;

    return h64;
}

u64 xxh64(std::string_view sv, u64 seed) noexcept {
    return xxh64(std::span<const u8>{reinterpret_cast<const u8*>(sv.data()), sv.size()}, seed);
}

void Xxh64Streamer::reset(u64 new_seed) noexcept {
    seed = new_seed;
    v[0] = seed + PRIME64_1 + PRIME64_2;
    v[1] = seed + PRIME64_2;
    v[2] = seed + 0;
    v[3] = seed - PRIME64_1;
    total_len = 0;
    memsize = 0;
    std::memset(buffer, 0, sizeof(buffer));
}

void Xxh64Streamer::update(std::span<const u8> data) noexcept {
    const u8* p = data.data();
    usize bsize = data.size();
    total_len += static_cast<u64>(bsize);

    if (memsize + bsize < 32) {
        std::memcpy(buffer + memsize, p, bsize);
        memsize += static_cast<u32>(bsize);
        return;
    }

    if (memsize > 0) {
        const usize fill = 32 - memsize;
        std::memcpy(buffer + memsize, p, fill);
        v[0] = xxh64_round(v[0], read64_le(buffer));
        v[1] = xxh64_round(v[1], read64_le(buffer + 8));
        v[2] = xxh64_round(v[2], read64_le(buffer + 16));
        v[3] = xxh64_round(v[3], read64_le(buffer + 24));
        p += fill;
        bsize -= fill;
        memsize = 0;
    }

    while (bsize >= 32) {
        v[0] = xxh64_round(v[0], read64_le(p));
        v[1] = xxh64_round(v[1], read64_le(p + 8));
        v[2] = xxh64_round(v[2], read64_le(p + 16));
        v[3] = xxh64_round(v[3], read64_le(p + 24));
        p += 32;
        bsize -= 32;
    }

    if (bsize > 0) {
        std::memcpy(buffer, p, bsize);
        memsize = static_cast<u32>(bsize);
    }
}

u64 Xxh64Streamer::digest() const noexcept {
    u64 h64 = 0;
    if (total_len >= 32) {
        h64 = std::rotl(v[0], 1) + std::rotl(v[1], 7) + std::rotl(v[2], 12) + std::rotl(v[3], 18);
        h64 = xxh64_merge_round(h64, v[0]);
        h64 = xxh64_merge_round(h64, v[1]);
        h64 = xxh64_merge_round(h64, v[2]);
        h64 = xxh64_merge_round(h64, v[3]);
    } else {
        h64 = seed + PRIME64_5;
    }

    h64 += total_len;

    const u8* p = buffer;
    u32 remaining = memsize;

    while (remaining >= 8) {
        u64 k1 = xxh64_round(0, read64_le(p));
        h64 ^= k1;
        h64 = std::rotl(h64, 27) * PRIME64_1 + PRIME64_4;
        p += 8;
        remaining -= 8;
    }

    if (remaining >= 4) {
        u64 k1 = static_cast<u64>(read32_le(p)) * PRIME64_1;
        h64 ^= k1;
        h64 = std::rotl(h64, 23) * PRIME64_2 + PRIME64_3;
        p += 4;
        remaining -= 4;
    }

    while (remaining > 0) {
        u64 k1 = static_cast<u64>(*p) * PRIME64_5;
        h64 ^= k1;
        h64 = std::rotl(h64, 11) * PRIME64_1;
        p += 1;
        remaining -= 1;
    }

    h64 ^= h64 >> 33;
    h64 *= PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= PRIME64_3;
    h64 ^= h64 >> 32;

    return h64;
}

} // namespace ron
