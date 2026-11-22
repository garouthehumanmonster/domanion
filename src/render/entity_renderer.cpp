#include "render/entity_renderer.hpp"
#include "render/entity_math.hpp"
#include "render/gl_bindings.hpp"
#include "render/lighting_system.hpp"
#include "render/shader_preproc.hpp"
#include "core/assert_log.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

namespace ron::render {

bool EntityRenderer::init(VramLedger& ledger, EntityAssets& assets, ShaderLibrary* shaders) {
    shutdown();
    assets_ = &assets;

    if (shaders) {
        shaders_ = shaders;
    } else {
        static const char* paths[] = {
            RON_SOURCE_DIR "/assets/shaders",
            "assets/shaders",
            "../assets/shaders",
            "../../assets/shaders"
        };
        const char* dir = paths[0];
        for (const char* p : paths) {
            std::string test_file = std::string(p) + "/common/frame.glsl";
            FILE* f = fopen(test_file.c_str(), "rb");
            if (f) {
                fclose(f);
                dir = p;
                break;
            }
        }
        owned_shaders_ = std::make_unique<ShaderLibrary>();
        std::string log;
        if (!owned_shaders_->init(disk_file_reader, const_cast<char*>(dir), log)) {
            RON_LOG_ERROR("EntityRenderer: failed to load shader library:\n%s", log.c_str());
            return false;
        }
        shaders_ = owned_shaders_.get();
    }

    prog_cull_count_   = shaders_->program(ProgramId::EntityCullCount);
    prog_cull_scan_    = shaders_->program(ProgramId::EntityCullScan);
    prog_cull_scatter_ = shaders_->program(ProgramId::EntityCullScatter);
    prog_draw_         = shaders_->program(ProgramId::EntityDraw);
    prog_draw_ovl_     = shaders_->program(ProgramId::EntityDrawOvl);
    prog_draw_lit_     = shaders_->program(ProgramId::EntityLit);
    prog_draw_lit_ovl_ = shaders_->program(ProgramId::EntityLitOvl);

    if (!prog_cull_count_ || !prog_cull_scan_ || !prog_cull_scatter_ || !prog_draw_ || !prog_draw_ovl_) {
        RON_LOG_ERROR("EntityRenderer: missing required shader program");
        shutdown();
        return false;
    }

    // 1. Instance persistent ring: 3 slots x MAX_INSTANCES x 64 B
    GLint ssbo_align = 0;
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &ssbo_align);
    GLint ubo_align = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_align);

    u64 bytes_per_slot = static_cast<u64>(MAX_INSTANCES) * sizeof(EntityInstance);
    if (!ring_.create(ledger, VramCat::EntityBuffers, "entity_instance_ring",
                      bytes_per_slot, static_cast<u32>(ssbo_align), static_cast<u32>(ubo_align))) {
        RON_LOG_ERROR("EntityRenderer: failed to create instance ring");
        shutdown();
        return false;
    }

    // 2. Visible buffer: MAX_INSTANCES x 4 B
    u64 visible_bytes = static_cast<u64>(MAX_INSTANCES) * sizeof(u32);
    if (!visible_.create(ledger, VramCat::EntityBuffers, "entity_visible",
                         visible_bytes, nullptr, GL_DYNAMIC_STORAGE_BIT)) {
        RON_LOG_ERROR("EntityRenderer: failed to create visible buffer");
        shutdown();
        return false;
    }

    // 3. Ranks buffer: MAX_INSTANCES x 8 B (uvec2 per instance)
    u64 ranks_bytes = static_cast<u64>(MAX_INSTANCES) * sizeof(u32) * 2;
    if (!ranks_.create(ledger, VramCat::EntityBuffers, "entity_ranks",
                       ranks_bytes, nullptr, GL_DYNAMIC_STORAGE_BIT)) {
        RON_LOG_ERROR("EntityRenderer: failed to create ranks buffer");
        shutdown();
        return false;
    }

    // 4. Indirect buffer: MAX_BUCKETS x 20 B
    u64 indirect_bytes = static_cast<u64>(MAX_BUCKETS) * sizeof(DrawCmd);
    if (!indirect_.create(ledger, VramCat::EntityBuffers, "entity_indirect",
                          indirect_bytes, nullptr, GL_DYNAMIC_STORAGE_BIT)) {
        RON_LOG_ERROR("EntityRenderer: failed to create indirect buffer");
        shutdown();
        return false;
    }

    // 5. Counters buffer: (16 + MAX_BUCKETS) x 4 B
    u64 counters_bytes = static_cast<u64>(16 + MAX_BUCKETS) * sizeof(u32);
    if (!counters_.create(ledger, VramCat::EntityBuffers, "entity_counters",
                          counters_bytes, nullptr, GL_DYNAMIC_STORAGE_BIT)) {
        RON_LOG_ERROR("EntityRenderer: failed to create counters buffer");
        shutdown();
        return false;
    }

    // 6. EntityUbo: 160 B
    if (!ubo_.create(ledger, VramCat::EntityBuffers, "entity_ubo",
                     sizeof(EntityUbo), nullptr, GL_DYNAMIC_STORAGE_BIT)) {
        RON_LOG_ERROR("EntityRenderer: failed to create EntityUbo");
        shutdown();
        return false;
    }

    vram_bytes_ = ring_.slot_stride() * 3 + visible_.size() + ranks_.size() +
                  indirect_.size() + counters_.size() + ubo_.size();

    mapped_slot_ = ring_.begin_frame();
    feed_.clear();
    upload_count_ = 0;

    return true;
}

void EntityRenderer::shutdown() noexcept {
    ring_.destroy();
    visible_.destroy();
    ranks_.destroy();
    indirect_.destroy();
    counters_.destroy();
    ubo_.destroy();

    owned_shaders_.reset();
    shaders_ = nullptr;
    assets_ = nullptr;
    overlays_ = nullptr;
    mapped_slot_ = nullptr;
    upload_count_ = 0;
    vram_bytes_ = 0;
}

void EntityRenderer::begin_frame(int frame_index) {
    (void)frame_index;
    ring_.end_frame();
    mapped_slot_ = ring_.begin_frame();
    feed_.clear();
    upload_count_ = 0;
}

void EntityRenderer::upload() {
    u32 cnt = feed_.count();
    upload_count_ = cnt;
    if (cnt > 0 && mapped_slot_) {
        std::memcpy(mapped_slot_, feed_.data(), cnt * sizeof(EntityInstance));
    }
}

void EntityRenderer::cull(const FrameUniforms& frame, float alpha, float time_s, bool freeze) {
    if (!assets_) return;

    u32 model_count = static_cast<u32>(assets_->model_count());

    EntityUbo ubo;
    entity_fill_ubo(frame, alpha, upload_count_, model_count, time_s, &ubo);
    ubo.time_p[2] = shadow_margin_;
    glNamedBufferSubData(ubo_.id(), 0, sizeof(EntityUbo), &ubo);
    glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_ENTITY, ubo_.id());

    if (freeze) {
        return;
    }

    // Clear counters to zero
    u32 zero = 0;
    glClearNamedBufferData(counters_.id(), GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);

    // Bind SSBOs
    u64 inst_bytes = std::max(1u, upload_count_) * sizeof(EntityInstance);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_INSTANCES, ring_.buffer(), ring_.slot_offset(), inst_bytes);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_MODELS, assets_->model_ssbo());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_BUCKETS, ranks_.id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_COUNTERS, counters_.id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_INDIRECT, indirect_.id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_VISIBLE, visible_.id());

    // 1. Dispatch count pass
    if (upload_count_ > 0) {
        glUseProgram(prog_cull_count_);
        u32 groups = (upload_count_ + 63u) / 64u;
        glDispatchCompute(groups, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    // 2. Dispatch scan pass (1 workgroup)
    glUseProgram(prog_cull_scan_);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // 3. Dispatch scatter pass
    if (upload_count_ > 0) {
        glUseProgram(prog_cull_scatter_);
        u32 groups = (upload_count_ + 63u) / 64u;
        glDispatchCompute(groups, 1, 1);
    }

    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT);
}

void EntityRenderer::draw(const FrameUniforms& frame) {
    (void)frame;
    if (!assets_) return;

    // State setup:
    // depth test GL_GREATER, depth write ON, cull face BACK, front face per EntityAssets::front_face_ccw(), blending off
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(assets_->front_face_ccw() ? GL_CCW : GL_CW);
    glDisable(GL_BLEND);

    // Program selection
    u32 prog = prog_draw_;
    if (lighting_) {
        prog = overlays_ ? prog_draw_lit_ovl_ : prog_draw_lit_;
        lighting_->bind(0);
    } else if (overlays_) {
        prog = prog_draw_ovl_;
    }
    if (overlays_) {
        overlays_->bind_world_pass();
    }
    glUseProgram(prog);

    // Bind VAO
    glBindVertexArray(assets_->vao());

    // Bind textures:
    // TEX_VAT (14), TEX_TRIM_ALBEDO (15), TEX_TRIM_NORMAL (16), TEX_TRIM_ORM (17), TEX_TRIM_EMISSIVE (18)
    glBindTextureUnit(TEX_VAT, assets_->vat_tex());
    for (int i = 0; i < 4; ++i) {
        glBindTextureUnit(TEX_TRIM_ALBEDO + i, assets_->trim_tex(i));
    }

    // Bind SSBOs
    u64 inst_bytes = std::max(1u, upload_count_) * sizeof(EntityInstance);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_INSTANCES, ring_.buffer(), ring_.slot_offset(), inst_bytes);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_VISIBLE, visible_.id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_MODELS, assets_->model_ssbo());
    glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_ENTITY, ubo_.id());

    // Bind Draw indirect buffer
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect_.id());

    // ONE multi-draw-elements-indirect
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_SHORT, nullptr, draw_count(), sizeof(DrawCmd));

    glBindVertexArray(0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
}

EntityRenderer::EntityStats EntityRenderer::read_stats_blocking() {
    EntityStats stats{};
    u32 data[16];
    glGetNamedBufferSubData(counters_.id(), 0, sizeof(data), data);
    stats.input = data[0];
    stats.visible = data[1];
    stats.frustum_culled = data[2];
    stats.size_culled = data[3];
    stats.lod[0] = data[4];
    stats.lod[1] = data[5];
    stats.lod[2] = data[6];
    stats.invalid = data[7];
    stats.draws = draw_count();
    return stats;
}

int EntityRenderer::draw_count() const noexcept {
    return assets_ ? assets_->model_count() * 3 : 0;
}

u64 EntityRenderer::vram_bytes() const noexcept {
    return vram_bytes_;
}

void EntityRenderer::draw_shadow_cascade(int c, u32 shadow_prog, const GpuBuffer& lighting_ubo) {
    if (!assets_ || draw_count() == 0 || shadow_prog == 0) return;

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_GREATER); // Reverse-Z: near 1.0, far 0.0
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(assets_->front_face_ccw() ? GL_CCW : GL_CW);
    glDisable(GL_BLEND);

    glUseProgram(shadow_prog);
    glUniform1i(0, c); // location 0: u_cascade

    // Bind VAO & VAT
    glBindVertexArray(assets_->vao());
    glBindTextureUnit(TEX_VAT, assets_->vat_tex());

    // Bind SSBOs
    u64 inst_bytes = std::max(1u, upload_count_) * sizeof(EntityInstance);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_INSTANCES, ring_.buffer(), ring_.slot_offset(), inst_bytes);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_VISIBLE, visible_.id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_MODELS, assets_->model_ssbo());
    glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_ENTITY, ubo_.id());
    if (lighting_ubo.id() != 0) {
        glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_LIGHT, lighting_ubo.id());
    }

    // Bind Draw indirect buffer and draw
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect_.id());
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_SHORT, nullptr, draw_count(), sizeof(DrawCmd));

    glBindVertexArray(0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    glDisable(GL_CULL_FACE);
}

} // namespace ron::render
