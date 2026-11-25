#pragma once

#include "core/types.hpp"
#include "render/gl_objects.hpp"
#include "render/gl_bindings.hpp"
#include "render/light_types.hpp"
#include "render/cascade_math.hpp"
#include "render/camera_rts.hpp"
#include "render/frame_uniforms.hpp"
#include "render/terrain_source.hpp"
#include "render/terrain_renderer.hpp"
#include "render/shader_library.hpp"

namespace ron::render {

class EntityRenderer;

class LightingSystem {
public:
    LightingSystem() noexcept = default;
    ~LightingSystem() noexcept { destroy(); }

    LightingSystem(const LightingSystem&) = delete;
    LightingSystem& operator=(const LightingSystem&) = delete;
    LightingSystem(LightingSystem&&) = delete;
    LightingSystem& operator=(LightingSystem&&) = delete;

    bool init(VramLedger& ledger, ShaderLibrary& shaders) noexcept;
    void update(const FrameUniforms& fu, const CameraRts& cam, u32 flags = LIGHT_FLAG_CSM | LIGHT_FLAG_TERRAIN_SHADOW) noexcept;
    void render_shadows(EntityRenderer& entity_renderer, const ShaderLibrary& shaders) noexcept;

    void bind(u32 height_tex_id = 0) const noexcept;
    void bind(const TerrainSource& terrain_source) const noexcept;
    void bind(const TerrainRenderer& terrain) const noexcept;

    void destroy() noexcept;
    void destroy(VramLedger&) noexcept { destroy(); }

    u32 shadow_map_id() const noexcept { return shadow_map_.id(); }
    const GpuTexture& shadow_map() const noexcept { return shadow_map_; }
    u32 shadow_fbo(u32 cascade) const noexcept { return (cascade < 4) ? shadow_fbo_[cascade] : 0; }
    u32 height_linear_sampler() const noexcept { return height_linear_sampler_; }
    u32 lighting_ubo_id() const noexcept { return lighting_ubo_.id(); }
    const GpuBuffer& lighting_ubo() const noexcept { return lighting_ubo_; }
    const CascadeSet& cascades() const noexcept { return cascades_; }
    const LightingUbo& cpu_ubo() const noexcept { return cpu_ubo_; }

private:
    GpuTexture shadow_map_;
    u32 shadow_fbo_[4]{0, 0, 0, 0};
    u32 height_linear_sampler_{0};
    GpuBuffer lighting_ubo_;

    CascadeSet cascades_{};
    LightingUbo cpu_ubo_{};
};

} // namespace ron::render
