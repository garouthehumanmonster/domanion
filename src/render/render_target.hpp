#pragma once

#include "core/types.hpp"
#include "render/vram_ledger.hpp"
#include "render/gl_objects.hpp"
#include <glad/gl.h>

namespace ron::render {

class RenderTarget {
public:
    RenderTarget() = default;
    ~RenderTarget() { destroy(); }

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    bool create(VramLedger& ledger, VramCat color_cat, VramCat depth_cat,
                const char* label, u32 w, u32 h, TexFmt color_fmt, bool with_depth);

    u32 fbo() const { return m_fbo; }
    u32 color_tex() const { return m_color_tex.id(); }
    u32 depth_tex() const { return m_depth_tex.id(); }
    u32 width() const { return m_w; }
    u32 height() const { return m_h; }

    void clear(const float rgba[4]); // FRAME-SAFE: color to rgba, depth to 0.0 (reverse-Z)
    void destroy();

private:
    u32 m_fbo = 0;
    u32 m_w = 0;
    u32 m_h = 0;
    bool m_with_depth = false;
    GpuTexture m_color_tex;
    GpuTexture m_depth_tex;
};

} // namespace ron::render
