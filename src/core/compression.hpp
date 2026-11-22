#pragma once

#include "types.hpp"
#include "result.hpp"
#include "arena.hpp"

namespace ron {

struct Lz4BlockHeader {
    u32 magic{0x36324E52}; // 0x36324E52 ('RN26')
    u32 uncompressed_size{0};
    u32 compressed_size{0};
    u32 payload_xxh32{0};
};

static_assert(sizeof(Lz4BlockHeader) == 16);
static_assert(std::has_unique_object_representations_v<Lz4BlockHeader>);

Result<std::span<u8>> lz4_compress_to_arena(std::span<const u8> src, MemoryArena& dst_arena) noexcept;
Result<std::span<u8>> lz4_decompress_to_arena(std::span<const u8> framed_src, MemoryArena& dst_arena) noexcept;

} // namespace ron
