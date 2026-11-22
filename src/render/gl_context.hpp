#pragma once

#include "core/types.hpp"
#include <glad/gl.h>
#include <SDL3/SDL.h>

namespace ron::render {

struct GlCaps {
    int major{0};
    int minor{0};
    char vendor[64]{0};
    char renderer[128]{0};
    char version[96]{0};
    int max_texture_size{0};
    int max_array_layers{0};
    int max_compute_invocations{0};
    int ssbo_offset_align{0};
    int ubo_offset_align{0};
    int max_ssbo_bindings{0};
    int max_color_attachments{0};
    i64 max_ssbo_block_size{0};
    float max_aniso{0.0f};
    bool has_nvx_meminfo{false};
    u32 nvx_total_kb{0};
    u32 nvx_dedicated_kb{0};
};

struct GlContextDesc {
    const char* title{"DOMINION 2026"};
    int width{1600};
    int height{900};
    bool hidden{false};
    bool fullscreen_borderless{false};
    int swap_interval{1};
    bool debug{false};
};

class GlContext {
public:
    GlContext() noexcept = default;
    ~GlContext() noexcept { destroy(); }

    GlContext(const GlContext&) = delete;
    GlContext& operator=(const GlContext&) = delete;

    bool create(const GlContextDesc& desc) noexcept;
    void destroy() noexcept;
    void swap() noexcept;

    SDL_Window* window() const noexcept { return window_; }
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    const GlCaps& caps() const noexcept { return caps_; }
    u32 debug_error_count() const noexcept { return debug_error_count_; }
    void reset_debug_counters() noexcept { debug_error_count_ = 0; }

    void on_debug_message(GLenum source, GLenum type, GLuint id, GLenum severity,
                          GLsizei length, const GLchar* message) noexcept;

private:
    SDL_Window* window_{nullptr};
    SDL_GLContext gl_context_{nullptr};
    int width_{0};
    int height_{0};
    GlCaps caps_{};
    u32 debug_error_count_{0};
    bool is_initialized_{false};
};

} // namespace ron::render
