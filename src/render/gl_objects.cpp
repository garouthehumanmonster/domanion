#include "render/gl_objects.hpp"
#include <algorithm>
#include <cmath>

namespace ron::render {

u32 mip_count(u32 w, u32 h) noexcept {
    u32 max_dim = std::max(w, h);
    if (max_dim == 0) return 0;
    return static_cast<u32>(std::floor(std::log2(static_cast<float>(max_dim)))) + 1;
}

struct TexFmtTraits {
    GLenum internal_format;
    GLenum format;
    GLenum type;
    bool compressed;
    u32 bytes_per_block_or_pixel;
};

static TexFmtTraits get_traits(TexFmt fmt) noexcept {
    switch (fmt) {
        case TexFmt::BC1:
            return {GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_RGB, GL_UNSIGNED_BYTE, true, 8};
        case TexFmt::BC1_SRGB:
            return {GL_COMPRESSED_SRGB_S3TC_DXT1_EXT, GL_RGB, GL_UNSIGNED_BYTE, true, 8};
        case TexFmt::BC3:
            return {GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_RGBA, GL_UNSIGNED_BYTE, true, 16};
        case TexFmt::BC4:
            return {GL_COMPRESSED_RED_RGTC1, GL_RED, GL_UNSIGNED_BYTE, true, 8};
        case TexFmt::BC5:
            return {GL_COMPRESSED_RG_RGTC2, GL_RG, GL_UNSIGNED_BYTE, true, 16};
        case TexFmt::RGBA8:
            return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false, 4};
        case TexFmt::RGBA16F:
            return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, false, 8};
        case TexFmt::RG16F:
            return {GL_RG16F, GL_RG, GL_HALF_FLOAT, false, 4};
        case TexFmt::R16:
            return {GL_R16, GL_RED, GL_UNSIGNED_SHORT, false, 2};
        case TexFmt::R32F:
            return {GL_R32F, GL_RED, GL_FLOAT, false, 4};
        case TexFmt::D32F:
            return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, false, 4};
        case TexFmt::R11G11B10F:
            return {GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, false, 4};
        case TexFmt::R8:
            return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, false, 1};
    }
    return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false, 4};
}

u64 gl_tex_bytes(TexFmt fmt, u32 w, u32 h, u32 layers, u32 mips) noexcept {
    TexFmtTraits t = get_traits(fmt);
    u64 total_per_layer = 0;

    for (u32 l = 0; l < mips; ++l) {
        u32 lw = std::max(1u, w >> l);
        u32 lh = std::max(1u, h >> l);

        if (t.compressed) {
            u32 bx = (lw + 3) / 4;
            u32 by = (lh + 3) / 4;
            if (bx == 0) bx = 1;
            if (by == 0) by = 1;
            total_per_layer += static_cast<u64>(bx) * by * t.bytes_per_block_or_pixel;
        } else {
            total_per_layer += static_cast<u64>(lw) * lh * t.bytes_per_block_or_pixel;
        }
    }

    return total_per_layer * std::max(1u, layers);
}

// GpuBuffer
bool GpuBuffer::create(VramLedger& ledger, VramCat cat, const char* label, u64 bytes,
                       const void* data, u32 gl_storage_flags) noexcept {
    destroy();

    u32 lid = 0;
    if (!ledger.reserve(cat, bytes, label, lid)) {
        return false;
    }

    ledger_ = &ledger;
    ledger_id_ = lid;
    size_ = bytes;

    glCreateBuffers(1, &id_);
    glNamedBufferStorage(id_, bytes, data, gl_storage_flags);
    return true;
}

void GpuBuffer::destroy() noexcept {
    if (id_ != 0) {
        glDeleteBuffers(1, &id_);
        id_ = 0;
    }
    if (ledger_ && ledger_id_ != 0) {
        ledger_->release(ledger_id_);
        ledger_id_ = 0;
        ledger_ = nullptr;
    }
    size_ = 0;
}

// GpuTexture
bool GpuTexture::create_2d(VramLedger& ledger, VramCat cat, const char* label, TexFmt fmt,
                          u32 w, u32 h, u32 mips) noexcept {
    destroy();

    u64 bytes = gl_tex_bytes(fmt, w, h, 1, mips);
    u32 lid = 0;
    if (!ledger.reserve(cat, bytes, label, lid)) {
        return false;
    }

    ledger_ = &ledger;
    ledger_id_ = lid;
    fmt_ = fmt;
    width_ = w;
    height_ = h;
    layers_ = 1;
    mips_ = mips;
    is_array_ = false;

    glCreateTextures(GL_TEXTURE_2D, 1, &id_);
    TexFmtTraits t = get_traits(fmt);
    glTextureStorage2D(id_, mips, t.internal_format, w, h);

    glTextureParameteri(id_, GL_TEXTURE_MIN_FILTER, mips > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTextureParameteri(id_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(id_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(id_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (fmt != TexFmt::D32F) {
        glTextureParameterf(id_, GL_TEXTURE_MAX_ANISOTROPY, 16.0f);
    }

    return true;
}

bool GpuTexture::create_2d_array(VramLedger& ledger, VramCat cat, const char* label, TexFmt fmt,
                                u32 w, u32 h, u32 layers, u32 mips) noexcept {
    destroy();

    u64 bytes = gl_tex_bytes(fmt, w, h, layers, mips);
    u32 lid = 0;
    if (!ledger.reserve(cat, bytes, label, lid)) {
        return false;
    }

    ledger_ = &ledger;
    ledger_id_ = lid;
    fmt_ = fmt;
    width_ = w;
    height_ = h;
    layers_ = layers;
    mips_ = mips;
    is_array_ = true;

    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &id_);
    TexFmtTraits t = get_traits(fmt);
    glTextureStorage3D(id_, mips, t.internal_format, w, h, layers);

    glTextureParameteri(id_, GL_TEXTURE_MIN_FILTER, mips > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTextureParameteri(id_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(id_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(id_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(id_, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    if (fmt != TexFmt::D32F) {
        glTextureParameterf(id_, GL_TEXTURE_MAX_ANISOTROPY, 16.0f);
    }

    return true;
}

void GpuTexture::upload_level(u32 level, u32 layer, const void* data, u64 bytes) noexcept {
    if (!id_ || !data || level >= mips_) return;
    u32 lw = std::max(1u, width_ >> level);
    u32 lh = std::max(1u, height_ >> level);
    TexFmtTraits t = get_traits(fmt_);

    if (!is_array_) {
        if (t.compressed) {
            glCompressedTextureSubImage2D(id_, level, 0, 0, lw, lh, t.internal_format, static_cast<GLsizei>(bytes), data);
        } else {
            glTextureSubImage2D(id_, level, 0, 0, lw, lh, t.format, t.type, data);
        }
    } else {
        if (t.compressed) {
            glCompressedTextureSubImage3D(id_, level, 0, 0, layer, lw, lh, 1, t.internal_format, static_cast<GLsizei>(bytes), data);
        } else {
            glTextureSubImage3D(id_, level, 0, 0, layer, lw, lh, 1, t.format, t.type, data);
        }
    }
}

void GpuTexture::destroy() noexcept {
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
    if (ledger_ && ledger_id_ != 0) {
        ledger_->release(ledger_id_);
        ledger_id_ = 0;
        ledger_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
    layers_ = 0;
    mips_ = 0;
}

} // namespace ron::render
