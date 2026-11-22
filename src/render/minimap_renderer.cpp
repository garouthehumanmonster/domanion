#include "render/minimap_renderer.hpp"
#include "render/gl_bindings.hpp"
#include "render/gl_state.hpp"
#include <vector>
#include <algorithm>

namespace ron::render {

bool MinimapRenderer::init(VramLedger& ledger, WorldOverlays& overlays, const ShaderLibrary* shaders) noexcept {
    destroy();
    m_overlays = &overlays;
    m_shaders = shaders ? shaders : overlays.shaders();

    if (!m_base_tex.create_2d(ledger, VramCat::PostFx, "MinimapBase", TexFmt::RGBA8, MINIMAP_N, MINIMAP_N, 1)) {
        return false;
    }
    glTextureParameteri(m_base_tex.id(), GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_base_tex.id(), GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_base_tex.id(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_base_tex.id(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (!m_target_tex.create_2d(ledger, VramCat::PostFx, "MinimapTarget", TexFmt::RGBA8, MINIMAP_N, MINIMAP_N, 1)) {
        destroy();
        return false;
    }
    glTextureParameteri(m_target_tex.id(), GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_target_tex.id(), GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_target_tex.id(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_target_tex.id(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glCreateFramebuffers(1, &m_fbo);
    glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, m_target_tex.id(), 0);
    if (glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        destroy();
        return false;
    }

    if (!m_blip_ssbo.create(ledger, VramCat::TerrainMaps, "BlipSsbo", MAX_BLIPS * sizeof(Blip), nullptr, GL_DYNAMIC_STORAGE_BIT)) {
        destroy();
        return false;
    }

    MinimapUbo zero_ubo{};
    if (!m_ubo_buf.create(ledger, VramCat::PostFx, "MinimapUbo", sizeof(MinimapUbo), &zero_ubo, GL_DYNAMIC_STORAGE_BIT)) {
        destroy();
        return false;
    }

    return true;
}

void MinimapRenderer::destroy() noexcept {
    if (m_fbo != 0) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    m_base_tex.destroy();
    m_target_tex.destroy();
    m_blip_ssbo.destroy();
    m_ubo_buf.destroy();
    m_overlays = nullptr;
    m_shaders = nullptr;
}

void MinimapRenderer::bake_base(const HeightSampler& sampler) noexcept {
    if (m_base_tex.id() == 0) return;
    std::vector<u8> rgba(static_cast<size_t>(MINIMAP_N) * MINIMAP_N * 4);
    minimap_bake(sampler, rgba.data());
    glTextureSubImage2D(m_base_tex.id(), 0, 0, 0, MINIMAP_N, MINIMAP_N, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
}

void MinimapRenderer::render(const Scene& scene, float time_s) noexcept {
    if (!m_shaders || !m_overlays || m_fbo == 0) return;

    int count = std::clamp(scene.blip_count, 0, MAX_BLIPS);
    if (count > 0 && scene.blips != nullptr) {
        glNamedBufferSubData(m_blip_ssbo.id(), 0, static_cast<GLsizeiptr>(count * sizeof(Blip)), scene.blips);
    }

    MinimapUbo ubo{};
    for (int i = 0; i < 8; ++i) {
        ubo.quad[i] = scene.quad_xz[i];
    }
    ubo.fog_on = scene.fog_on ? 1.0f : 0.0f;
    ubo.blip_count = static_cast<float>(count);
    ubo.time_s = time_s;
    ubo.line_px = 1.0f;
    glNamedBufferSubData(m_ubo_buf.id(), 0, sizeof(MinimapUbo), &ubo);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, MINIMAP_N, MINIMAP_N);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // Composite pass: territory, fog, camera quad outline
    glUseProgram(m_shaders->program(ProgramId::MinimapComposite));
    glBindTextureUnit(TEX_MINIMAP_BASE, m_base_tex.id());
    m_overlays->bind_world_pass();
    glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_MINIMAP, m_ubo_buf.id());
    draw_fullscreen_triangle();

    // Blip pass: instanced blip quads
    if (count > 0) {
        glUseProgram(m_shaders->program(ProgramId::MinimapBlip));
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_BLIPS, m_blip_ssbo.id());
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, count);
        glDisable(GL_BLEND);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
}

void MinimapRenderer::blit(const PxRect& rect, int window_w, int window_h) noexcept {
    (void)window_w;
    if (!m_shaders || m_target_tex.id() == 0) return;

    GLint gl_x = static_cast<GLint>(rect.x);
    GLint gl_y = static_cast<GLint>(window_h - (rect.y + rect.h));
    GLsizei gl_w = static_cast<GLsizei>(rect.w);
    GLsizei gl_h = static_cast<GLsizei>(rect.h);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    // 2 px dark border
    glEnable(GL_SCISSOR_TEST);
    glScissor(gl_x, gl_y, gl_w, gl_h);
    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Interior 2 px inside
    glViewport(gl_x + 2, gl_y + 2, gl_w - 4, gl_h - 4);
    glScissor(gl_x + 2, gl_y + 2, gl_w - 4, gl_h - 4);

    glUseProgram(m_shaders->program(ProgramId::MinimapBlit));
    glBindTextureUnit(TEX_MINIMAP_BASE, m_target_tex.id());
    draw_fullscreen_triangle();

    glDisable(GL_SCISSOR_TEST);
}

u64 MinimapRenderer::vram_bytes() const noexcept {
    return static_cast<u64>(MINIMAP_N) * MINIMAP_N * 4ULL * 2ULL +
           MAX_BLIPS * sizeof(Blip) +
           sizeof(MinimapUbo);
}

} // namespace ron::render
