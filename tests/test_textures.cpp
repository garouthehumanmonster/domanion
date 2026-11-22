#include "test_framework.hpp"
#include "core/arena.hpp"
#include "assets/texture_data.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

#ifndef RON_FAIL
#define RON_FAIL(msg) do { \
    std::fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
    ::ron::test::g_assert_failures++; \
    return; \
} while(0)
#endif

namespace ron {

static const char* get_textures_path() {
    static const char* paths[] = {
        RON_SOURCE_DIR "/assets/compiled/textures/textures_2026.tex26",
        "assets/compiled/textures/textures_2026.tex26",
        "../assets/compiled/textures/textures_2026.tex26",
        "../../assets/compiled/textures/textures_2026.tex26"
    };

    for (const char* p : paths) {
        FILE* f = fopen(p, "rb");
        if (f) {
            fclose(f);
            return p;
        }
    }
    return paths[0];
}

// 1. Tex_LoadHeader
RON_TEST(Tex_LoadHeader) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(96), MiB(1), "tex_arena_1").is_ok());

    const char* path = get_textures_path();
    FILE* f = fopen(path, "rb");
    if (!f) {
        RON_FAIL("run: python tools/texture_baker/bake_textures.py");
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    u8* orig = arena.alloc<u8>(static_cast<usize>(fsize));
    RON_CHECK(orig != nullptr);
    usize nread = fread(orig, 1, static_cast<usize>(fsize), f);
    RON_CHECK_EQ(nread, static_cast<usize>(fsize));
    fclose(f);

    assets::TextureData td;
    RON_CHECK(td.init_from_blob(orig, static_cast<usize>(fsize)) == assets::TextureError::Ok);
    RON_CHECK(td.is_valid());
    RON_CHECK_EQ(td.texture_count(), 9u);
    RON_CHECK_EQ(td.glyph_count(), 95u);
    RON_CHECK_EQ(td.header().magic, 0x36325854u);
    RON_CHECK_EQ(td.header().version, 1u);
    RON_CHECK_EQ(td.header().total_bytes, static_cast<u32>(fsize));

    // Also test td.load_file with another arena
    {
        MemoryArena arena_load;
        RON_CHECK(arena_load.init(MiB(40), MiB(1), "tex_arena_load").is_ok());
        assets::TextureData td_file;
        RON_CHECK(td_file.load_file(path, arena_load) == assets::TextureError::Ok);
        RON_CHECK(td_file.is_valid());
        RON_CHECK_EQ(td_file.texture_count(), 9u);
    }

    usize marker = arena.save_marker();
    // Corruption: flip payload byte at offset 128 -> BadHash
    {
        assets::TextureData bad_td;
        u8* copy = arena.alloc<u8>(static_cast<usize>(fsize));
        std::memcpy(copy, orig, static_cast<usize>(fsize));
        copy[128] ^= 0xFF;
        RON_CHECK(bad_td.init_from_blob(copy, static_cast<usize>(fsize)) == assets::TextureError::BadHash);
        arena.restore_marker(marker);
    }

    // Corruption: flip magic -> BadMagic
    {
        assets::TextureData bad_td;
        u8* copy = arena.alloc<u8>(static_cast<usize>(fsize));
        std::memcpy(copy, orig, static_cast<usize>(fsize));
        copy[0] ^= 0xFF;
        RON_CHECK(bad_td.init_from_blob(copy, static_cast<usize>(fsize)) == assets::TextureError::BadMagic);
        arena.restore_marker(marker);
    }

    // Corruption: truncate -> BadSize
    {
        assets::TextureData bad_td;
        RON_CHECK(bad_td.init_from_blob(orig, static_cast<usize>(fsize - 64)) == assets::TextureError::BadSize);
    }
}

// 2. Tex_TableIntegrity
RON_TEST(Tex_TableIntegrity) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(40), MiB(1), "tex_arena_2").is_ok());

    assets::TextureData td;
    RON_CHECK(td.load_file(get_textures_path(), arena) == assets::TextureError::Ok);

    struct ExpectedTex {
        const char* name;
        u32 fmt;
        u16 w, h, layers, mips;
        u32 flags;
    } expected[9] = {
        {"trim_albedo", 0, 2048, 2048, 1, 12, 1},
        {"trim_normal", 3, 2048, 2048, 1, 12, 0},
        {"trim_orm", 0, 2048, 2048, 1, 12, 0},
        {"trim_emissive", 2, 2048, 2048, 1, 12, 0},
        {"terrain_albedo", 0, 512, 512, 8, 10, 1},
        {"terrain_normal", 3, 512, 512, 8, 10, 0},
        {"terrain_rh", 0, 512, 512, 8, 10, 0},
        {"vfx_atlas", 1, 2048, 2048, 1, 4, 2},
        {"msdf_atlas", 4, 1024, 512, 1, 1, 0}
    };

    for (u32 i = 0; i < 9; ++i) {
        const assets::TextureRec& t = td.texture(i);
        RON_CHECK_EQ(t.name_hash, assets::TextureData::fnv1a64(expected[i].name));
        RON_CHECK_EQ(t.format, expected[i].fmt);
        RON_CHECK_EQ(t.width, expected[i].w);
        RON_CHECK_EQ(t.height, expected[i].h);
        RON_CHECK_EQ(t.layers, expected[i].layers);
        RON_CHECK_EQ(t.mips, expected[i].mips);
        RON_CHECK_EQ(t.flags, expected[i].flags);
        RON_CHECK_EQ(t.data_offset % 64, 0u);

        RON_CHECK_EQ(td.find(expected[i].name), static_cast<i32>(i));
        RON_CHECK_EQ(td.find(t.name_hash), static_cast<i32>(i));
    }

    RON_CHECK_EQ(td.header().glyph_offset % 64, 0u);
    RON_CHECK_EQ(td.glyph_count(), 95u);
}

// 3. Tex_BCDecodeVectors
RON_TEST(Tex_BCDecodeVectors) {
    // Vector 1 (BC1 solid white)
    {
        u8 blk[8] = {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        u8 rgba[64];
        assets::TextureData::decode_bc1_block(blk, rgba);
        for (int p = 0; p < 16; ++p) {
            RON_CHECK_EQ(rgba[p * 4 + 0], 255);
            RON_CHECK_EQ(rgba[p * 4 + 1], 255);
            RON_CHECK_EQ(rgba[p * 4 + 2], 255);
            RON_CHECK_EQ(rgba[p * 4 + 3], 255);
        }
    }

    // Vector 2 (BC1 4-step ramp: red -> blue)
    {
        u8 blk[8] = {0x00, 0xF8, 0x1F, 0x00, 0xE4, 0x00, 0x00, 0x00};
        u8 rgba[64];
        assets::TextureData::decode_bc1_block(blk, rgba);
        // Pixel 0: Red
        RON_CHECK_EQ(rgba[0], 255); RON_CHECK_EQ(rgba[1], 0); RON_CHECK_EQ(rgba[2], 0); RON_CHECK_EQ(rgba[3], 255);
        // Pixel 1: Blue
        RON_CHECK_EQ(rgba[4], 0); RON_CHECK_EQ(rgba[5], 0); RON_CHECK_EQ(rgba[6], 255); RON_CHECK_EQ(rgba[7], 255);
        // Pixel 2: 2/3 Red + 1/3 Blue
        RON_CHECK_EQ(rgba[8], 170); RON_CHECK_EQ(rgba[9], 0); RON_CHECK_EQ(rgba[10], 85); RON_CHECK_EQ(rgba[11], 255);
        // Pixel 3: 1/3 Red + 2/3 Blue
        RON_CHECK_EQ(rgba[12], 85); RON_CHECK_EQ(rgba[13], 0); RON_CHECK_EQ(rgba[14], 170); RON_CHECK_EQ(rgba[15], 255);
    }

    // Vector 3 (BC4 ramp)
    {
        u64 idx_64 = 0;
        for (u64 i = 0; i < 8; ++i) {
            idx_64 |= (i << (3 * i));
        }
        u8 blk[8];
        blk[0] = 255;
        blk[1] = 0;
        for (int i = 0; i < 6; ++i) {
            blk[2 + i] = static_cast<u8>((idx_64 >> (8 * i)) & 0xFF);
        }
        u8 r[16];
        assets::TextureData::decode_bc4_block(blk, r);
        u8 expected[8] = {255, 0, 219, 182, 146, 109, 73, 36};
        for (int i = 0; i < 8; ++i) {
            RON_CHECK_EQ(r[i], expected[i]);
        }
    }

    // Vector 4 (BC5 constant)
    {
        u8 blk[16]{};
        blk[0] = 200; blk[1] = 200;
        blk[8] = 100; blk[9] = 100;
        u8 rg[32];
        assets::TextureData::decode_bc5_block(blk, rg);
        for (int p = 0; p < 16; ++p) {
            RON_CHECK_EQ(rg[p * 2 + 0], 200);
            RON_CHECK_EQ(rg[p * 2 + 1], 100);
        }
    }
}

// 4. Tex_DecodeProbes
RON_TEST(Tex_DecodeProbes) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(40), MiB(1), "tex_arena_4").is_ok());

    assets::TextureData td;
    RON_CHECK(td.load_file(get_textures_path(), arena) == assets::TextureError::Ok);

    for (u32 i = 0; i < td.texture_count(); ++i) {
        const assets::TextureRec& t = td.texture(i);

        // Probe A at (37, 91) of level 0 layer 0
        u8 out_a[4]{};
        assets::TextureData::decode_texel(t.format, td.level_data(i, 0, 0), t.width, t.height, 37, 91, out_a);
        for (int c = 0; c < 4; ++c) {
            RON_CHECK_EQ(out_a[c], t.probe_a[c]);
        }

        // Probe B at (0, 0) of last mip level layer 0
        u32 last_lvl = t.mips - 1;
        u32 lw = std::max(1u, static_cast<u32>(t.width) >> last_lvl);
        u32 lh = std::max(1u, static_cast<u32>(t.height) >> last_lvl);
        u8 out_b[4]{};
        assets::TextureData::decode_texel(t.format, td.level_data(i, last_lvl, 0), lw, lh, 0, 0, out_b);
        for (int c = 0; c < 4; ++c) {
            RON_CHECK_EQ(out_b[c], t.probe_b[c]);
        }
    }
}

// 5. Tex_NormalsAndTrims
RON_TEST(Tex_NormalsAndTrims) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(40), MiB(1), "tex_arena_5").is_ok());

    assets::TextureData td;
    RON_CHECK(td.load_file(get_textures_path(), arena) == assets::TextureError::Ok);

    // Sample 100 points on trim_normal (texture 1)
    const assets::TextureRec& tn = td.texture(1);
    for (u32 sy = 0; sy < 10; ++sy) {
        for (u32 sx = 0; sx < 10; ++sx) {
            u32 x = 50 + sx * 190;
            u32 y = 50 + sy * 190;
            u8 out_n[4]{};
            assets::TextureData::decode_texel(tn.format, td.level_data(1, 0, 0), tn.width, tn.height, x, y, out_n);

            float nx = static_cast<float>(out_n[0]) / 255.0f * 2.0f - 1.0f;
            float ny = static_cast<float>(out_n[1]) / 255.0f * 2.0f - 1.0f;
            float nz_sq = 1.0f - nx * nx - ny * ny;
            float nz = nz_sq > 0.0f ? std::sqrt(nz_sq) : 0.0f;
            RON_CHECK(nz >= 0.58f);
        }
    }

    // Verify trim LOD clamping formula
    u32 heights[3] = {64, 128, 256};
    for (u32 h : heights) {
        u32 lod_max = std::min(5u, static_cast<u32>(std::log2(static_cast<float>(h))));
        RON_CHECK(lod_max <= 5u);
    }
}

// 6. Tex_FontIcons
RON_TEST(Tex_FontIcons) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(40), MiB(1), "tex_arena_6").is_ok());

    assets::TextureData td;
    RON_CHECK(td.load_file(get_textures_path(), arena) == assets::TextureError::Ok);

    // ASCII 32 (' ') check
    const assets::GlyphRec* space = td.get_glyph(' ');
    RON_CHECK(space != nullptr);
    RON_CHECK_EQ(space->cell_w, 48u);
    RON_CHECK_EQ(space->cell_h, 48u);
    RON_CHECK(space->advance_q > 0);

    const assets::GlyphRec* g_I = td.get_glyph('I');
    const assets::GlyphRec* g_W = td.get_glyph('W');
    RON_CHECK(g_I != nullptr && g_W != nullptr);
    RON_CHECK(g_I->advance_q < g_W->advance_q);

    // ASCII 32..126 checks
    for (u8 c = 32; c <= 126; ++c) {
        const assets::GlyphRec* g = td.get_glyph(c);
        RON_CHECK(g != nullptr);
        RON_CHECK_EQ(g->cell_w, 48u);
        RON_CHECK_EQ(g->cell_h, 48u);
        RON_CHECK(g->advance_q > 0);
        RON_CHECK_EQ(g->offset_y_q, 2304);
    }

    // Icon center check in msdf_atlas (texture 8)
    // Icon 31 ('ui_dot') is at col 7, row 3 in 64x64 icon grid
    // Cell 31 center: x = 7 * 64 + 32 = 480, y = 256 + 3 * 64 + 32 = 480
    const assets::TextureRec& msdf = td.texture(8);
    u8 out_icon[4]{};
    assets::TextureData::decode_texel(msdf.format, td.level_data(8, 0, 0), msdf.width, msdf.height, 480, 480, out_icon);

    u8 r = out_icon[0];
    u8 g = out_icon[1];
    u8 b = out_icon[2];
    u8 med = std::max(std::min(r, g), std::min(std::max(r, g), b));
    RON_CHECK(med > 127);
}

} // namespace ron
