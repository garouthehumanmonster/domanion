#include "assets/texture_data.hpp"
#include "core/hash.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <span>

namespace ron::assets {

u64 TextureData::fnv1a64(const char* s) noexcept {
    if (!s) return 0xCBF29CE484222325ULL;
    u64 hash = 0xCBF29CE484222325ULL;
    while (*s != '\0') {
        hash ^= static_cast<u64>(static_cast<u8>(*s));
        hash *= 0x100000001B3ULL;
        ++s;
    }
    return hash;
}

usize TextureData::level_size(u32 fmt, u32 w, u32 h) noexcept {
    if (fmt == 4) { // RGBA8
        return static_cast<usize>(w) * h * 4;
    }
    u32 bx = (w + 3) / 4;
    u32 by = (h + 3) / 4;
    if (bx == 0) bx = 1;
    if (by == 0) by = 1;
    u32 bsize = (fmt == 0 || fmt == 2) ? 8 : 16;
    return static_cast<usize>(bx) * by * bsize;
}

void TextureData::decode_bc1_block(const u8 block[8], u8 out_rgba[64], bool force_4color) noexcept {
    u16 c0 = static_cast<u16>(block[0]) | (static_cast<u16>(block[1]) << 8);
    u16 c1 = static_cast<u16>(block[2]) | (static_cast<u16>(block[3]) << 8);
    u32 idx = static_cast<u32>(block[4]) |
              (static_cast<u32>(block[5]) << 8) |
              (static_cast<u32>(block[6]) << 16) |
              (static_cast<u32>(block[7]) << 24);

    u32 r5_0 = (c0 >> 11) & 0x1Fu;
    u32 g6_0 = (c0 >> 5) & 0x3Fu;
    u32 b5_0 = c0 & 0x1Fu;
    u8 p0[3] = {
        static_cast<u8>((r5_0 << 3) | (r5_0 >> 2)),
        static_cast<u8>((g6_0 << 2) | (g6_0 >> 4)),
        static_cast<u8>((b5_0 << 3) | (b5_0 >> 2))
    };

    u32 r5_1 = (c1 >> 11) & 0x1Fu;
    u32 g6_1 = (c1 >> 5) & 0x3Fu;
    u32 b5_1 = c1 & 0x1Fu;
    u8 p1[3] = {
        static_cast<u8>((r5_1 << 3) | (r5_1 >> 2)),
        static_cast<u8>((g6_1 << 2) | (g6_1 >> 4)),
        static_cast<u8>((b5_1 << 3) | (b5_1 >> 2))
    };

    u8 pal[4][4];
    pal[0][0] = p0[0]; pal[0][1] = p0[1]; pal[0][2] = p0[2]; pal[0][3] = 255;
    pal[1][0] = p1[0]; pal[1][1] = p1[1]; pal[1][2] = p1[2]; pal[1][3] = 255;

    if (c0 > c1 || force_4color) {
        pal[2][0] = static_cast<u8>((2 * p0[0] + p1[0] + 1) / 3);
        pal[2][1] = static_cast<u8>((2 * p0[1] + p1[1] + 1) / 3);
        pal[2][2] = static_cast<u8>((2 * p0[2] + p1[2] + 1) / 3);
        pal[2][3] = 255;

        pal[3][0] = static_cast<u8>((p0[0] + 2 * p1[0] + 1) / 3);
        pal[3][1] = static_cast<u8>((p0[1] + 2 * p1[1] + 1) / 3);
        pal[3][2] = static_cast<u8>((p0[2] + 2 * p1[2] + 1) / 3);
        pal[3][3] = 255;
    } else {
        pal[2][0] = static_cast<u8>((p0[0] + p1[0]) / 2);
        pal[2][1] = static_cast<u8>((p0[1] + p1[1]) / 2);
        pal[2][2] = static_cast<u8>((p0[2] + p1[2]) / 2);
        pal[2][3] = 255;

        pal[3][0] = 0; pal[3][1] = 0; pal[3][2] = 0; pal[3][3] = 0;
    }

    for (u32 p = 0; p < 16; ++p) {
        u32 pidx = (idx >> (2 * p)) & 0x3;
        out_rgba[p * 4 + 0] = pal[pidx][0];
        out_rgba[p * 4 + 1] = pal[pidx][1];
        out_rgba[p * 4 + 2] = pal[pidx][2];
        out_rgba[p * 4 + 3] = pal[pidx][3];
    }
}

void TextureData::decode_bc4_block(const u8 block[8], u8 out_r[16]) noexcept {
    u8 r0 = block[0];
    u8 r1 = block[1];
    u64 idx_64 = 0;
    for (u32 i = 0; i < 6; ++i) {
        idx_64 |= static_cast<u64>(block[2 + i]) << (i * 8);
    }

    u8 pal[8];
    pal[0] = r0;
    pal[1] = r1;
    if (r0 > r1) {
        for (u32 i = 1; i <= 6; ++i) {
            pal[1 + i] = static_cast<u8>(((7 - i) * r0 + i * r1 + 3) / 7);
        }
    } else {
        for (u32 i = 1; i <= 4; ++i) {
            pal[1 + i] = static_cast<u8>(((5 - i) * r0 + i * r1 + 2) / 5);
        }
        pal[6] = 0;
        pal[7] = 255;
    }

    for (u32 p = 0; p < 16; ++p) {
        u32 pidx = static_cast<u32>((idx_64 >> (3 * p)) & 0x7);
        out_r[p] = pal[pidx];
    }
}

void TextureData::decode_bc5_block(const u8 block[16], u8 out_rg[32]) noexcept {
    u8 r[16];
    u8 g[16];
    decode_bc4_block(block, r);
    decode_bc4_block(block + 8, g);

    for (u32 p = 0; p < 16; ++p) {
        out_rg[p * 2 + 0] = r[p];
        out_rg[p * 2 + 1] = g[p];
    }
}

void TextureData::decode_bc3_block(const u8 block[16], u8 out_rgba[64]) noexcept {
    u8 a[16];
    u8 c[64];
    decode_bc4_block(block, a);
    decode_bc1_block(block + 8, c, true);

    for (u32 p = 0; p < 16; ++p) {
        out_rgba[p * 4 + 0] = c[p * 4 + 0];
        out_rgba[p * 4 + 1] = c[p * 4 + 1];
        out_rgba[p * 4 + 2] = c[p * 4 + 2];
        out_rgba[p * 4 + 3] = a[p];
    }
}

void TextureData::decode_texel(u32 fmt, const u8* data, u32 w, u32 h, u32 x, u32 y, u8 out[4]) noexcept {
    if (!data || w == 0 || h == 0 || !out) return;

    if (x >= w) x = w - 1;
    if (y >= h) y = h - 1;

    if (fmt == 4) { // RGBA8
        usize off = (static_cast<usize>(y) * w + x) * 4;
        out[0] = data[off + 0];
        out[1] = data[off + 1];
        out[2] = data[off + 2];
        out[3] = data[off + 3];
        return;
    }

    u32 bx = x / 4;
    u32 by = y / 4;
    u32 blocks_x = (w + 3) / 4;
    if (blocks_x == 0) blocks_x = 1;
    u32 b_idx = by * blocks_x + bx;
    u32 lx = x % 4;
    u32 ly = y % 4;
    u32 k = ly * 4 + lx;

    if (fmt == 0) { // BC1
        const u8* blk = data + b_idx * 8;
        u8 rgba[64];
        decode_bc1_block(blk, rgba, false);
        out[0] = rgba[k * 4 + 0];
        out[1] = rgba[k * 4 + 1];
        out[2] = rgba[k * 4 + 2];
        out[3] = rgba[k * 4 + 3];
    } else if (fmt == 2) { // BC4
        const u8* blk = data + b_idx * 8;
        u8 r[16];
        decode_bc4_block(blk, r);
        out[0] = r[k];
        out[1] = 0;
        out[2] = 0;
        out[3] = 255;
    } else if (fmt == 3) { // BC5
        const u8* blk = data + b_idx * 16;
        u8 rg[32];
        decode_bc5_block(blk, rg);
        out[0] = rg[k * 2 + 0];
        out[1] = rg[k * 2 + 1];
        out[2] = 0;
        out[3] = 255;
    } else if (fmt == 1) { // BC3
        const u8* blk = data + b_idx * 16;
        u8 rgba[64];
        decode_bc3_block(blk, rgba);
        out[0] = rgba[k * 4 + 0];
        out[1] = rgba[k * 4 + 1];
        out[2] = rgba[k * 4 + 2];
        out[3] = rgba[k * 4 + 3];
    }
}

TextureError TextureData::init_from_blob(const u8* data, usize size) noexcept {
    is_valid_ = false;
    textures_ = nullptr;
    glyphs_ = nullptr;
    raw_data_ = nullptr;

    if (!data || size < sizeof(TextureHeader)) {
        return TextureError::TooSmall;
    }

    std::memcpy(&header_, data, sizeof(TextureHeader));

    if (header_.magic != 0x36325854u) { // 'TX26'
        return TextureError::BadMagic;
    }
    if (header_.version != 1) {
        return TextureError::BadVersion;
    }
    if (header_.total_bytes != size) {
        return TextureError::BadSize;
    }
    if (header_.texture_count < 1 || header_.texture_count > 16 || header_.glyph_count != 95) {
        return TextureError::BadHeader;
    }
    if ((header_.glyph_offset % 64) != 0 || header_.glyph_offset < 64 ||
        static_cast<u64>(header_.glyph_offset) + header_.glyph_count * sizeof(GlyphRec) > header_.total_bytes) {
        return TextureError::BadHeader;
    }

    usize min_tex_table_size = sizeof(TextureHeader) + header_.texture_count * sizeof(TextureRec);
    if (size < min_tex_table_size) {
        return TextureError::BadTextureTable;
    }

    // Verify hash over [64, total_bytes)
    u64 computed_hash = ron::xxh64(std::span<const u8>(data + 64, size - 64), 0);
    if (computed_hash != header_.content_hash) {
        return TextureError::BadHash;
    }

    const TextureRec* tex_table = reinterpret_cast<const TextureRec*>(data + sizeof(TextureHeader));
    for (u32 i = 0; i < header_.texture_count; ++i) {
        const TextureRec& rec = tex_table[i];
        if (rec.format > 4) {
            return TextureError::BadTextureTable;
        }
        if (rec.width == 0 || rec.height == 0 || rec.layers == 0 || rec.mips == 0) {
            return TextureError::BadTextureTable;
        }
        if ((rec.data_offset % 64) != 0 || rec.data_offset < 64 ||
            static_cast<u64>(rec.data_offset) + rec.data_bytes > header_.total_bytes) {
            return TextureError::BadTextureTable;
        }

        usize expected_bytes = 0;
        for (u32 l = 0; l < rec.mips; ++l) {
            u32 lw = std::max(1u, static_cast<u32>(rec.width) >> l);
            u32 lh = std::max(1u, static_cast<u32>(rec.height) >> l);
            expected_bytes += static_cast<usize>(rec.layers) * level_size(rec.format, lw, lh);
        }
        if (rec.data_bytes != expected_bytes) {
            return TextureError::BadTextureTable;
        }
    }

    textures_ = tex_table;
    glyphs_ = reinterpret_cast<const GlyphRec*>(data + header_.glyph_offset);
    raw_data_ = data;
    is_valid_ = true;

    return TextureError::Ok;
}

TextureError TextureData::load_file(const char* filepath, MemoryArena& arena) noexcept {
    if (!filepath) {
        return TextureError::FileNotFound;
    }
    FILE* f = std::fopen(filepath, "rb");
    if (!f) {
        return TextureError::FileNotFound;
    }

    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (size < static_cast<long>(sizeof(TextureHeader))) {
        std::fclose(f);
        return TextureError::TooSmall;
    }

    u8* buf = arena.alloc<u8>(static_cast<usize>(size));
    if (!buf) {
        std::fclose(f);
        return TextureError::TooSmall;
    }

    usize read_bytes = std::fread(buf, 1, static_cast<usize>(size), f);
    std::fclose(f);

    if (read_bytes != static_cast<usize>(size)) {
        return TextureError::ReadFailed;
    }

    return init_from_blob(buf, static_cast<usize>(size));
}

i32 TextureData::find(u64 name_hash) const noexcept {
    if (!is_valid_) return -1;
    for (u32 i = 0; i < header_.texture_count; ++i) {
        if (textures_[i].name_hash == name_hash) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

i32 TextureData::find(const char* name) const noexcept {
    return find(fnv1a64(name));
}

const u8* TextureData::texture_data(u32 idx) const noexcept {
    if (!is_valid_ || idx >= header_.texture_count) return nullptr;
    return raw_data_ + textures_[idx].data_offset;
}

usize TextureData::level_layer_offset(u32 idx, u32 level, u32 layer) const noexcept {
    if (!is_valid_ || idx >= header_.texture_count) return 0;
    const TextureRec& rec = textures_[idx];

    usize offset = 0;
    for (u32 l = 0; l < level; ++l) {
        u32 lw = std::max(1u, static_cast<u32>(rec.width) >> l);
        u32 lh = std::max(1u, static_cast<u32>(rec.height) >> l);
        offset += static_cast<usize>(rec.layers) * level_size(rec.format, lw, lh);
    }
    u32 lw = std::max(1u, static_cast<u32>(rec.width) >> level);
    u32 lh = std::max(1u, static_cast<u32>(rec.height) >> level);
    offset += static_cast<usize>(layer) * level_size(rec.format, lw, lh);
    return offset;
}

const u8* TextureData::level_data(u32 idx, u32 level, u32 layer) const noexcept {
    const u8* base = texture_data(idx);
    if (!base) return nullptr;
    return base + level_layer_offset(idx, level, layer);
}

const GlyphRec* TextureData::get_glyph(u8 ascii_char) const noexcept {
    if (!is_valid_ || ascii_char < 32 || ascii_char > 126) return nullptr;
    return &glyphs_[ascii_char - 32];
}

} // namespace ron::assets
