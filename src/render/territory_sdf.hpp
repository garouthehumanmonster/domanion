#pragma once

#include "core/types.hpp"
#include "render/vram_ledger.hpp"
#include "render/gl_objects.hpp"
#include "render/shader_library.hpp"
#include <glad/gl.h>

namespace ron::render {

class TerritorySdf {
public:
    TerritorySdf() noexcept = default;
    ~TerritorySdf() noexcept { destroy(); }

    TerritorySdf(const TerritorySdf&) = delete;
    TerritorySdf& operator=(const TerritorySdf&) = delete;

    bool init(VramLedger& ledger, int owner_n, const ShaderLibrary* shaders = nullptr) noexcept;
    void upload_owner(const u8* owner_n2) noexcept;
    void rebuild() noexcept;
    void debug_run_seed_only() noexcept;
    void destroy() noexcept;
    void set_shaders(const ShaderLibrary* shaders) noexcept { m_shaders = shaders; }

    GLuint owner_tex() const noexcept { return m_owner_tex.id(); }
    GLuint sdf_tex() const noexcept { return m_sdf_tex.id(); }
    GLuint seed_tex() const noexcept { return m_jfa_ping[m_last_out_idx].id(); }
    int grid() const noexcept { return m_g; }
    int owner_n() const noexcept { return m_owner_n; }
    u64 vram_bytes() const noexcept;

private:
    int m_owner_n{0};
    int m_g{0};
    int m_last_out_idx{0};
    const ShaderLibrary* m_shaders{nullptr};

    GpuTexture m_owner_tex;
    GpuTexture m_jfa_ping[2];
    GpuTexture m_sdf_tex;

    GLint m_loc_step_g{-1};
    GLint m_loc_step_k{-1};
    GLint m_loc_final_texel_m{-1};
    GLint m_loc_final_g{-1};
};

} // namespace ron::render
