#pragma once

#include "core/types.hpp"
#include "render/gl_objects.hpp"
#include "render/gl_bindings.hpp"
#include "render/ui_source.hpp"

namespace ron::render {

class UiAtlas {
public:
    UiAtlas() noexcept = default;
    ~UiAtlas() noexcept { destroy(); }

    UiAtlas(const UiAtlas&) = delete;
    UiAtlas& operator=(const UiAtlas&) = delete;
    UiAtlas(UiAtlas&&) noexcept = default;
    UiAtlas& operator=(UiAtlas&&) noexcept = default;

    bool init(VramLedger& ledger, const UiSource& ui_source) noexcept;
    void bind(u32 unit = TEX_MSDF) const noexcept;
    void destroy() noexcept;
    void destroy(VramLedger&) noexcept { destroy(); }

    u32 texture_id() const noexcept { return tex_.id(); }
    const GpuTexture& texture() const noexcept { return tex_; }
    bool is_valid() const noexcept { return tex_.id() != 0; }
    u32 width() const noexcept { return tex_.width(); }
    u32 height() const noexcept { return tex_.height(); }

private:
    GpuTexture tex_;
};

} // namespace ron::render
