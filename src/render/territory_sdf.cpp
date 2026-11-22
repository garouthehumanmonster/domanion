#include "render/territory_sdf.hpp"
#include "render/gl_bindings.hpp"
#include <algorithm>
#include <utility>

namespace ron::render {

bool TerritorySdf::init(VramLedger& ledger, int owner_n, const ShaderLibrary* shaders) noexcept {
    destroy();
    m_owner_n = owner_n;
    m_g = 2 * owner_n;
    m_last_out_idx = 0;
    m_shaders = shaders;

    if (!m_owner_tex.create_2d(ledger, VramCat::TerrainMaps, "territory_owner",
                               TexFmt::R8, m_owner_n, m_owner_n, 1)) {
        return false;
    }
    glTextureParameteri(m_owner_tex.id(), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_owner_tex.id(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_owner_tex.id(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_owner_tex.id(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const char* ping_labels[2] = { "jfa_ping_0", "jfa_ping_1" };
    for (int i = 0; i < 2; ++i) {
        if (!m_jfa_ping[i].create_2d(ledger, VramCat::TerrainMaps, ping_labels[i],
                                    TexFmt::RG16F, m_g, m_g, 1)) {
            return false;
        }
        glTextureParameteri(m_jfa_ping[i].id(), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(m_jfa_ping[i].id(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(m_jfa_ping[i].id(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_jfa_ping[i].id(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    if (!m_sdf_tex.create_2d(ledger, VramCat::TerrainMaps, "territory_sdf",
                             TexFmt::RG16F, m_g, m_g, 1)) {
        return false;
    }
    glTextureParameteri(m_sdf_tex.id(), GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_sdf_tex.id(), GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_sdf_tex.id(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_sdf_tex.id(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return true;
}

void TerritorySdf::upload_owner(const u8* owner_n2) noexcept {
    if (!owner_n2 || m_owner_tex.id() == 0) return;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(m_owner_tex.id(), 0, 0, 0, m_owner_n, m_owner_n,
                       GL_RED, GL_UNSIGNED_BYTE, owner_n2);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void TerritorySdf::rebuild() noexcept {
    if (!m_shaders || m_g <= 0) return;

    u32 seed_prog = m_shaders->program(ProgramId::JfaSeed);
    u32 step_prog = m_shaders->program(ProgramId::JfaStep);
    u32 final_prog = m_shaders->program(ProgramId::JfaFinal);

    // 1. Seed pass
    glUseProgram(seed_prog);
    glBindTextureUnit(TEX_TERRITORY_OWNER, m_owner_tex.id());
    glBindImageTexture(IMG_JFA_OUT, m_jfa_ping[0].id(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
    glDispatchCompute((m_g + 15) / 16, (m_g + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    int in_idx = 0;
    int out_idx = 1;

    // 2. Step passes: k = G/2, G/4, ... 1
    glUseProgram(step_prog);
    glUniform1i(glGetUniformLocation(step_prog, "u_g"), m_g);
    GLint k_loc = glGetUniformLocation(step_prog, "u_k");

    for (int k = m_g / 2; k >= 1; k /= 2) {
        glUniform1i(k_loc, k);
        glBindImageTexture(IMG_JFA_IN, m_jfa_ping[in_idx].id(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG16F);
        glBindImageTexture(IMG_JFA_OUT, m_jfa_ping[out_idx].id(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
        glDispatchCompute((m_g + 15) / 16, (m_g + 15) / 16, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        std::swap(in_idx, out_idx);
    }

    // EXTRA_PASSES = 2 (tail passes: k = 2^(E-1) ... 1 -> k = 2, then k = 1)
    constexpr int EXTRA_PASSES = 2;
    for (int e = EXTRA_PASSES - 1; e >= 0; --e) {
        int k = 1 << e;
        glUniform1i(k_loc, k);
        glBindImageTexture(IMG_JFA_IN, m_jfa_ping[in_idx].id(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG16F);
        glBindImageTexture(IMG_JFA_OUT, m_jfa_ping[out_idx].id(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
        glDispatchCompute((m_g + 15) / 16, (m_g + 15) / 16, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        std::swap(in_idx, out_idx);
    }

    m_last_out_idx = in_idx;

    // 3. Final pass
    glUseProgram(final_prog);
    float texel_m = 4096.0f / static_cast<float>(m_g);
    glUniform1f(glGetUniformLocation(final_prog, "u_texel_m"), texel_m);
    glUniform1i(glGetUniformLocation(final_prog, "u_g"), m_g);

    glBindImageTexture(IMG_JFA_IN, m_jfa_ping[m_last_out_idx].id(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG16F);
    glBindTextureUnit(TEX_TERRITORY_OWNER, m_owner_tex.id());
    glBindImageTexture(IMG_SDF, m_sdf_tex.id(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
    glDispatchCompute((m_g + 15) / 16, (m_g + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}

void TerritorySdf::debug_run_seed_only() noexcept {
    if (!m_shaders || m_g <= 0) return;
    u32 seed_prog = m_shaders->program(ProgramId::JfaSeed);
    glUseProgram(seed_prog);
    glBindTextureUnit(TEX_TERRITORY_OWNER, m_owner_tex.id());
    glBindImageTexture(IMG_JFA_OUT, m_jfa_ping[0].id(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
    glDispatchCompute((m_g + 15) / 16, (m_g + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    m_last_out_idx = 0;
}

void TerritorySdf::destroy() noexcept {
    m_owner_tex.destroy();
    m_jfa_ping[0].destroy();
    m_jfa_ping[1].destroy();
    m_sdf_tex.destroy();
    m_owner_n = 0;
    m_g = 0;
    m_last_out_idx = 0;
}

u64 TerritorySdf::vram_bytes() const noexcept {
    if (m_owner_n <= 0 || m_g <= 0) return 0;
    u64 owner_b = static_cast<u64>(m_owner_n) * static_cast<u64>(m_owner_n);
    u64 ping_b = static_cast<u64>(m_g) * static_cast<u64>(m_g) * 4ULL;
    return owner_b + ping_b * 3ULL;
}

} // namespace ron::render
