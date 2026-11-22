#include "render/gl_fixture.hpp"
#include "render/gl_objects.hpp"
#include "render/vram_ledger.hpp"
#include "assets/texture_data.hpp"
#include "core/arena.hpp"
#include <cstdio>
#include <cstring>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

namespace ron::render {

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

// 3. Vram_Budget
RON_TEST(Vram_Budget) {
    u64 sum_caps = 0;
    for (usize i = 0; i < static_cast<usize>(VramCat::COUNT); ++i) {
        sum_caps += VRAM_CATS[i].cap;
    }
    RON_CHECK_EQ(sum_caps, VRAM_TOTAL_CAP);

    VramLedger ledger;
    u32 id1 = 0;
    RON_CHECK(ledger.reserve(VramCat::TrimTextures, 64ull << 20, "trim64", id1));
    RON_CHECK(id1 != 0);
    RON_CHECK_EQ(ledger.used(VramCat::TrimTextures), 64ull << 20);

    // Reserving 1 more byte in TrimTextures fails
    u32 id_fail = 0;
    RON_CHECK(!ledger.reserve(VramCat::TrimTextures, 1, "trim_overflow", id_fail));
    RON_CHECK_EQ(id_fail, 0u);
    RON_CHECK_EQ(ledger.used(VramCat::TrimTextures), 64ull << 20);

    // Release first, then 1 byte succeeds
    ledger.release(id1);
    RON_CHECK_EQ(ledger.used(VramCat::TrimTextures), 0u);
    u32 id2 = 0;
    RON_CHECK(ledger.reserve(VramCat::TrimTextures, 1, "trim1", id2));
    RON_CHECK_EQ(ledger.used(VramCat::TrimTextures), 1u);

    // Reserve UINT64_MAX fails and used unchanged
    u64 before_used = ledger.total_used();
    RON_CHECK(!ledger.reserve(VramCat::TrimTextures, UINT64_MAX, "huge", id_fail));
    RON_CHECK_EQ(ledger.total_used(), before_used);

    // Reserve 0 fails
    RON_CHECK(!ledger.reserve(VramCat::TrimTextures, 0, "zero", id_fail));

    // total_used equals sum of used
    u64 calc_total = 0;
    for (usize i = 0; i < static_cast<usize>(VramCat::COUNT); ++i) {
        calc_total += ledger.used(static_cast<VramCat>(i));
    }
    RON_CHECK_EQ(ledger.total_used(), calc_total);

    // After freeze, any reserve fails
    ledger.freeze();
    RON_CHECK(ledger.frozen());
    RON_CHECK(!ledger.reserve(VramCat::UiAtlas, 1024, "frozen_fail", id_fail));
}

// 4. Vram_TexBytes
RON_TEST(Vram_TexBytes) {
    RON_CHECK_EQ(mip_count(2048, 2048), 12u);
    RON_CHECK_EQ(mip_count(1024, 512), 11u);
    RON_CHECK_EQ(mip_count(1, 1), 1u);

    RON_CHECK_EQ(gl_tex_bytes(TexFmt::BC1, 2048, 2048, 1, 12), 2796216ull);
    RON_CHECK_EQ(gl_tex_bytes(TexFmt::BC3, 2048, 2048, 1, 4), 5570560ull);
    RON_CHECK_EQ(gl_tex_bytes(TexFmt::BC5, 512, 512, 8, 10), 2796416ull);
    RON_CHECK_EQ(gl_tex_bytes(TexFmt::RGBA8, 1024, 512, 1, 1), 2097152ull);
    RON_CHECK_EQ(gl_tex_bytes(TexFmt::RGBA16F, 1920, 1080, 1, 1), 16588800ull);
    RON_CHECK_EQ(gl_tex_bytes(TexFmt::D32F, 640, 360, 1, 1), 921600ull);
}

// 5. Vram_RealTex26
RON_TEST(Vram_RealTex26) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(40), MiB(1), "tex_test_arena").is_ok());

    const char* path = get_textures_path();
    assets::TextureData td;
    assets::TextureError err = td.load_file(path, arena);
    if (err != assets::TextureError::Ok) {
        std::fprintf(stderr, "[FAIL] run the Gate 08 bake\n");
        ::ron::test::g_assert_failures++;
        return;
    }

    RON_CHECK(td.is_valid());
    RON_CHECK_EQ(td.texture_count(), 9u);

    for (u32 i = 0; i < 9; ++i) {
        const assets::TextureRec& rec = td.texture(i);

        TexFmt fmt = TexFmt::RGBA8;
        if (rec.format == 0) {
            fmt = (rec.flags & 1) ? TexFmt::BC1_SRGB : TexFmt::BC1;
        } else if (rec.format == 1) {
            fmt = TexFmt::BC3;
        } else if (rec.format == 2) {
            fmt = TexFmt::BC4;
        } else if (rec.format == 3) {
            fmt = TexFmt::BC5;
        } else if (rec.format == 4) {
            fmt = TexFmt::RGBA8;
        }

        u64 calculated_bytes = gl_tex_bytes(fmt, rec.width, rec.height, rec.layers, rec.mips);
        RON_CHECK_EQ(calculated_bytes, static_cast<u64>(rec.data_bytes));
    }
}

// 6. Gpu_ObjectsLedger
RON_TEST(Gpu_ObjectsLedger) {
    ::ron::render::test::test_gl();
    GL_TEST_BEGIN();

    VramLedger ledger;

    // Buffer test
    {
        GpuBuffer buf;
        u64 buf_bytes = 4ull << 20; // 4 MiB
        RON_CHECK(buf.create(ledger, VramCat::EntityBuffers, "test_buf_4mb", buf_bytes, nullptr, GL_DYNAMIC_STORAGE_BIT));
        RON_CHECK_EQ(ledger.used(VramCat::EntityBuffers), buf_bytes);
        RON_CHECK(buf.id() != 0);

        buf.destroy();
        RON_CHECK_EQ(ledger.used(VramCat::EntityBuffers), 0ull);
    }

    // RGBA8 8x8 texture
    {
        GpuTexture tex;
        RON_CHECK(tex.create_2d(ledger, VramCat::UiAtlas, "rgba8_8x8", TexFmt::RGBA8, 8, 8, 1));
        u8 pattern[8 * 8 * 4];
        for (u32 i = 0; i < 8 * 8 * 4; ++i) {
            pattern[i] = static_cast<u8>((i * 7 + 13) & 0xFF);
        }
        tex.upload_level(0, 0, pattern, sizeof(pattern));

        u8 readback[8 * 8 * 4]{0};
        glGetTextureImage(tex.id(), 0, GL_RGBA, GL_UNSIGNED_BYTE, sizeof(readback), readback);
        RON_CHECK(std::memcmp(pattern, readback, sizeof(pattern)) == 0);
        tex.destroy();
    }

    // BC1 4x4 texture (1 mip)
    {
        GpuTexture tex;
        RON_CHECK(tex.create_2d(ledger, VramCat::TrimTextures, "bc1_4x4", TexFmt::BC1, 4, 4, 1));
        u8 blk[8] = {0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0};
        tex.upload_level(0, 0, blk, sizeof(blk));

        u8 readback[8]{0};
        glGetCompressedTextureImage(tex.id(), 0, sizeof(readback), readback);
        RON_CHECK(std::memcmp(blk, readback, sizeof(blk)) == 0);
        tex.destroy();
    }

    // BC5 4x4 texture (1 mip)
    {
        GpuTexture tex;
        RON_CHECK(tex.create_2d(ledger, VramCat::TerrainTextures, "bc5_4x4", TexFmt::BC5, 4, 4, 1));
        u8 blk[16];
        for (u32 i = 0; i < 16; ++i) blk[i] = static_cast<u8>(i + 1);
        tex.upload_level(0, 0, blk, sizeof(blk));

        u8 readback[16]{0};
        glGetCompressedTextureImage(tex.id(), 0, sizeof(readback), readback);
        RON_CHECK(std::memcmp(blk, readback, sizeof(blk)) == 0);
        tex.destroy();
    }

    // Exceeding category cap (RGBA16F 8192x8192 in Ocean: 512 MiB > 64 MiB cap)
    {
        GpuTexture huge_tex;
        RON_CHECK(!huge_tex.create_2d(ledger, VramCat::Ocean, "huge_ocean", TexFmt::RGBA16F, 8192, 8192, 1));
        RON_CHECK_EQ(huge_tex.id(), 0u);
    }

    GL_TEST_END();
}

} // namespace ron::render
