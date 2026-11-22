#pragma once

#include "core/types.hpp"
#include "render/vram_ledger.hpp"
#include "render/gl_objects.hpp"
#include "render/shader_library.hpp"
#include "render/overlay_types.hpp"
#include "render/minimap_cpu.hpp"
#include "render/height_sampler.hpp"
#include "render/world_overlays.hpp"
#include <glad/gl.h>

namespace ron::render {

class MinimapRenderer {
public:
    struct Scene {
        float quad_xz[8]{0.0f};
        const Blip* blips{nullptr};
        int blip_count{0};
        bool fog_on{false};
    };

    MinimapRenderer() noexcept = default;
    ~MinimapRenderer() noexcept { destroy(); }

    MinimapRenderer(const MinimapRenderer&) = delete;
    MinimapRenderer& operator=(const MinimapRenderer&) = delete;

    bool init(VramLedger& ledger, WorldOverlays& overlays, const ShaderLibrary* shaders = nullptr) noexcept;
    void destroy() noexcept;

    void bake_base(const HeightSampler& sampler) noexcept;

    void render(const Scene& scene, float time_s) noexcept;
    void blit(const PxRect& rect, int window_w, int window_h) noexcept;

    void set_shaders(const ShaderLibrary* shaders) noexcept { m_shaders = shaders; }

    GLuint texture() const noexcept { return m_target_tex.id(); }
    GLuint target_tex() const noexcept { return m_target_tex.id(); }
    GLuint base_tex() const noexcept { return m_base_tex.id(); }
    u64 vram_bytes() const noexcept;

private:
    WorldOverlays* m_overlays{nullptr};
    const ShaderLibrary* m_shaders{nullptr};

    GpuTexture m_base_tex;
    GpuTexture m_target_tex;
    GLuint m_fbo{0};

    GpuBuffer m_blip_ssbo;
    GpuBuffer m_ubo_buf;
};

} // namespace ron::render
