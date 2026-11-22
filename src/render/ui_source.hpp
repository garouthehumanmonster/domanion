#pragma once

#include "core/types.hpp"
#include <memory>

namespace ron::render {

struct IconUvRect {
    float u0, v0, u1, v1;
};

class UiSource {
public:
    UiSource();
    ~UiSource();

    UiSource(const UiSource&) = delete;
    UiSource& operator=(const UiSource&) = delete;

    bool init(const char* textures_path = nullptr);
    bool is_valid() const noexcept;

    const u8* atlas_pixels() const noexcept;
    u32 atlas_width() const noexcept;
    u32 atlas_height() const noexcept;

    const IconUvRect& icon_uv(u32 idx) const noexcept;
    const char* icon_name(u32 idx) const noexcept;
    i32 find_icon(const char* name) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    IconUvRect icon_uvs_[32];
};

} // namespace ron::render
