#include "render/ui_source.hpp"
#include "assets/texture_data.hpp"
#include "core/arena.hpp"
#include "core/assert_log.hpp"
#include <cstdio>
#include <cstring>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

namespace ron::render {

static const char* const K_ICON_NAMES[32] = {
    "frame_friendly", "frame_hostile", "frame_neutral", "frame_unknown",
    "sym_infantry", "sym_armor", "sym_artillery", "sym_air",
    "sym_naval", "sym_recon", "sym_missile", "sym_engineer",
    "sym_logistics", "sym_drone", "res_food", "res_timber",
    "res_metal", "res_wealth", "res_knowledge", "res_oil",
    "res_silicon", "ui_arrow_up", "ui_arrow_down", "ui_check",
    "ui_cross", "ui_plus", "ui_minus", "ui_play",
    "ui_ring", "ui_square_ring", "ui_gear", "ui_dot"
};

static const char* resolve_textures_path(const char* explicit_path) {
    if (explicit_path) return explicit_path;
    static const char* paths[] = {
        RON_SOURCE_DIR "/assets/compiled/textures/textures_2026.tex26",
        "assets/compiled/textures/textures_2026.tex26",
        "../assets/compiled/textures/textures_2026.tex26",
        "../../assets/compiled/textures/textures_2026.tex26"
    };
    for (const char* p : paths) {
        FILE* f = std::fopen(p, "rb");
        if (f) {
            std::fclose(f);
            return p;
        }
    }
    return paths[0];
}

struct UiSource::Impl {
    MemoryArena tex_arena;
    assets::TextureData tex_data;
    i32 atlas_idx{-1};
    bool valid{false};
};

UiSource::UiSource() : impl_(std::make_unique<Impl>()) {
    std::memset(icon_uvs_, 0, sizeof(icon_uvs_));
}

UiSource::~UiSource() = default;

bool UiSource::init(const char* textures_path) {
    impl_->valid = false;
    const char* resolved_tex = resolve_textures_path(textures_path);

    if (!impl_->tex_arena.init(MiB(96), MiB(1), "ui_source_arena").is_ok()) {
        RON_LOG_ERROR("UiSource: failed to allocate memory arena");
        return false;
    }

    auto tex_err = impl_->tex_data.load_file(resolved_tex, impl_->tex_arena);
    if (tex_err != assets::TextureError::Ok || !impl_->tex_data.is_valid()) {
        RON_LOG_ERROR("UiSource: failed to load textures from %s", resolved_tex);
        return false;
    }

    impl_->atlas_idx = impl_->tex_data.find("msdf_atlas");
    if (impl_->atlas_idx < 0) {
        if (impl_->tex_data.texture_count() > 8) {
            impl_->atlas_idx = 8;
        } else {
            RON_LOG_ERROR("UiSource: msdf_atlas texture not found in %s", resolved_tex);
            return false;
        }
    }

    const auto& rec = impl_->tex_data.texture(static_cast<u32>(impl_->atlas_idx));
    if (rec.width != 1024 || rec.height != 512 || rec.format != 4) {
        RON_LOG_ERROR("UiSource: msdf_atlas unexpected dimensions or format (%ux%u, fmt %u)",
                      rec.width, rec.height, rec.format);
        return false;
    }

    for (u32 i = 0; i < 32; ++i) {
        u32 col = i % 16;
        u32 row = i / 16;
        float x = static_cast<float>(col * 64);
        float y = static_cast<float>(384 + row * 64);

        icon_uvs_[i].u0 = x / 1024.0f;
        icon_uvs_[i].u1 = (x + 64.0f) / 1024.0f;
        icon_uvs_[i].v0 = y / 512.0f;
        icon_uvs_[i].v1 = (y + 64.0f) / 512.0f;
    }

    impl_->valid = true;
    return true;
}

bool UiSource::is_valid() const noexcept {
    return impl_->valid;
}

const u8* UiSource::atlas_pixels() const noexcept {
    if (!impl_->valid || impl_->atlas_idx < 0) return nullptr;
    return impl_->tex_data.level_data(static_cast<u32>(impl_->atlas_idx), 0, 0);
}

u32 UiSource::atlas_width() const noexcept {
    return 1024;
}

u32 UiSource::atlas_height() const noexcept {
    return 512;
}

const IconUvRect& UiSource::icon_uv(u32 idx) const noexcept {
    static const IconUvRect k_zero{0.0f, 0.0f, 0.0f, 0.0f};
    if (idx < 32) return icon_uvs_[idx];
    return k_zero;
}

const char* UiSource::icon_name(u32 idx) const noexcept {
    if (idx < 32) return K_ICON_NAMES[idx];
    return "unknown";
}

i32 UiSource::find_icon(const char* name) const noexcept {
    if (!name) return -1;
    for (i32 i = 0; i < 32; ++i) {
        if (std::strcmp(name, K_ICON_NAMES[i]) == 0) return i;
    }
    return -1;
}

} // namespace ron::render
