#pragma once

#include "core/types.hpp"
#include "render/vram_ledger.hpp"
#include "render/gl_objects.hpp"
#include "render/shader_library.hpp"
#include "render/overlay_types.hpp"
#include "render/fog_map.hpp"
#include <glad/gl.h>

namespace ron::render {

class FogGpu {
public:
    FogGpu() noexcept = default;
    ~FogGpu() noexcept { destroy(); }

    FogGpu(const FogGpu&) = delete;
    FogGpu& operator=(const FogGpu&) = delete;

    bool init(VramLedger& ledger, int n = FOG_N, const ShaderLibrary* shaders = nullptr) noexcept;
    void destroy() noexcept;

    void upload(const FogMap& map) noexcept;
    void step(float dt) noexcept;
    void snap() noexcept;

    void set_shaders(const ShaderLibrary* shaders) noexcept { m_shaders = shaders; }

    GLuint smooth_tex() const noexcept { return m_smooth.id(); }
    GLuint tvis_tex() const noexcept { return m_tvis.id(); }
    GLuint texp_tex() const noexcept { return m_texp.id(); }
    int n() const noexcept { return m_n; }
    u64 vram_bytes() const noexcept;

private:
    int m_n{0};
    const ShaderLibrary* m_shaders{nullptr};

    GpuTexture m_tvis;
    GpuTexture m_texp;
    GpuTexture m_smooth;
};

} // namespace ron::render
