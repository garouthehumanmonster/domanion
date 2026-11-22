#pragma once

#include "core/types.hpp"
#include "render/vram_ledger.hpp"
#include "render/gl_objects.hpp"
#include "render/persistent_ring.hpp"
#include "render/shader_library.hpp"
#include "render/entity_types.hpp"
#include "render/entity_feed.hpp"
#include "render/entity_assets.hpp"
#include "render/frame_uniforms.hpp"
#include "render/world_overlays.hpp"
#include <glad/gl.h>

namespace ron::render {

class LightingSystem;

class EntityRenderer {
public:
    struct EntityStats {
        u32 input;
        u32 visible;
        u32 frustum_culled;
        u32 size_culled;
        u32 lod[3];
        u32 invalid;
        int draws;
    };

    EntityRenderer() noexcept = default;
    ~EntityRenderer() noexcept { shutdown(); }

    EntityRenderer(const EntityRenderer&) = delete;
    EntityRenderer& operator=(const EntityRenderer&) = delete;

    bool init(VramLedger& ledger, EntityAssets& assets, ShaderLibrary* shaders = nullptr);
    void shutdown() noexcept;

    EntityFeed& feed() noexcept { return feed_; }

    void begin_frame(int frame_index);
    void upload();
    void cull(const FrameUniforms& frame, float alpha, float time_s, bool freeze = false);
    void draw(const FrameUniforms& frame);

    void set_overlays(WorldOverlays* ovl) noexcept { overlays_ = ovl; }
    WorldOverlays* overlays() const noexcept { return overlays_; }

    void set_lighting(const LightingSystem* lighting) noexcept { lighting_ = lighting; }
    const LightingSystem* lighting() const noexcept { return lighting_; }

    void set_frustum_margin(float margin) noexcept { shadow_margin_ = margin; }
    float frustum_margin() const noexcept { return shadow_margin_; }
    void set_shadow_margin(float margin) noexcept { set_frustum_margin(margin); }

    EntityStats read_stats_blocking();

    GLuint indirect_buffer() const noexcept { return indirect_.id(); }
    GLuint visible_buffer() const noexcept { return visible_.id(); }
    GLuint counters_buffer() const noexcept { return counters_.id(); }
    GLuint rank_buffer() const noexcept { return ranks_.id(); }
    GLuint ubo_buffer() const noexcept { return ubo_.id(); }
    const PersistentRing& ring() const noexcept { return ring_; }
    u32 upload_count() const noexcept { return upload_count_; }

    int draw_count() const noexcept;
    u64 vram_bytes() const noexcept;

    void draw_shadow_cascade(int c, u32 shadow_prog, const GpuBuffer& lighting_ubo);

private:
    float shadow_margin_{0.0f};
    const LightingSystem* lighting_{nullptr};
    EntityAssets* assets_{nullptr};
    ShaderLibrary* shaders_{nullptr};
    std::unique_ptr<ShaderLibrary> owned_shaders_;
    WorldOverlays* overlays_{nullptr};

    EntityFeed feed_;
    PersistentRing ring_;
    u8* mapped_slot_{nullptr};
    u32 upload_count_{0};

    GpuBuffer visible_;
    GpuBuffer ranks_;
    GpuBuffer indirect_;
    GpuBuffer counters_;
    GpuBuffer ubo_;

    u32 prog_cull_count_{0};
    u32 prog_cull_scan_{0};
    u32 prog_cull_scatter_{0};
    u32 prog_draw_{0};
    u32 prog_draw_ovl_{0};
    u32 prog_draw_lit_{0};
    u32 prog_draw_lit_ovl_{0};

    u64 vram_bytes_{0};
};

} // namespace ron::render
