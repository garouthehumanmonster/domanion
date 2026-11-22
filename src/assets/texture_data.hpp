#pragma once

#include "core/types.hpp"
#include "core/arena.hpp"
#include <cstddef>

namespace ron::assets {

enum class TextureError : u8 {
    Ok,
    TooSmall,
    BadMagic,
    BadVersion,
    BadSize,
    BadHeader,
    BadHash,
    BadTextureTable,
    BadGlyphTable,
    FileNotFound,
    ReadFailed
};

#pragma pack(push, 1)
struct TextureHeader {
    u32 magic;          // 0x36325854 'TX26'
    u32 version;        // 1
    u32 total_bytes;    // exact file size
    u32 texture_count;  // 9
    u64 content_hash;   // xxh64 over [64, total_bytes)
    u32 glyph_offset;   // 64-byte aligned offset to GlyphRec table
    u32 glyph_count;    // 95
    u8  reserved[32];
};
static_assert(sizeof(TextureHeader) == 64);

struct TextureRec {
    u64 name_hash;      // fnv1a64 of ASCII name
    u32 format;         // 0=BC1, 1=BC3, 2=BC4, 3=BC5, 4=RGBA8
    u16 width;
    u16 height;
    u16 layers;
    u16 mips;
    u32 data_offset;    // 64-byte aligned
    u32 data_bytes;
    u32 flags;          // bit 0=sRGB, bit 1=premul
    u8  probe_a[4];
    u8  probe_b[4];
    u8  reserved[24];
};
static_assert(sizeof(TextureRec) == 64);

struct GlyphRec {
    u16 cell_x;
    u16 cell_y;
    u16 cell_w;
    u16 cell_h;
    i16 offset_x_q;     // Q8.8 signed (divide by 256.0f)
    i16 offset_y_q;     // Q8.8 signed (divide by 256.0f)
    u16 advance_q;      // Q8.8 unsigned (divide by 256.0f)
    u16 reserved;
};
static_assert(sizeof(GlyphRec) == 16);
#pragma pack(pop)

class TextureData {
public:
    TextureData() noexcept = default;

    TextureError init_from_blob(const u8* data, usize size) noexcept;
    TextureError load_file(const char* filepath, MemoryArena& arena) noexcept;

    bool is_valid() const noexcept { return is_valid_; }
    const TextureHeader& header() const noexcept { return header_; }

    u32 texture_count() const noexcept { return is_valid_ ? header_.texture_count : 0; }
    const TextureRec& texture(u32 idx) const noexcept { return textures_[idx]; }
    i32 find(u64 name_hash) const noexcept;
    i32 find(const char* name) const noexcept;

    const u8* texture_data(u32 idx) const noexcept;
    usize level_layer_offset(u32 idx, u32 level, u32 layer) const noexcept;
    const u8* level_data(u32 idx, u32 level, u32 layer = 0) const noexcept;

    const GlyphRec* glyphs() const noexcept { return glyphs_; }
    u32 glyph_count() const noexcept { return is_valid_ ? header_.glyph_count : 0; }
    const GlyphRec* get_glyph(u8 ascii_char) const noexcept;

    // Static decoders & utilities
    static u64 fnv1a64(const char* s) noexcept;
    static usize level_size(u32 fmt, u32 w, u32 h) noexcept;

    static void decode_bc1_block(const u8 block[8], u8 out_rgba[64], bool force_4color = false) noexcept;
    static void decode_bc4_block(const u8 block[8], u8 out_r[16]) noexcept;
    static void decode_bc5_block(const u8 block[16], u8 out_rg[32]) noexcept;
    static void decode_bc3_block(const u8 block[16], u8 out_rgba[64]) noexcept;

    static void decode_texel(u32 fmt, const u8* data, u32 w, u32 h, u32 x, u32 y, u8 out[4]) noexcept;

private:
    TextureHeader header_{};
    const TextureRec* textures_{nullptr};
    const GlyphRec* glyphs_{nullptr};
    const u8* raw_data_{nullptr};
    bool is_valid_{false};
};

} // namespace ron::assets
