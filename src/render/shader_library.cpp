#include "render/shader_library.hpp"

namespace ron::render {

static const ShaderDesc K_DESCS[static_cast<size_t>(ProgramId::COUNT)] = {
    // Tonemap
    ShaderDesc{
        "Tonemap",
        "fullscreen.vert",
        "tonemap.frag",
        nullptr,
        {},
        0
    },
    // TonemapPassthrough
    ShaderDesc{
        "TonemapPassthrough",
        "fullscreen.vert",
        "tonemap.frag",
        nullptr,
        { "TONEMAP_PASSTHROUGH" },
        1
    },
    // SelfTestRing
    ShaderDesc{
        "SelfTestRing",
        nullptr,
        nullptr,
        "selftest_ring.comp",
        {},
        0
    },
    // SelfTestFrame
    ShaderDesc{
        "SelfTestFrame",
        nullptr,
        nullptr,
        "selftest_frame.comp",
        {},
        0
    },
    // DepthProbe
    ShaderDesc{
        "DepthProbe",
        "depth_probe.vert",
        "depth_probe.frag",
        nullptr,
        {},
        0
    },
    // Pattern
    ShaderDesc{
        "Pattern",
        "fullscreen.vert",
        "pattern.frag",
        nullptr,
        {},
        0
    },
    // Terrain
    ShaderDesc{
        "Terrain",
        "terrain.vert",
        "terrain.frag",
        nullptr,
        {},
        0
    },
    // TerrainDebugFlat
    ShaderDesc{
        "TerrainDebugFlat",
        "terrain.vert",
        "terrain.frag",
        nullptr,
        { "TERRAIN_DEBUG_FLAT" },
        1
    },
    // TerrainDebugLod
    ShaderDesc{
        "TerrainDebugLod",
        "terrain.vert",
        "terrain.frag",
        nullptr,
        { "TERRAIN_DEBUG_LOD" },
        1
    },
    // Ocean
    ShaderDesc{
        "Ocean",
        "ocean.vert",
        "ocean.frag",
        nullptr,
        {},
        0
    },
    // JfaSeed
    ShaderDesc{
        "JfaSeed",
        nullptr,
        nullptr,
        "jfa_seed.comp",
        {},
        0
    },
    // JfaStep
    ShaderDesc{
        "JfaStep",
        nullptr,
        nullptr,
        "jfa_step.comp",
        {},
        0
    },
    // JfaFinal
    ShaderDesc{
        "JfaFinal",
        nullptr,
        nullptr,
        "jfa_final.comp",
        {},
        0
    },
    // CraterSplat
    ShaderDesc{
        "CraterSplat",
        nullptr,
        nullptr,
        "crater_splat.comp",
        {},
        0
    },
    // FogSmooth
    ShaderDesc{
        "FogSmooth",
        nullptr,
        nullptr,
        "fog_smooth.comp",
        {},
        0
    },
    // TerrainOvl
    ShaderDesc{
        "TerrainOvl",
        "terrain.vert",
        "terrain.frag",
        nullptr,
        { "RON_OVERLAYS=1" },
        1
    },
    // OceanOvl
    ShaderDesc{
        "OceanOvl",
        "ocean.vert",
        "ocean.frag",
        nullptr,
        { "RON_OVERLAYS=1" },
        1
    },
    // MinimapComposite
    ShaderDesc{
        "MinimapComposite",
        "fullscreen.vert",
        "minimap_comp.frag",
        nullptr,
        { "RON_OVERLAYS=1" },
        1
    },
    // MinimapBlip
    ShaderDesc{
        "MinimapBlip",
        "minimap_blip.vert",
        "minimap_blip.frag",
        nullptr,
        {},
        0
    },
    // MinimapBlit
    ShaderDesc{
        "MinimapBlit",
        "fullscreen.vert",
        "minimap_blit.frag",
        nullptr,
        {},
        0
    },
    // EntityCullCount
    ShaderDesc{
        "EntityCullCount",
        nullptr,
        nullptr,
        "entity_cull_count.comp",
        {},
        0
    },
    // EntityCullScan
    ShaderDesc{
        "EntityCullScan",
        nullptr,
        nullptr,
        "entity_cull_scan.comp",
        {},
        0
    },
    // EntityCullScatter
    ShaderDesc{
        "EntityCullScatter",
        nullptr,
        nullptr,
        "entity_cull_scatter.comp",
        {},
        0
    },
    // EntityDraw
    ShaderDesc{
        "EntityDraw",
        "entity_draw.vert",
        "entity_draw.frag",
        nullptr,
        {},
        0
    },
    // EntityDrawOvl
    ShaderDesc{
        "EntityDrawOvl",
        "entity_draw.vert",
        "entity_draw.frag",
        nullptr,
        { "RON_OVERLAYS=1" },
        1
    },
    // ShadowEntity
    ShaderDesc{
        "ShadowEntity",
        "shadow_entity.vert",
        "empty.frag",
        nullptr,
        {},
        0
    },
    // TerrainLit
    ShaderDesc{
        "TerrainLit",
        "terrain.vert",
        "terrain.frag",
        nullptr,
        { "RON_LIT=1" },
        1
    },
    // TerrainLitOvl
    ShaderDesc{
        "TerrainLitOvl",
        "terrain.vert",
        "terrain.frag",
        nullptr,
        { "RON_LIT=1", "RON_OVERLAYS=1" },
        2
    },
    // EntityLit
    ShaderDesc{
        "EntityLit",
        "entity_draw.vert",
        "entity_draw.frag",
        nullptr,
        { "RON_LIT=1" },
        1
    },
    // EntityLitOvl
    ShaderDesc{
        "EntityLitOvl",
        "entity_draw.vert",
        "entity_draw.frag",
        nullptr,
        { "RON_LIT=1", "RON_OVERLAYS=1" },
        2
    },
    // MarksCull
    ShaderDesc{
        "MarksCull",
        nullptr,
        nullptr,
        "entity_cull_count.comp",
        {},
        0
    },
    // IconDraw
    ShaderDesc{
        "IconDraw",
        "fullscreen.vert",
        "tonemap.frag",
        nullptr,
        {},
        0
    },
    // BarDraw
    ShaderDesc{
        "BarDraw",
        "fullscreen.vert",
        "tonemap.frag",
        nullptr,
        {},
        0
    },
    // ShadowSelfTest
    ShaderDesc{
        "ShadowSelfTest",
        nullptr,
        nullptr,
        "shadow_selftest.comp",
        {},
        0
    },
    // PbrSelfTest
    ShaderDesc{
        "PbrSelfTest",
        nullptr,
        nullptr,
        "pbr_selftest.comp",
        {},
        0
    }
};

bool ShaderLibrary::init(FileReader reader, void* user, std::string& log) {
    destroy();
    for (size_t i = 0; i < static_cast<size_t>(ProgramId::COUNT); ++i) {
        if (!shader_build(K_DESCS[i], reader, user, m_programs[i], log)) {
            destroy();
            return false;
        }
    }
    return true;
}

bool ShaderLibrary::reload_all(FileReader reader, void* user, std::string& log) {
    bool ok = true;
    for (size_t i = 0; i < static_cast<size_t>(ProgramId::COUNT); ++i) {
        if (!shader_build(K_DESCS[i], reader, user, m_programs[i], log)) {
            ok = false;
        }
    }
    return ok;
}

u32 ShaderLibrary::program(ProgramId id) const {
    size_t idx = static_cast<size_t>(id);
    if (idx < static_cast<size_t>(ProgramId::COUNT)) {
        return m_programs[idx].id();
    }
    return 0;
}

void ShaderLibrary::destroy() {
    for (size_t i = 0; i < static_cast<size_t>(ProgramId::COUNT); ++i) {
        m_programs[i].destroy();
    }
}

} // namespace ron::render
