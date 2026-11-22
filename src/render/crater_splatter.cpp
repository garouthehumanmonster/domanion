#include "render/crater_splatter.hpp"
#include "render/gl_bindings.hpp"
#include <algorithm>

namespace ron::render {

bool CraterSplatter::init(VramLedger& ledger, int n, const ShaderLibrary* shaders) noexcept {
    destroy();
    m_n = n;
    m_shaders = shaders;
    m_head = 0;
    m_tail = 0;
    m_count = 0;
    m_dropped = 0;
    m_applied_total = 0;

    if (!m_crater_tex.create_2d(ledger, VramCat::TerrainMaps, "CraterTex", TexFmt::RGBA8, n, n, 1)) {
        return false;
    }

    const u8 clear_clr[4] = {128, 0, 0, 0};
    glClearTexImage(m_crater_tex.id(), 0, GL_RGBA, GL_UNSIGNED_BYTE, clear_clr);

    if (!m_splat_ssbo.create(ledger, VramCat::TerrainMaps, "SplatSsbo", sizeof(CraterSplat) * 64, nullptr, GL_DYNAMIC_STORAGE_BIT)) {
        m_crater_tex.destroy();
        return false;
    }

    return true;
}

void CraterSplatter::destroy() noexcept {
    m_crater_tex.destroy();
    m_splat_ssbo.destroy();
    m_n = 0;
    m_shaders = nullptr;
    m_head = 0;
    m_tail = 0;
    m_count = 0;
    m_dropped = 0;
    m_applied_total = 0;
}

bool CraterSplatter::enqueue(CraterSplat splat) noexcept {
    splat = crater_sanitize(splat);
    if (m_count >= SPLAT_QUEUE_CAP) {
        ++m_dropped;
        return false;
    }

    m_queue[m_tail] = splat;
    m_tail = (m_tail + 1) % SPLAT_QUEUE_CAP;
    ++m_count;
    return true;
}

int CraterSplatter::flush(int max_per_frame) noexcept {
    if (!m_shaders || m_count == 0 || m_crater_tex.id() == 0) {
        return 0;
    }

    u32 prog = m_shaders->program(ProgramId::CraterSplat);
    if (prog == 0) return 0;

    glUseProgram(prog);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_SPLATS, m_splat_ssbo.id());
    glBindImageTexture(IMG_CRATER, m_crater_tex.id(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);

    GLint u_index_loc = glGetUniformLocation(prog, "u_index");
    GLint u_origin_loc = glGetUniformLocation(prog, "u_origin");
    GLint u_n_loc = glGetUniformLocation(prog, "u_n");
    GLint u_extent_loc = glGetUniformLocation(prog, "u_extent");

    int to_pop = std::min(max_per_frame, m_count);
    int applied_count = 0;

    for (int i = 0; i < to_pop; ++i) {
        CraterSplat splat = m_queue[m_head];
        m_head = (m_head + 1) % SPLAT_QUEUE_CAP;
        --m_count;

        TexelBox box = crater_box(splat, m_n);
        int box_w = box.x1 - box.x0 + 1;
        int box_h = box.y1 - box.y0 + 1;
        if (box_w <= 0 || box_h <= 0) continue;

        float extent = crater_extent_m(splat);

        glNamedBufferSubData(m_splat_ssbo.id(), 0, sizeof(CraterSplat), &splat);
        glUniform1ui(u_index_loc, 0);
        glUniform2i(u_origin_loc, box.x0, box.y0);
        glUniform1i(u_n_loc, m_n);
        glUniform1f(u_extent_loc, extent);

        glDispatchCompute((box_w + 7) / 8, (box_h + 7) / 8, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        ++m_applied_total;
        ++applied_count;
    }

    if (applied_count > 0) {
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
    }

    return applied_count;
}

void CraterSplatter::clear() noexcept {
    m_head = 0;
    m_tail = 0;
    m_count = 0;
    if (m_crater_tex.id() != 0) {
        const u8 clear_clr[4] = {128, 0, 0, 0};
        glClearTexImage(m_crater_tex.id(), 0, GL_RGBA, GL_UNSIGNED_BYTE, clear_clr);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }
}

u64 CraterSplatter::vram_bytes() const noexcept {
    return static_cast<u64>(m_n) * m_n * 4 + sizeof(CraterSplat) * 64;
}

} // namespace ron::render
