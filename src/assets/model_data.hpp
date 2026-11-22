#pragma once

#include "core/types.hpp"
#include "core/arena.hpp"
#include <cstddef>

namespace ron::assets {

enum class ModelError : u8 {
    Ok,
    TooSmall,
    BadMagic,
    BadVersion,
    BadSize,
    BadHeader,
    BadSectionTable,
    BadSectionSize,
    BadHash,
    BadModelTable
};

constexpr u32 kFramesPerModel = 44;
constexpr u32 kClipStart[4] = {0, 8, 24, 32};
constexpr u32 kClipFrames[4] = {8, 16, 8, 12};
constexpr u32 kVatCols = 48;

#pragma pack(push, 1)
struct ModelHeader {
    u32 magic;              // 0x36324D44 'MD26'
    u32 version;            // 1
    u32 total_bytes;
    u32 section_count;      // 5
    u64 content_xxh64;
    u32 model_count;
    u32 vertex_count;
    u32 index_count;
    u32 hardpoint_count;
    u32 vat_rows;           // 44 * model_count
    u32 vat_cols;           // 48
    u32 frames_per_model;   // 44
    u8  reserved[12];
};
static_assert(sizeof(ModelHeader) == 64);

struct SectionEntry {
    u32 id;
    u32 offset;
    u32 size_bytes;
    u32 count;
};
static_assert(sizeof(SectionEntry) == 16);

struct ModelRec {
    u64 id_hash;            // 0
    i32 bmin[3];            // 8
    i32 bmax[3];            // 20
    u32 vtx_base[3];        // 32
    u32 idx_first[3];       // 44
    u32 idx_count[3];       // 56
    u16 vtx_count[3];       // 68
    u16 part_count;         // 74
    u32 vat_row_base;       // 76
    u16 hp_first;           // 80
    u16 hp_count;           // 82
    u16 tri_count_lod0;     // 84
    u8  category;           // 86
    u8  flags;              // 87
    i32 radius_xz;          // 88
    u32 reserved;           // 92
};
static_assert(sizeof(ModelRec) == 96);
static_assert(offsetof(ModelRec, id_hash) == 0);
static_assert(offsetof(ModelRec, bmin) == 8);
static_assert(offsetof(ModelRec, vtx_base) == 32);
static_assert(offsetof(ModelRec, idx_first) == 44);
static_assert(offsetof(ModelRec, idx_count) == 56);
static_assert(offsetof(ModelRec, vtx_count) == 68);
static_assert(offsetof(ModelRec, vat_row_base) == 76);
static_assert(offsetof(ModelRec, category) == 86);
static_assert(offsetof(ModelRec, radius_xz) == 88);

struct VertexRec {
    float x;                // 0
    float y;                // 4
    float z;                // 8
    i16   nx;               // 12
    i16   ny;               // 14
    u16   u;                // 16
    u16   v;                // 18
    u8    trim;             // 20
    u8    flags;            // 21
    u16   part;             // 22
};
static_assert(sizeof(VertexRec) == 24);

struct HardpointRec {
    u8  kind;               // 0
    u8  part;               // 1
    u16 reserved;           // 2
    i32 x;                  // 4
    i32 y;                  // 8
    i32 z;                  // 12
};
static_assert(sizeof(HardpointRec) == 16);
#pragma pack(pop)

class ModelData {
public:
    ModelData() noexcept = default;

    ModelError init_from_blob(const u8* data, usize size) noexcept;
    ModelError load_file(const char* filepath, MemoryArena& arena) noexcept;

    bool is_valid() const noexcept { return is_valid_; }
    const ModelHeader& header() const noexcept { return header_; }

    u32 model_count() const noexcept { return is_valid_ ? header_.model_count : 0; }
    const ModelRec& model(u32 idx) const noexcept { return models_[idx]; }
    i32 find(u64 id_hash) const noexcept;

    const VertexRec* vertices() const noexcept { return vertices_; }
    u32 vertex_count() const noexcept { return is_valid_ ? header_.vertex_count : 0; }

    const u16* indices() const noexcept { return indices_; }
    u32 index_count() const noexcept { return is_valid_ ? header_.index_count : 0; }

    const HardpointRec* hardpoints() const noexcept { return hardpoints_; }
    u32 hardpoint_count() const noexcept { return is_valid_ ? header_.hardpoint_count : 0; }

    const u16* vat_halfs() const noexcept { return vat_halfs_; }
    u32 vat_rows() const noexcept { return is_valid_ ? header_.vat_rows : 0; }

    static u64 fnv1a64(const char* s) noexcept;
    static void decode_oct(i16 x, i16 y, float out[3]) noexcept;
    static float half_to_float(u16 h) noexcept;

    bool vat_matrix(u32 model_idx, u32 clip_idx, u32 frame_idx, u32 part_idx, float m[12]) const noexcept;

private:
    ModelHeader header_{};
    const ModelRec* models_{nullptr};
    const VertexRec* vertices_{nullptr};
    const u16* indices_{nullptr};
    const HardpointRec* hardpoints_{nullptr};
    const u16* vat_halfs_{nullptr};
    bool is_valid_{false};
};

} // namespace ron::assets
