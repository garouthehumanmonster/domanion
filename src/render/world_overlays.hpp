#pragma once

#include "core/types.hpp"
#include "render/vram_ledger.hpp"
#include "render/gl_objects.hpp"
#include "render/shader_library.hpp"
#include "render/overlay_types.hpp"
#include "render/territory_sdf.hpp"
#include "render/crater_splatter.hpp"
#include "render/fog_map.hpp"
#include "render/fog_gpu.hpp"

namespace ron::render {

class WorldOverlays {
public:
    struct Config {
        int owner_n = 512;
        int crater_n = 2048;
        int fog_n = 512;
    };

    WorldOverlays() noexcept = default;
    ~WorldOverlays() noexcept { shutdown(); }

    WorldOverlays(const WorldOverlays&) = delete;
    WorldOverlays& operator=(const WorldOverlays&) = delete;

    bool init(VramLedger& ledger, const ShaderLibrary* shaders = nullptr) noexcept {
        return init(ledger, Config{}, shaders);
    }
    bool init(VramLedger& ledger, const Config& cfg, const ShaderLibrary* shaders = nullptr) noexcept;
    void shutdown() noexcept;

    TerritorySdf& territory() noexcept { return m_territory; }
    const TerritorySdf& territory() const noexcept { return m_territory; }

    CraterSplatter& craters() noexcept { return m_craters; }
    const CraterSplatter& craters() const noexcept { return m_craters; }

    FogGpu& fog_gpu() noexcept { return m_fog_gpu; }
    const FogGpu& fog_gpu() const noexcept { return m_fog_gpu; }

    FogMap& fog() noexcept { return m_fog_map; }
    const FogMap& fog() const noexcept { return m_fog_map; }

    OverlayUbo& ubo() noexcept { return m_ubo_data; }
    const OverlayUbo& ubo() const noexcept { return m_ubo_data; }

    const ShaderLibrary* shaders() const noexcept { return m_shaders; }
    void set_shaders(const ShaderLibrary* shaders) noexcept;

    void set_territory_owner(const u8* owner_data) noexcept;
    void set_flags(bool territory, bool fog, bool craters) noexcept;

    void update(float dt, float time_s) noexcept;
    void bind_world_pass() noexcept;

    u64 vram_bytes() const noexcept;

private:
    Config m_cfg{};
    const ShaderLibrary* m_shaders{nullptr};

    TerritorySdf m_territory;
    CraterSplatter m_craters;
    FogGpu m_fog_gpu;
    FogMap m_fog_map;

    OverlayUbo m_ubo_data{};
    GpuBuffer m_ubo_buffer;
    bool m_territory_dirty{false};
};

} // namespace ron::render
