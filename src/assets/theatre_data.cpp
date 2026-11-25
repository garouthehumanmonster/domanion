#include "assets/theatre_data.hpp"
#include <cstdio>
#include <cstring>

namespace ron::assets {

Result<void> TheatreData::init_from_blob(std::span<const u8> blob) noexcept {
    is_valid_ = false;

    if (blob.size() < sizeof(TheatreHeader) + sizeof(SectionEntry) * 8) {
        return Result<void>::err(ErrorCode::InvalidArgument);
    }

    std::memcpy(&header_, blob.data(), sizeof(TheatreHeader));

    if (header_.magic != 0x52544854) { // 'THTR'
        return Result<void>::err(ErrorCode::InvalidArgument);
    }
    if (header_.version != 1) {
        return Result<void>::err(ErrorCode::InvalidArgument);
    }
    if (header_.section_count != 8) {
        return Result<void>::err(ErrorCode::InvalidArgument);
    }
    if (header_.total_bytes != blob.size()) {
        return Result<void>::err(ErrorCode::InvalidArgument);
    }

    // Verify content hash of post-header data
    u64 computed_hash = ron::xxh64(blob.subspan(sizeof(TheatreHeader)), 0);
    if (computed_hash != header_.content_hash) {
        return Result<void>::err(ErrorCode::CorruptData);
    }

    // Parse Section Table (8 entries starting at offset 64)
    const SectionEntry* sec_table = reinterpret_cast<const SectionEntry*>(blob.data() + sizeof(TheatreHeader));

    for (u32 i = 0; i < 8; ++i) {
        const SectionEntry& entry = sec_table[i];
        if ((u64)entry.sec_offset + entry.size_bytes > blob.size()) {
            return Result<void>::err(ErrorCode::CorruptData);
        }

        const u8* sec_ptr = blob.data() + entry.sec_offset;

        switch (entry.sec_id) {
            case 1: // Heightmap uint16[N * N]
                if (entry.count != (u32)header_.height_n * header_.height_n ||
                    entry.size_bytes != entry.count * sizeof(u16)) {
                    return Result<void>::err(ErrorCode::CorruptData);
                }
                heights_ = std::span<const u16>(reinterpret_cast<const u16*>(sec_ptr), entry.count);
                break;

            case 2: // Surface uint8[512 * 512]
                if (entry.count != (u32)header_.logic_n * header_.logic_n ||
                    entry.size_bytes != entry.count) {
                    return Result<void>::err(ErrorCode::CorruptData);
                }
                surface_ = std::span<const u8>(sec_ptr, entry.count);
                break;

            case 3: // Slope uint8[512 * 512]
                if (entry.count != (u32)header_.logic_n * header_.logic_n ||
                    entry.size_bytes != entry.count) {
                    return Result<void>::err(ErrorCode::CorruptData);
                }
                slope_ = std::span<const u8>(sec_ptr, entry.count);
                break;

            case 4: // Biome uint8[512 * 512]
                if (entry.count != (u32)header_.logic_n * header_.logic_n ||
                    entry.size_bytes != entry.count) {
                    return Result<void>::err(ErrorCode::CorruptData);
                }
                biome_ = std::span<const u8>(sec_ptr, entry.count);
                break;

            case 5: // DistWater uint8[512 * 512]
                if (entry.count != (u32)header_.logic_n * header_.logic_n ||
                    entry.size_bytes != entry.count) {
                    return Result<void>::err(ErrorCode::CorruptData);
                }
                distwater_ = std::span<const u8>(sec_ptr, entry.count);
                break;

            case 6: // Starts TheatreStart[start_count]
                if (entry.count != header_.start_count ||
                    entry.size_bytes != entry.count * sizeof(TheatreStart)) {
                    return Result<void>::err(ErrorCode::CorruptData);
                }
                starts_ = std::span<const TheatreStart>(reinterpret_cast<const TheatreStart*>(sec_ptr), entry.count);
                break;

            case 7: // Resources TheatreResource[count]
                if (entry.size_bytes != entry.count * sizeof(TheatreResource)) {
                    return Result<void>::err(ErrorCode::CorruptData);
                }
                resources_ = std::span<const TheatreResource>(reinterpret_cast<const TheatreResource*>(sec_ptr), entry.count);
                break;

            case 8: // Landings TheatreLanding[count]
                if (entry.size_bytes != entry.count * sizeof(TheatreLanding)) {
                    return Result<void>::err(ErrorCode::CorruptData);
                }
                landings_ = std::span<const TheatreLanding>(reinterpret_cast<const TheatreLanding*>(sec_ptr), entry.count);
                break;

            default:
                return Result<void>::err(ErrorCode::CorruptData);
        }
    }

    is_valid_ = true;
    return Result<void>::ok();
}

Result<void> TheatreData::load_file(const char* filepath, MemoryArena& arena) noexcept {
    FILE* f = std::fopen(filepath, "rb");
    if (!f) {
        return Result<void>::err(ErrorCode::IoError);
    }

    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        std::fclose(f);
        return Result<void>::err(ErrorCode::InvalidArgument);
    }

    u8* buf = arena.alloc<u8>((usize)size);
    if (!buf) {
        std::fclose(f);
        return Result<void>::err(ErrorCode::OutOfMemory);
    }

    usize read_bytes = std::fread(buf, 1, (usize)size, f);
    std::fclose(f);

    if (read_bytes != (usize)size) {
        return Result<void>::err(ErrorCode::IoError);
    }

    return init_from_blob(std::span<const u8>(buf, (usize)size));
}

Surface TheatreData::surface_at_logic(u32 x_cell, u32 z_cell) const noexcept {
    if (!is_valid_ || surface_.empty()) return Surface::DeepWater;
    u32 x = (x_cell < header_.logic_n) ? x_cell : (header_.logic_n - 1);
    u32 z = (z_cell < header_.logic_n) ? z_cell : (header_.logic_n - 1);
    return static_cast<Surface>(surface_[z * header_.logic_n + x]);
}

Biome TheatreData::biome_at_logic(u32 x_cell, u32 z_cell) const noexcept {
    if (!is_valid_ || biome_.empty()) return Biome::Ocean;
    u32 x = (x_cell < header_.logic_n) ? x_cell : (header_.logic_n - 1);
    u32 z = (z_cell < header_.logic_n) ? z_cell : (header_.logic_n - 1);
    return static_cast<Biome>(biome_[z * header_.logic_n + x]);
}

u8 TheatreData::slope_deg_at_logic(u32 x_cell, u32 z_cell) const noexcept {
    if (!is_valid_ || slope_.empty()) return 0;
    u32 x = (x_cell < header_.logic_n) ? x_cell : (header_.logic_n - 1);
    u32 z = (z_cell < header_.logic_n) ? z_cell : (header_.logic_n - 1);
    return slope_[z * header_.logic_n + x];
}

u8 TheatreData::distwater_at_logic(u32 x_cell, u32 z_cell) const noexcept {
    if (!is_valid_ || distwater_.empty()) return 0;
    u32 x = (x_cell < header_.logic_n) ? x_cell : (header_.logic_n - 1);
    u32 z = (z_cell < header_.logic_n) ? z_cell : (header_.logic_n - 1);
    return distwater_[z * header_.logic_n + x];
}

Surface TheatreData::surface_at_world(fx32 x, fx32 z) const noexcept {
    i32 rx = x.raw;
    i32 rz = z.raw;
    if (rx < 0) rx = 0;
    if (rz < 0) rz = 0;
    u32 cell_x = (u32)(rx >> 19);
    u32 cell_z = (u32)(rz >> 19);
    return surface_at_logic(cell_x, cell_z);
}

Biome TheatreData::biome_at_world(fx32 x, fx32 z) const noexcept {
    i32 rx = x.raw;
    i32 rz = z.raw;
    if (rx < 0) rx = 0;
    if (rz < 0) rz = 0;
    u32 cell_x = (u32)(rx >> 19);
    u32 cell_z = (u32)(rz >> 19);
    return biome_at_logic(cell_x, cell_z);
}

fx32 TheatreData::sample_height_fx32(fx32 x, fx32 z) const noexcept {
    if (!is_valid_ || heights_.empty()) return fx32::zero();

    const u32 N = header_.height_n;
    if (N == 0) return fx32::zero();

    i32 rx = x.raw;
    i32 rz = z.raw;
    if (rx < 0) rx = 0;
    if (rz < 0) rz = 0;
    const i32 max_world_raw = (4096 << 16) - 1;
    if (rx > max_world_raw) rx = max_world_raw;
    if (rz > max_world_raw) rz = max_world_raw;

    // Grid coordinates in Q16.16: gx = (rx * N) / 4096
    i64 gx_q16 = ((i64)rx * (i64)N) / 4096LL;
    i64 gz_q16 = ((i64)rz * (i64)N) / 4096LL;

    i32 c0 = (i32)(gx_q16 >> 16);
    i32 r0 = (i32)(gz_q16 >> 16);

    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c0 >= (i32)N) c0 = (i32)N - 1;
    if (r0 >= (i32)N) r0 = (i32)N - 1;

    i32 c1 = (c0 + 1 < (i32)N) ? c0 + 1 : c0;
    i32 r1 = (r0 + 1 < (i32)N) ? r0 + 1 : r0;

    i64 fx = gx_q16 & 0xFFFF;
    i64 fz = gz_q16 & 0xFFFF;

    u16 h00 = heights_[r0 * N + c0];
    u16 h10 = heights_[r0 * N + c1];
    u16 h01 = heights_[r1 * N + c0];
    u16 h11 = heights_[r1 * N + c1];

    fx32 y00 = hcode_to_fx32(h00);
    fx32 y10 = hcode_to_fx32(h10);
    fx32 y01 = hcode_to_fx32(h01);
    fx32 y11 = hcode_to_fx32(h11);

    i64 dy0 = (i64)(y10.raw - y00.raw) * fx;
    fx32 y0 = fx32::from_raw(y00.raw + (i32)(dy0 >> 16));

    i64 dy1 = (i64)(y11.raw - y01.raw) * fx;
    fx32 y1 = fx32::from_raw(y01.raw + (i32)(dy1 >> 16));

    i64 dy = (i64)(y1.raw - y0.raw) * fz;
    fx32 y = fx32::from_raw(y0.raw + (i32)(dy >> 16));

    return y;
}

} // namespace ron::assets
