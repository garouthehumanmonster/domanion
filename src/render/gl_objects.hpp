#pragma once

#include "core/types.hpp"
#include "render/vram_ledger.hpp"
#include <glad/gl.h>

namespace ron::render {

enum class TexFmt : u8 {
    BC1,
    BC1_SRGB,
    BC3,
    BC4,
    BC5,
    RGBA8,
    RGBA16F,
    RG16F,
    R16,
    R32F,
    D32F,
    R11G11B10F,
    R8
};

u32 mip_count(u32 w, u32 h) noexcept;
u64 gl_tex_bytes(TexFmt fmt, u32 w, u32 h, u32 layers, u32 mips) noexcept;

class GpuBuffer {
public:
    GpuBuffer() noexcept = default;
    ~GpuBuffer() noexcept { destroy(); }

    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    bool create(VramLedger& ledger, VramCat cat, const char* label, u64 bytes,
                const void* data, u32 gl_storage_flags) noexcept;
    void destroy() noexcept;

    u32 id() const noexcept { return id_; }
    u64 size() const noexcept { return size_; }

private:
    VramLedger* ledger_{nullptr};
    u32 ledger_id_{0};
    u32 id_{0};
    u64 size_{0};
};

class GpuTexture {
public:
    GpuTexture() noexcept = default;
    ~GpuTexture() noexcept { destroy(); }

    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;

    bool create_2d(VramLedger& ledger, VramCat cat, const char* label, TexFmt fmt,
                   u32 w, u32 h, u32 mips) noexcept;
    bool create_2d_array(VramLedger& ledger, VramCat cat, const char* label, TexFmt fmt,
                         u32 w, u32 h, u32 layers, u32 mips) noexcept;
    void upload_level(u32 level, u32 layer, const void* data, u64 bytes) noexcept;
    void destroy() noexcept;

    u32 id() const noexcept { return id_; }
    TexFmt fmt() const noexcept { return fmt_; }
    u32 width() const noexcept { return width_; }
    u32 height() const noexcept { return height_; }
    u32 layers() const noexcept { return layers_; }
    u32 mips() const noexcept { return mips_; }

private:
    VramLedger* ledger_{nullptr};
    u32 ledger_id_{0};
    u32 id_{0};
    TexFmt fmt_{TexFmt::RGBA8};
    u32 width_{0};
    u32 height_{0};
    u32 layers_{0};
    u32 mips_{0};
    bool is_array_{false};
};

} // namespace ron::render
