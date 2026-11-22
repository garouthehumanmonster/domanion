#include "render/ui_atlas.hpp"
#include "core/assert_log.hpp"
#include <glad/gl.h>

namespace ron::render {

bool UiAtlas::init(VramLedger& ledger, const UiSource& ui_source) noexcept {
    destroy();

    if (!ui_source.is_valid() || !ui_source.atlas_pixels()) {
        RON_LOG_ERROR("UiAtlas::init: invalid UiSource or null atlas pixels");
        return false;
    }

    u32 w = ui_source.atlas_width();
    u32 h = ui_source.atlas_height();
    if (w != 1024 || h != 512) {
        RON_LOG_ERROR("UiAtlas::init: unexpected atlas size %ux%u", w, h);
        return false;
    }

    // 1024x512 RGBA8 (2 MiB) in VramCat::UiAtlas
    if (!tex_.create_2d(ledger, VramCat::UiAtlas, "ui_msdf_atlas", TexFmt::RGBA8, w, h, 1)) {
        RON_LOG_ERROR("UiAtlas::init: failed to create GpuTexture in VramCat::UiAtlas");
        return false;
    }

    u64 bytes = static_cast<u64>(w) * h * 4;
    tex_.upload_level(0, 0, ui_source.atlas_pixels(), bytes);

    // Linear filtering, clamp to edge
    glTextureParameteri(tex_.id(), GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(tex_.id(), GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex_.id(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex_.id(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return true;
}

void UiAtlas::bind(u32 unit) const noexcept {
    if (tex_.id() != 0) {
        glBindTextureUnit(unit, tex_.id());
    }
}

void UiAtlas::destroy() noexcept {
    tex_.destroy();
}

} // namespace ron::render
