#pragma once

#include "core/types.hpp"
#include "core/arena.hpp"
#include "core/fixed_math.hpp"
#include "core/fixed_geom.hpp"
#include "core/result.hpp"
#include "core/hash.hpp"
#include <span>
#include <string_view>

namespace ron::sim {

enum class Surface : u8 {
    DeepWater = 0,
    ShallowWater = 1,
    Beach = 2,
    Land = 3,
    Rough = 4,
    Cliff = 5
};

enum class Biome : u8 {
    Ocean = 0,
    Beach = 1,
    Grass = 2,
    Forest = 3,
    Arid = 4,
    Tundra = 5,
    Rock = 6
};

enum class NodeKind : u8 {
    Berries = 0,
    Forest = 1,
    Iron = 2,
    Gold = 3,
    Relic = 4,
    Oil = 5,
    Silicon = 6,
    Herd = 7
};

#pragma pack(push, 1)
struct TheatreHeader {
    u32 magic;             // 0x52544854 'THTR'
    u32 version;           // 1
    u32 total_bytes;
    u32 section_count;     // 8
    u64 content_hash;
    u64 seed_eff;
    u16 height_n;          // 512, 1024, or 2048
    u16 logic_n;           // 512
    u16 world_m;           // 4096
    u8  player_slots;      // 8
    u8  start_count;       // 8
    u16 sea_level_code;    // 13107
    u16 reserved;
    u8  pad[20];
};
static_assert(sizeof(TheatreHeader) == 64);

struct SectionEntry {
    u32 sec_id;
    u32 sec_offset;
    u32 size_bytes;
    u32 count;
};
static_assert(sizeof(SectionEntry) == 16);

struct TheatreStart {
    u8  slot;
    u8  flags;
    u16 x_q;               // 1/16 m
    u16 z_q;               // 1/16 m
    u16 plateau_hcode;
    u16 flat_radius_q;     // 1/16 m
    u8  pad[6];
};
static_assert(sizeof(TheatreStart) == 16);

struct TheatreResource {
    u8  kind;
    u8  owner;             // 0..7 or 0xFF
    u16 amount;
    u16 x_q;               // 1/16 m
    u16 z_q;               // 1/16 m
    u16 footprint_q;       // 1/16 m
    u8  pad[6];
};
static_assert(sizeof(TheatreResource) == 16);

struct TheatreLanding {
    u16 x_q;               // 1/16 m
    u16 z_q;               // 1/16 m
    u16 heading_a16;       // Angle16 BAM
    u8  pad[2];
};
static_assert(sizeof(TheatreLanding) == 8);
#pragma pack(pop)

class TheatreData {
public:
    TheatreData() noexcept = default;

    Result<void> init_from_blob(std::span<const u8> blob) noexcept;
    Result<void> load_file(const char* filepath, MemoryArena& arena) noexcept;

    bool is_valid() const noexcept { return is_valid_; }
    const TheatreHeader& header() const noexcept { return header_; }

    u16 height_n() const noexcept { return header_.height_n; }
    u16 logic_n() const noexcept { return header_.logic_n; }
    u16 world_m() const noexcept { return header_.world_m; }
    u16 sea_level_code() const noexcept { return header_.sea_level_code; }

    std::span<const u16> height_grid() const noexcept { return heights_; }
    std::span<const u8> surface_grid() const noexcept { return surface_; }
    std::span<const u8> slope_grid() const noexcept { return slope_; }
    std::span<const u8> biome_grid() const noexcept { return biome_; }
    std::span<const u8> distwater_grid() const noexcept { return distwater_; }

    std::span<const TheatreStart> starts() const noexcept { return starts_; }
    std::span<const TheatreResource> resources() const noexcept { return resources_; }
    std::span<const TheatreLanding> landings() const noexcept { return landings_; }

    // Logic queries (512x512 logic cells, cell size 8m)
    Surface surface_at_logic(u32 x_cell, u32 z_cell) const noexcept;
    Biome biome_at_logic(u32 x_cell, u32 z_cell) const noexcept;
    u8 slope_deg_at_logic(u32 x_cell, u32 z_cell) const noexcept;
    u8 distwater_at_logic(u32 x_cell, u32 z_cell) const noexcept;

    // Spatial world queries (world coordinates in fx32)
    Surface surface_at_world(fx32 x, fx32 z) const noexcept;
    Biome biome_at_world(fx32 x, fx32 z) const noexcept;

    // Height sampling: exact Q16.16 integer bilinear interpolation
    fx32 sample_height_fx32(fx32 x, fx32 z) const noexcept;

    // Conversion helpers
    static constexpr fx32 hcode_to_fx32(u16 code) noexcept {
        i64 num = (i64)code * 640LL * 65536LL + 32767LL;
        i64 val = num / 65535LL;
        return fx32::from_raw((i32)val - (128 << 16));
    }

    static constexpr fx32 q_to_fx32(u16 val_q) noexcept {
        return fx32::from_raw((i32)val_q << 12);
    }

private:
    bool is_valid_{false};
    TheatreHeader header_{};
    std::span<const u16> heights_{};
    std::span<const u8> surface_{};
    std::span<const u8> slope_{};
    std::span<const u8> biome_{};
    std::span<const u8> distwater_{};
    std::span<const TheatreStart> starts_{};
    std::span<const TheatreResource> resources_{};
    std::span<const TheatreLanding> landings_{};
};

} // namespace ron::sim
