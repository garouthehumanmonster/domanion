#include "render/fog_gpu.hpp"
#include "render/gl_bindings.hpp"

namespace ron::render {

bool FogGpu::init(VramLedger& ledger, int n, const ShaderLibrary* shaders) noexcept {
    destroy();
    m_n = n;
    m_shaders = shaders;

    if (!m_tvis.create_2d(ledger, VramCat::TerrainMaps, "FogTvis", TexFmt::R8, n, n, 1)) {
        return false;
    }
    glTextureParameteri(m_tvis.id(), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_tvis.id(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    const u8 zero = 0;
    glClearTexImage(m_tvis.id(), 0, GL_RED, GL_UNSIGNED_BYTE, &zero);

    if (!m_texp.create_2d(ledger, VramCat::TerrainMaps, "FogTexp", TexFmt::R8, n, n, 1)) {
        m_tvis.destroy();
        return false;
    }
    glTextureParameteri(m_texp.id(), GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_texp.id(), GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glClearTexImage(m_texp.id(), 0, GL_RED, GL_UNSIGNED_BYTE, &zero);

    if (!m_smooth.create_2d(ledger, VramCat::TerrainMaps, "FogSmooth", TexFmt::RG16F, n, n, 1)) {
        m_tvis.destroy();
        m_texp.destroy();
        return false;
    }
    const float ones[2] = {1.0f, 1.0f};
    glClearTexImage(m_smooth.id(), 0, GL_RG, GL_FLOAT, ones);

    return true;
}

void FogGpu::destroy() noexcept {
    m_tvis.destroy();
    m_texp.destroy();
    m_smooth.destroy();
    m_n = 0;
    m_shaders = nullptr;
}

void FogGpu::upload(const FogMap& map) noexcept {
    if (m_tvis.id() == 0 || m_texp.id() == 0 || m_n <= 0) return;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(m_tvis.id(), 0, 0, 0, m_n, m_n,
                       GL_RED, GL_UNSIGNED_BYTE, map.visible_data());
    glTextureSubImage2D(m_texp.id(), 0, 0, 0, m_n, m_n,
                       GL_RED, GL_UNSIGNED_BYTE, map.explored_data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void FogGpu::step(float dt) noexcept {
    if (!m_shaders || m_n <= 0 || m_smooth.id() == 0) return;

    u32 prog = m_shaders->program(ProgramId::FogSmooth);
    if (prog == 0) return;

    glUseProgram(prog);
    glBindTextureUnit(TEX_FOG_TVIS, m_tvis.id());
    glBindTextureUnit(TEX_FOG_TEXP, m_texp.id());
    glBindImageTexture(IMG_FOG_SMOOTH, m_smooth.id(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RG16F);

    glUniform1f(glGetUniformLocation(prog, "u_dt"), dt);
    glUniform1i(glGetUniformLocation(prog, "u_n"), m_n);

    glDispatchCompute((m_n + 15) / 16, (m_n + 15) / 16, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}

void FogGpu::snap() noexcept {
    step(1000.0f);
}

u64 FogGpu::vram_bytes() const noexcept {
    return static_cast<u64>(m_n) * m_n * 6;
}

} // namespace ron::render
