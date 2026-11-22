#include "compression.hpp"
#include "hash.hpp"
#include <lz4.h>

namespace ron {

Result<std::span<u8>> lz4_compress_to_arena(std::span<const u8> src, MemoryArena& dst_arena) noexcept {
    if (src.size() > 0x7E000000ULL) {
        return Result<std::span<u8>>::err(ErrorCode::InvalidArgument);
    }

    const int src_size = static_cast<int>(src.size());
    const int max_dst_size = LZ4_compressBound(src_size);
    if (max_dst_size <= 0) {
        return Result<std::span<u8>>::err(ErrorCode::BufferTooSmall);
    }

    const usize start_marker = dst_arena.save_marker();
    const usize alloc_size = sizeof(Lz4BlockHeader) + static_cast<usize>(max_dst_size);

    if (dst_arena.used_bytes() + alloc_size > dst_arena.reserved_bytes()) {
        return Result<std::span<u8>>::err(ErrorCode::OutOfMemory);
    }

    u8* raw_block = static_cast<u8*>(dst_arena.alloc_raw(alloc_size, alignof(Lz4BlockHeader)));
    Lz4BlockHeader* hdr = reinterpret_cast<Lz4BlockHeader*>(raw_block);
    char* dst_payload = reinterpret_cast<char*>(raw_block + sizeof(Lz4BlockHeader));

    const int comp_size = LZ4_compress_default(
        reinterpret_cast<const char*>(src.data()),
        dst_payload,
        src_size,
        max_dst_size
    );

    if (comp_size <= 0) {
        dst_arena.restore_marker(start_marker);
        return Result<std::span<u8>>::err(ErrorCode::CompressionFailed);
    }

    const usize exact_used = sizeof(Lz4BlockHeader) + static_cast<usize>(comp_size);
    dst_arena.restore_marker(start_marker + exact_used);

    const u64 hash64 = xxh64(src, 0);
    hdr->magic = 0x36324E52;
    hdr->uncompressed_size = static_cast<u32>(src.size());
    hdr->compressed_size = static_cast<u32>(comp_size);
    hdr->payload_xxh32 = static_cast<u32>(hash64);

    return Result<std::span<u8>>::ok(std::span<u8>{raw_block, exact_used});
}

Result<std::span<u8>> lz4_decompress_to_arena(std::span<const u8> framed_src, MemoryArena& dst_arena) noexcept {
    if (framed_src.size() < sizeof(Lz4BlockHeader)) {
        return Result<std::span<u8>>::err(ErrorCode::BufferTooSmall);
    }

    Lz4BlockHeader hdr;
    std::memcpy(&hdr, framed_src.data(), sizeof(Lz4BlockHeader));

    if (hdr.magic != 0x36324E52) {
        return Result<std::span<u8>>::err(ErrorCode::CorruptData);
    }

    if (framed_src.size() != sizeof(Lz4BlockHeader) + static_cast<usize>(hdr.compressed_size)) {
        return Result<std::span<u8>>::err(ErrorCode::CorruptData);
    }

    const usize start_marker = dst_arena.save_marker();
    if (dst_arena.used_bytes() + hdr.uncompressed_size > dst_arena.reserved_bytes()) {
        return Result<std::span<u8>>::err(ErrorCode::OutOfMemory);
    }

    u8* decomp_buf = static_cast<u8*>(dst_arena.alloc_raw(hdr.uncompressed_size, CACHE_LINE_BYTES));
    const char* comp_data = reinterpret_cast<const char*>(framed_src.data() + sizeof(Lz4BlockHeader));

    const int decomp_result = LZ4_decompress_safe(
        comp_data,
        reinterpret_cast<char*>(decomp_buf),
        static_cast<int>(hdr.compressed_size),
        static_cast<int>(hdr.uncompressed_size)
    );

    if (decomp_result < 0 || static_cast<u32>(decomp_result) != hdr.uncompressed_size) {
        dst_arena.restore_marker(start_marker);
        return Result<std::span<u8>>::err(ErrorCode::DecompressionFailed);
    }

    std::span<const u8> decomp_span{decomp_buf, hdr.uncompressed_size};
    const u64 hash64 = xxh64(decomp_span, 0);
    if (static_cast<u32>(hash64) != hdr.payload_xxh32) {
        dst_arena.restore_marker(start_marker);
        return Result<std::span<u8>>::err(ErrorCode::CorruptData);
    }

    return Result<std::span<u8>>::ok(std::span<u8>{decomp_buf, hdr.uncompressed_size});
}

} // namespace ron
