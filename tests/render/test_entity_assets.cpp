#include "test_framework.hpp"
#include "render/gl_fixture.hpp"
#include "render/entity_assets.hpp"
#include "render/vram_ledger.hpp"
#include <vector>
#include <cstring>
#include <cmath>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

namespace ron::render {

// Test 7: Ent_AssetsUpload
RON_TEST(Ent_AssetsUpload) {
    GL_TEST_BEGIN();
    VramLedger ledger;
    EntityAssets assets;
    bool ok = assets.init(ledger,
                          RON_SOURCE_DIR "/assets/compiled/models/models_2026.mdbin",
                          RON_SOURCE_DIR "/assets/compiled/textures/textures_2026.tex26");
    RON_CHECK(ok);
    RON_CHECK(assets.model_count() > 0);

    const EntitySource& src = assets.source();

    // Read back whole ModelGpu SSBO
    std::vector<ModelGpu> gpu_models_read(src.model_count());
    glGetNamedBufferSubData(assets.model_ssbo(), 0,
                            src.model_count() * sizeof(ModelGpu),
                            gpu_models_read.data());
    RON_CHECK(std::memcmp(gpu_models_read.data(), src.models_gpu().data(),
                          src.model_count() * sizeof(ModelGpu)) == 0);

    // First and last 4096 bytes of VBO
    std::vector<u8> vbo_buf(4096);
    glGetNamedBufferSubData(assets.vbo(), 0, 4096, vbo_buf.data());
    RON_CHECK(std::memcmp(vbo_buf.data(), src.vertex_blob(), 4096) == 0);

    glGetNamedBufferSubData(assets.vbo(), src.vertex_bytes() - 4096, 4096, vbo_buf.data());
    RON_CHECK(std::memcmp(vbo_buf.data(), src.vertex_blob() + src.vertex_bytes() - 4096, 4096) == 0);

    // First and last 4096 bytes of IBO
    std::vector<u8> ibo_buf(4096);
    glGetNamedBufferSubData(assets.ibo(), 0, 4096, ibo_buf.data());
    RON_CHECK(std::memcmp(ibo_buf.data(), src.index_blob(), 4096) == 0);

    glGetNamedBufferSubData(assets.ibo(), src.index_bytes() - 4096, 4096, ibo_buf.data());
    RON_CHECK(std::memcmp(ibo_buf.data(), reinterpret_cast<const u8*>(src.index_blob()) + src.index_bytes() - 4096, 4096) == 0);

    // 200 deterministic VAT texels (glGetTextureSubImage GL_RGBA/GL_HALF_FLOAT, compare raw u16)
    std::vector<u16> vat_read(src.vat_rows() * src.vat_cols() * 4);
    glGetTextureSubImage(assets.vat_tex(), 0, 0, 0, 0,
                         src.vat_cols(), src.vat_rows(), 1,
                         GL_RGBA, GL_HALF_FLOAT,
                         vat_read.size() * sizeof(u16), vat_read.data());

    u32 total_texels = src.vat_rows() * src.vat_cols();
    for (u32 i = 0; i < 200; ++i) {
        u32 idx = (i * 2654435761u) % total_texels;
        for (int k = 0; k < 4; ++k) {
            RON_CHECK(vat_read[idx * 4 + k] == src.vat_blob()[idx * 4 + k]);
        }
    }

    // VAO attributes 0..3 enabled
    for (GLuint attr = 0; attr < 4; ++attr) {
        GLint enabled = 0;
        glGetVertexArrayIndexediv(assets.vao(), attr, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
        RON_CHECK(enabled != 0);
    }

    assets.shutdown();
    GL_TEST_END();
}

// Test 8: Ent_TrimTextures
RON_TEST(Ent_TrimTextures) {
    GL_TEST_BEGIN();
    VramLedger ledger;
    EntityAssets assets;
    bool ok = assets.init(ledger,
                          RON_SOURCE_DIR "/assets/compiled/models/models_2026.mdbin",
                          RON_SOURCE_DIR "/assets/compiled/textures/textures_2026.tex26");
    RON_CHECK(ok);

    const EntitySource& src = assets.source();

    GLenum expected_fmts[4] = {
        GL_COMPRESSED_SRGB_S3TC_DXT1_EXT,
        GL_COMPRESSED_RG_RGTC2,
        GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
        GL_COMPRESSED_RED_RGTC1
    };

    for (int i = 0; i < 4; ++i) {
        GLuint tid = assets.trim_tex(i);
        const TrimTexView& tv = src.trim_texture(i);

        // Internal format matches Q8 decision
        GLint ifmt = 0;
        glGetTextureLevelParameteriv(tid, 0, GL_TEXTURE_INTERNAL_FORMAT, &ifmt);
        RON_CHECK(static_cast<GLenum>(ifmt) == expected_fmts[i]);

        // GL_TEXTURE_MAX_LOD == 5.0f
        GLfloat max_lod = 0.0f;
        glGetTextureParameterfv(tid, GL_TEXTURE_MAX_LOD, &max_lod);
        RON_CHECK(std::abs(max_lod - 5.0f) < 1e-4f);

        // GL_TEXTURE_IMMUTABLE_FORMAT true
        GLint immutable = 0;
        glGetTextureParameteriv(tid, GL_TEXTURE_IMMUTABLE_FORMAT, &immutable);
        RON_CHECK(immutable != 0);

        // Level 0 first 64 blocks
        usize block_sz = (expected_fmts[i] == GL_COMPRESSED_RG_RGTC2) ? 16 : 8;
        usize blocks_64_bytes = 64 * block_sz;
        std::vector<u8> buf0(tv.levels[0].bytes);
        glGetCompressedTextureImage(tid, 0, buf0.size(), buf0.data());
        RON_CHECK(std::memcmp(buf0.data(), tv.levels[0].data, blocks_64_bytes) == 0);

        // Level 5 first 16 blocks
        if (tv.mips > 5 && tv.levels[5].data) {
            usize blocks_16_bytes = 16 * block_sz;
            std::vector<u8> buf5(tv.levels[5].bytes);
            glGetCompressedTextureImage(tid, 5, buf5.size(), buf5.data());
            RON_CHECK(std::memcmp(buf5.data(), tv.levels[5].data, std::min(blocks_16_bytes, static_cast<usize>(tv.levels[5].bytes))) == 0);
        }
    }

    assets.shutdown();
    GL_TEST_END();
}

// Test 9: Ent_VramAssets
RON_TEST(Ent_VramAssets) {
    GL_TEST_BEGIN();
    VramLedger ledger;
    u64 before = ledger.total_used();

    EntityAssets assets;
    bool ok = assets.init(ledger,
                          RON_SOURCE_DIR "/assets/compiled/models/models_2026.mdbin",
                          RON_SOURCE_DIR "/assets/compiled/textures/textures_2026.tex26");
    RON_CHECK(ok);

    u64 after = ledger.total_used();
    RON_CHECK(after - before == assets.vram_bytes());

    u64 model_geom = ledger.used(VramCat::ModelGeometry);
    u64 trim_tex   = ledger.used(VramCat::TrimTextures);
    u64 vat_tex    = ledger.used(VramCat::VatTextures);

    std::printf("[INFO] ModelGeometry: %.2f MiB (cap 128 MiB)\n", model_geom / (1024.0 * 1024.0));
    std::printf("[INFO] TrimTextures:  %.2f MiB (cap 64 MiB)\n", trim_tex / (1024.0 * 1024.0));
    std::printf("[INFO] VatTextures:   %.2f MiB (cap 32 MiB)\n", vat_tex / (1024.0 * 1024.0));

    RON_CHECK(model_geom <= (128ull << 20));
    RON_CHECK(trim_tex   <= (64ull << 20));
    RON_CHECK(vat_tex    <= (32ull << 20));

    assets.shutdown();
    GL_TEST_END();
}

} // namespace ron::render
