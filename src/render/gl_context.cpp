#include "render/gl_context.hpp"
#include "core/assert_log.hpp"
#include <cstring>
#include <cstdio>

namespace ron::render {

static void GLAD_API_PTR gl_debug_callback_trampoline(GLenum source, GLenum type, GLuint id,
                                                      GLenum severity, GLsizei length,
                                                      const GLchar* message, const void* user_param) {
    (void)source;
    (void)length;
    auto* ctx = static_cast<GlContext*>(const_cast<void*>(user_param));
    if (ctx) {
        ctx->on_debug_message(source, type, id, severity, length, message);
    }
}

void GlContext::on_debug_message(GLenum source, GLenum type, GLuint id, GLenum severity,
                                GLsizei length, const GLchar* message) noexcept {
    (void)source;
    (void)length;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return;
    }

    if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH) {
        ++debug_error_count_;
        RON_LOG_ERROR("GL Error [%u]: %s", id, message ? message : "unknown");
    } else {
        RON_LOG_WARN("GL Debug [%u]: %s", id, message ? message : "unknown");
    }
}

bool GlContext::create(const GlContextDesc& desc) noexcept {
    destroy();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        RON_LOG_ERROR("SDL_Init(VIDEO) failed: %s", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, desc.debug ? SDL_GL_CONTEXT_DEBUG_FLAG : 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL;
    if (desc.hidden) {
        flags |= SDL_WINDOW_HIDDEN;
    }

    window_ = SDL_CreateWindow(desc.title ? desc.title : "DOMINION 2026", desc.width, desc.height, flags);
    if (!window_) {
        RON_LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        destroy();
        return false;
    }

    if (desc.fullscreen_borderless) {
        SDL_SetWindowFullscreen(window_, true);
    }

    gl_context_ = SDL_GL_CreateContext(window_);
    if (!gl_context_) {
        RON_LOG_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        destroy();
        return false;
    }

    if (!SDL_GL_MakeCurrent(window_, gl_context_)) {
        RON_LOG_ERROR("SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        destroy();
        return false;
    }

    int version = gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress));
    if (!version) {
        RON_LOG_ERROR("gladLoadGL failed to load OpenGL 4.6 core entry points");
        destroy();
        return false;
    }

    SDL_GL_SetSwapInterval(desc.swap_interval);

    // Install debug callback
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
    glDebugMessageCallback(gl_debug_callback_trampoline, this);

    // Query dimensions
    SDL_GetWindowSizeInPixels(window_, &width_, &height_);

    // Query caps
    caps_ = GlCaps{};
    const char* v = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* r = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));

    if (v) std::snprintf(caps_.vendor, sizeof(caps_.vendor), "%s", v);
    if (r) std::snprintf(caps_.renderer, sizeof(caps_.renderer), "%s", r);
    if (ver) std::snprintf(caps_.version, sizeof(caps_.version), "%s", ver);

    glGetIntegerv(GL_MAJOR_VERSION, &caps_.major);
    glGetIntegerv(GL_MINOR_VERSION, &caps_.minor);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &caps_.max_texture_size);
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &caps_.max_array_layers);
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &caps_.max_compute_invocations);
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &caps_.ssbo_offset_align);
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &caps_.ubo_offset_align);
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &caps_.max_ssbo_bindings);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &caps_.max_color_attachments);

    glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, reinterpret_cast<GLint64*>(&caps_.max_ssbo_block_size));
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &caps_.max_aniso);

    caps_.has_nvx_meminfo = (GLAD_GL_NVX_gpu_memory_info != 0);
    if (caps_.has_nvx_meminfo) {
        glGetIntegerv(0x9047, reinterpret_cast<GLint*>(&caps_.nvx_dedicated_kb));
        glGetIntegerv(0x9048, reinterpret_cast<GLint*>(&caps_.nvx_total_kb));
        RON_LOG_INFO("NVX Meminfo: Dedicated=%u KB, Total=%u KB", caps_.nvx_dedicated_kb, caps_.nvx_total_kb);
    }

    RON_LOG_INFO("OpenGL %s | Vendor: %s | Renderer: %s", caps_.version, caps_.vendor, caps_.renderer);

    // Validate hard requirements
    if (!GLAD_GL_VERSION_4_6) {
        RON_LOG_ERROR("Validation failure: GLAD_GL_VERSION_4_6 is false");
        destroy();
        return false;
    }

    GLint profile_mask = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile_mask);
    if (!(profile_mask & GL_CONTEXT_CORE_PROFILE_BIT)) {
        RON_LOG_ERROR("Validation failure: Not in OpenGL Core Profile (mask=0x%X)", profile_mask);
        destroy();
        return false;
    }

    if (!GLAD_GL_EXT_texture_compression_s3tc) {
        RON_LOG_ERROR("Validation failure: GL_EXT_texture_compression_s3tc missing");
        destroy();
        return false;
    }
    if (!GLAD_GL_EXT_texture_sRGB) {
        RON_LOG_ERROR("Validation failure: GL_EXT_texture_sRGB missing");
        destroy();
        return false;
    }

    if (caps_.max_texture_size < 8192) {
        RON_LOG_ERROR("Validation failure: max_texture_size %d < 8192", caps_.max_texture_size);
        destroy();
        return false;
    }
    if (caps_.max_array_layers < 256) {
        RON_LOG_ERROR("Validation failure: max_array_layers %d < 256", caps_.max_array_layers);
        destroy();
        return false;
    }
    if (caps_.max_ssbo_block_size < 134217728) {
        RON_LOG_ERROR("Validation failure: max_ssbo_block_size %lld < 134217728", static_cast<long long>(caps_.max_ssbo_block_size));
        destroy();
        return false;
    }
    if (caps_.max_compute_invocations < 1024) {
        RON_LOG_ERROR("Validation failure: max_compute_invocations %d < 1024", caps_.max_compute_invocations);
        destroy();
        return false;
    }
    if (caps_.max_ssbo_bindings < 8) {
        RON_LOG_ERROR("Validation failure: max_ssbo_bindings %d < 8", caps_.max_ssbo_bindings);
        destroy();
        return false;
    }
    if (caps_.max_aniso < 16.0f) {
        RON_LOG_ERROR("Validation failure: max_aniso %f < 16", caps_.max_aniso);
        destroy();
        return false;
    }

    if (!std::strstr(caps_.renderer, "NVIDIA") && !std::strstr(caps_.renderer, "nvidia")) {
        RON_LOG_WARN("Renderer '%s' is not NVIDIA! Set exe to High Performance in Windows Graphics Settings.", caps_.renderer);
    }

    is_initialized_ = true;
    return true;
}

void GlContext::destroy() noexcept {
    if (gl_context_) {
        SDL_GL_DestroyContext(gl_context_);
        gl_context_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    is_initialized_ = false;
    width_ = 0;
    height_ = 0;
    debug_error_count_ = 0;
}

void GlContext::swap() noexcept {
    if (window_) {
        SDL_GL_SwapWindow(window_);
    }
}

} // namespace ron::render
