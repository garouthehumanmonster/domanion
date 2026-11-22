#include "render/render_target.hpp"
#include <string>

namespace ron::render {

bool RenderTarget::create(
    VramLedger& ledger,
    VramCat color_cat,
    VramCat depth_cat,
    const char* label,
    u32 w,
    u32 h,
    TexFmt color_fmt,
    bool with_depth)
{
    destroy();

    std::string col_label = std::string(label) + "_color";
    if (!m_color_tex.create_2d(ledger, color_cat, col_label.c_str(), color_fmt, w, h, 1)) {
        return false;
    }

    if (with_depth) {
        std::string depth_label = std::string(label) + "_depth";
        if (!m_depth_tex.create_2d(ledger, depth_cat, depth_label.c_str(), TexFmt::D32F, w, h, 1)) {
            destroy();
            return false;
        }
    }

    glCreateFramebuffers(1, &m_fbo);
    glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, m_color_tex.id(), 0);

    if (with_depth) {
        glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, m_depth_tex.id(), 0);
    }

    GLenum status = glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        destroy();
        return false;
    }

    m_w = w;
    m_h = h;
    m_with_depth = with_depth;
    return true;
}

void RenderTarget::clear(const float rgba[4]) {
    glClearNamedFramebufferfv(m_fbo, GL_COLOR, 0, rgba);
    if (m_with_depth) {
        const float depth_zero = 0.0f;
        glClearNamedFramebufferfv(m_fbo, GL_DEPTH, 0, &depth_zero);
    }
}

void RenderTarget::destroy() {
    if (m_fbo != 0) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    m_color_tex.destroy();
    m_depth_tex.destroy();
    m_w = 0;
    m_h = 0;
    m_with_depth = false;
}

} // namespace ron::render
