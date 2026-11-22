#pragma once

#include "core/types.hpp"
#include "render/shader_program.hpp"
#include <string>

namespace ron::render {

enum class ProgramId : u32 {
    Tonemap = 0,
    TonemapPassthrough,
    SelfTestRing,
    SelfTestFrame,
    DepthProbe,
    Pattern,
    Terrain,
    TerrainDebugFlat,
    TerrainDebugLod,
    Ocean,
    JfaSeed,
    JfaStep,
    JfaFinal,
    CraterSplat,
    FogSmooth,
    TerrainOvl,
    OceanOvl,
    MinimapComposite,
    MinimapBlip,
    MinimapBlit,
    EntityCullCount,
    EntityCullScan,
    EntityCullScatter,
    EntityDraw,
    EntityDrawOvl,
    ShadowEntity,
    TerrainLit,
    TerrainLitOvl,
    EntityLit,
    EntityLitOvl,
    MarksCull,
    IconDraw,
    BarDraw,
    ShadowSelfTest,
    PbrSelfTest,
    COUNT
};

class ShaderLibrary {
public:
    ShaderLibrary() = default;
    ~ShaderLibrary() { destroy(); }

    bool init(FileReader reader, void* user, std::string& log);
    bool reload_all(FileReader reader, void* user, std::string& log);
    u32 program(ProgramId id) const;
    void destroy();

private:
    ShaderProgram m_programs[static_cast<size_t>(ProgramId::COUNT)];
};

} // namespace ron::render
