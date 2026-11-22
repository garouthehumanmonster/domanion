#include "render/world_overlays.hpp"
#include "render/gl_bindings.hpp"
#include <vector>

namespace ron::render {

bool WorldOverlays::init(VramLedger& ledger, const Config& cfg, const ShaderLibrary* shaders) noexcept {
    shutdown();
    m_cfg = cfg;
    m_shaders = shaders;

    if (!m_territory.init(ledger, cfg.owner_n, shaders)) {
        return false;
    }
    if (!m_craters.init(ledger, cfg.crater_n, shaders)) {
        m_territory.destroy();
        return false;
    }
    if (!m_fog_gpu.init(ledger, cfg.fog_n, shaders)) {
        m_territory.destroy();
        m_craters.destroy();
        return false;
    }

    std::vector<u8> zero_owner(cfg.owner_n * cfg.owner_n, 0);
    m_territory.upload_owner(zero_owner.data());
    m_territory_dirty = true;

    m_fog_map.reset();
    m_craters.clear();

    m_ubo_data = OverlayUbo{};
    for (int i = 0; i < 16; ++i) {
        Vec4 c = overlay_palette(static_cast<u8>(i));
        m_ubo_data.owner_color[i][0] = c.x;
        m_ubo_data.owner_color[i][1] = c.y;
        m_ubo_data.owner_color[i][2] = c.z;
        m_ubo_data.owner_color[i][3] = c.w;
    }
    m_ubo_data.territory_on = 0.0f;
    m_ubo_data.fill_alpha = 0.12f;
    m_ubo_data.line_w0_m = 2.0f;
    m_ubo_data.line_w1_m = 5.0f;

    m_ubo_data.fog_on = 0.0f;
    m_ubo_data.fog_unexplored = 0.0f;
    m_ubo_data.fog_explored_dim = 0.45f;
    m_ubo_data.crater_on = 0.0f;

    m_ubo_data.world_size = 4096.0f;
    m_ubo_data.inv_world = 1.0f / 4096.0f;
    m_ubo_data.time_s = 0.0f;

    if (!m_ubo_buffer.create(ledger, VramCat::TerrainMaps, "OverlayUbo",
                            sizeof(OverlayUbo), &m_ubo_data, GL_DYNAMIC_STORAGE_BIT)) {
        m_territory.destroy();
        m_craters.destroy();
        m_fog_gpu.destroy();
        return false;
    }

    return true;
}

void WorldOverlays::shutdown() noexcept {
    m_territory.destroy();
    m_craters.destroy();
    m_fog_gpu.destroy();
    m_ubo_buffer.destroy();
    m_shaders = nullptr;
    m_territory_dirty = false;
}

void WorldOverlays::set_shaders(const ShaderLibrary* shaders) noexcept {
    m_shaders = shaders;
    m_territory.set_shaders(shaders);
    m_craters.set_shaders(shaders);
    m_fog_gpu.set_shaders(shaders);
}

void WorldOverlays::set_territory_owner(const u8* owner_data) noexcept {
    m_territory.upload_owner(owner_data);
    m_territory_dirty = true;
}

void WorldOverlays::set_flags(bool territory, bool fog, bool craters) noexcept {
    m_ubo_data.territory_on = territory ? 1.0f : 0.0f;
    m_ubo_data.fog_on = fog ? 1.0f : 0.0f;
    m_ubo_data.crater_on = craters ? 1.0f : 0.0f;
}

void WorldOverlays::update(float dt, float time_s) noexcept {
    if (m_territory_dirty) {
        m_territory.rebuild();
        m_territory_dirty = false;
    }

    m_craters.flush(64);

    if (m_fog_map.consume_dirty()) {
        m_fog_gpu.upload(m_fog_map);
    }
    m_fog_gpu.step(dt);

    m_ubo_data.world_size = 4096.0f;
    m_ubo_data.inv_world = 1.0f / 4096.0f;
    m_ubo_data.time_s = time_s;

    if (m_ubo_buffer.id() != 0) {
        glNamedBufferSubData(m_ubo_buffer.id(), 0, sizeof(OverlayUbo), &m_ubo_data);
    }

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_UNIFORM_BARRIER_BIT |
                    GL_BUFFER_UPDATE_BARRIER_BIT);
}

void WorldOverlays::bind_world_pass() noexcept {
    glBindTextureUnit(TEX_TERRITORY_SDF, m_territory.sdf_tex());
    glBindTextureUnit(TEX_TERRITORY_OWNER, m_territory.owner_tex());
    glBindTextureUnit(TEX_CRATER, m_craters.texture());
    glBindTextureUnit(TEX_FOG, m_fog_gpu.smooth_tex());
    glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_OVERLAY, m_ubo_buffer.id());
}

u64 WorldOverlays::vram_bytes() const noexcept {
    return m_territory.vram_bytes() + m_craters.vram_bytes() + m_fog_gpu.vram_bytes() + m_ubo_buffer.size();
}

} // namespace ron::render
