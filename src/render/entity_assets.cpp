#include "render/entity_assets.hpp"
#include "render/entity_vertex_layout.hpp"
#include "core/assert_log.hpp"
#include <algorithm>

namespace ron::render {

bool EntityAssets::init(VramLedger& ledger,
                        const char* models_path,
                        const char* textures_path)
{
    shutdown();

    if (!src_.init(models_path, textures_path)) {
        RON_LOG_ERROR("EntityAssets: failed to load source data");
        return false;
    }

    u32 n_models = src_.model_count();
    if (n_models == 0) {
        RON_LOG_ERROR("EntityAssets: 0 models in source data");
        return false;
    }

    // 1. ModelGeometry buffers (VBO, IBO, ModelGpu SSBO)
    if (!vbo_.create(ledger, VramCat::ModelGeometry, "entity_vbo",
                     src_.vertex_bytes(), src_.vertex_blob(), 0)) {
        RON_LOG_ERROR("EntityAssets: failed to create VBO");
        shutdown();
        return false;
    }

    if (!ibo_.create(ledger, VramCat::ModelGeometry, "entity_ibo",
                     src_.index_bytes(), src_.index_blob(), 0)) {
        RON_LOG_ERROR("EntityAssets: failed to create IBO");
        shutdown();
        return false;
    }

    u64 model_bytes = static_cast<u64>(n_models) * sizeof(ModelGpu);
    if (!model_ssbo_.create(ledger, VramCat::ModelGeometry, "entity_models",
                            model_bytes, src_.models_gpu().data(), 0)) {
        RON_LOG_ERROR("EntityAssets: failed to create ModelGpu SSBO");
        shutdown();
        return false;
    }

    // 2. VatTextures (2D RGBA16F, width 48, height 44 * model_count, mips 1)
    u32 vat_h = n_models * VAT_FRAMES;
    if (!vat_tex_.create_2d(ledger, VramCat::VatTextures, "entity_vat",
                            TexFmt::RGBA16F, VAT_COLS, vat_h, 1)) {
        RON_LOG_ERROR("EntityAssets: failed to create VAT texture");
        shutdown();
        return false;
    }
    vat_tex_.upload_level(0, 0, src_.vat_blob(), src_.vat_bytes());

    glTextureParameteri(vat_tex_.id(), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(vat_tex_.id(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(vat_tex_.id(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(vat_tex_.id(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameterf(vat_tex_.id(), GL_TEXTURE_MAX_ANISOTROPY, 1.0f);

    // 3. TrimTextures (4 textures)
    const char* trim_labels[4] = {
        "trim_albedo",
        "trim_normal",
        "trim_orm",
        "trim_emissive"
    };
    TexFmt trim_fmts[4] = {
        TexFmt::BC1_SRGB, // Q8 decision
        TexFmt::BC5,
        TexFmt::BC1,
        TexFmt::BC4
    };

    GLfloat max_device_aniso = 8.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &max_device_aniso);
    float aniso = std::min(8.0f, max_device_aniso);

    for (u32 i = 0; i < 4; ++i) {
        const TrimTexView& tv = src_.trim_texture(i);
        if (tv.mips == 0 || tv.width == 0 || tv.height == 0) {
            RON_LOG_ERROR("EntityAssets: invalid trim texture %u", i);
            shutdown();
            return false;
        }

        if (!trim_tex_[i].create_2d(ledger, VramCat::TrimTextures, trim_labels[i],
                                    trim_fmts[i], tv.width, tv.height, tv.mips)) {
            RON_LOG_ERROR("EntityAssets: failed to create trim texture %u", i);
            shutdown();
            return false;
        }

        for (u32 m = 0; m < tv.mips; ++m) {
            if (tv.levels[m].data && tv.levels[m].bytes > 0) {
                trim_tex_[i].upload_level(m, 0, tv.levels[m].data, tv.levels[m].bytes);
            }
        }

        GLuint tid = trim_tex_[i].id();
        glTextureParameteri(tid, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(tid, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(tid, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(tid, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTextureParameterf(tid, GL_TEXTURE_MAX_LOD, 5.0f);
        glTextureParameterf(tid, GL_TEXTURE_MAX_ANISOTROPY, aniso);
    }

    // 4. VAO setup (DSA)
    glCreateVertexArrays(1, &vao_);
    glVertexArrayVertexBuffer(vao_, 0, vbo_.id(), 0, ENTITY_VERTEX_STRIDE);
    glVertexArrayElementBuffer(vao_, ibo_.id());

    // Attribute 0: pos (vec3 float)
    glEnableVertexArrayAttrib(vao_, 0);
    glVertexArrayAttribFormat(vao_, 0, 3, GL_FLOAT, GL_FALSE, ENTITY_OFFSET_POS);
    glVertexArrayAttribBinding(vao_, 0, 0);

    // Attribute 1: normal (vec2 snorm16)
    glEnableVertexArrayAttrib(vao_, 1);
    glVertexArrayAttribFormat(vao_, 1, 2, GL_SHORT, GL_TRUE, ENTITY_OFFSET_NORMAL);
    glVertexArrayAttribBinding(vao_, 1, 0);

    // Attribute 2: uv (vec2 unorm16)
    glEnableVertexArrayAttrib(vao_, 2);
    glVertexArrayAttribFormat(vao_, 2, 2, GL_UNSIGNED_SHORT, GL_TRUE, ENTITY_OFFSET_UV);
    glVertexArrayAttribBinding(vao_, 2, 0);

    // Attribute 3: part (uint)
    glEnableVertexArrayAttrib(vao_, 3);
    glVertexArrayAttribIFormat(vao_, 3, 1, GL_UNSIGNED_SHORT, ENTITY_OFFSET_PART);
    glVertexArrayAttribBinding(vao_, 3, 0);

    // Calculate total VRAM bytes
    vram_bytes_ = vbo_.size() + ibo_.size() + model_ssbo_.size() +
                  gl_tex_bytes(vat_tex_.fmt(), vat_tex_.width(), vat_tex_.height(), vat_tex_.layers(), vat_tex_.mips());
    for (int i = 0; i < 4; ++i) {
        vram_bytes_ += gl_tex_bytes(trim_tex_[i].fmt(), trim_tex_[i].width(), trim_tex_[i].height(),
                                    trim_tex_[i].layers(), trim_tex_[i].mips());
    }

    return true;
}

void EntityAssets::shutdown() noexcept {
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    vbo_.destroy();
    ibo_.destroy();
    model_ssbo_.destroy();
    vat_tex_.destroy();
    for (int i = 0; i < 4; ++i) {
        trim_tex_[i].destroy();
    }
    vram_bytes_ = 0;
}

u64 EntityAssets::vram_bytes() const noexcept {
    return vram_bytes_;
}

} // namespace ron::render
