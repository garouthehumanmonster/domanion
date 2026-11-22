#include "render/marks_renderer.hpp"
#include "render/icon_map.hpp"
#include "core/assert_log.hpp"
#include <glad/gl.h>
#include <algorithm>

namespace ron::render {

bool MarksRenderer::init(
    VramLedger& ledger,
    ShaderLibrary& shaders,
    const UiSource& ui_source,
    const EntitySource& entity_source) noexcept
{
    destroy();
    (void)shaders;
    ui_source_ = &ui_source;

    // 1. Upload atlas to ui_atlas_
    if (!ui_atlas_.init(ledger, ui_source)) {
        RON_LOG_ERROR("MarksRenderer::init: failed to init UiAtlas");
        destroy();
        return false;
    }

    // 2. icons_ssbo_: MAX_INSTANCES * sizeof(IconGpu) (2 MiB) in VramCat::EntityBuffers
    u64 icons_bytes = static_cast<u64>(MAX_INSTANCES) * sizeof(IconGpu);
    if (!icons_ssbo_.create(ledger, VramCat::EntityBuffers, "marks_icons_ssbo",
                            icons_bytes, nullptr, GL_DYNAMIC_STORAGE_BIT)) {
        RON_LOG_ERROR("MarksRenderer::init: failed to create icons_ssbo");
        destroy();
        return false;
    }

    // 3. bars_ssbo_: MAX_INSTANCES * sizeof(BarGpu) (2 MiB) in VramCat::EntityBuffers
    u64 bars_bytes = static_cast<u64>(MAX_INSTANCES) * sizeof(BarGpu);
    if (!bars_ssbo_.create(ledger, VramCat::EntityBuffers, "marks_bars_ssbo",
                           bars_bytes, nullptr, GL_DYNAMIC_STORAGE_BIT)) {
        RON_LOG_ERROR("MarksRenderer::init: failed to create bars_ssbo");
        destroy();
        return false;
    }

    // 4. cmds_ssbo_: 2 * sizeof(DrawArraysIndirectCommand) (32 B) in VramCat::EntityBuffers
    DrawArraysIndirectCommand default_cmds[2] = {
        { 6, 0, 0, 0 }, // Icon quad: 6 vertices, 0 instances
        { 6, 0, 0, 0 }  // Bar quad: 6 vertices, 0 instances
    };
    if (!cmds_ssbo_.create(ledger, VramCat::EntityBuffers, "marks_cmds_ssbo",
                           sizeof(default_cmds), default_cmds, GL_DYNAMIC_STORAGE_BIT)) {
        RON_LOG_ERROR("MarksRenderer::init: failed to create cmds_ssbo");
        destroy();
        return false;
    }

    // 5. model_marks_ssbo_: MAX_MODELS * sizeof(ModelMarkGpu) (1024 B) in VramCat::EntityBuffers
    ModelMark cpu_marks[MAX_MODELS]{};
    for (u32 i = 0; i < MAX_MODELS; ++i) {
        cpu_marks[i].icon = 0xFFFFu;
        cpu_marks[i].shape = 0;
    }
    u32 m_count = std::min(entity_source.model_count(), MAX_MODELS);
    build_model_marks(entity_source.models_cpu().data(), m_count, cpu_marks);

    ModelMarkGpu gpu_marks[MAX_MODELS]{};
    for (u32 i = 0; i < MAX_MODELS; ++i) {
        gpu_marks[i].icon = static_cast<u32>(cpu_marks[i].icon);
        gpu_marks[i].shape = static_cast<u32>(cpu_marks[i].shape);
    }
    if (!model_marks_ssbo_.create(ledger, VramCat::EntityBuffers, "model_marks_ssbo",
                                  sizeof(gpu_marks), gpu_marks, 0)) {
        RON_LOG_ERROR("MarksRenderer::init: failed to create model_marks_ssbo");
        destroy();
        return false;
    }

    // 6. marks_ubo_: sizeof(MarksUbo) (832 B) in VramCat::EntityBuffers
    if (!marks_ubo_.create(ledger, VramCat::EntityBuffers, "marks_ubo",
                           sizeof(MarksUbo), nullptr, GL_DYNAMIC_STORAGE_BIT)) {
        RON_LOG_ERROR("MarksRenderer::init: failed to create marks_ubo");
        destroy();
        return false;
    }

    return true;
}

void MarksRenderer::update_ubo(const FrameUniforms& fu, float ui_scale) noexcept {
    // 1. icon_p: {20.0f, 8.0f, 28.0f, ui_scale}
    cpu_ubo_.icon_p[0] = 20.0f;
    cpu_ubo_.icon_p[1] = 8.0f;
    cpu_ubo_.icon_p[2] = 28.0f;
    cpu_ubo_.icon_p[3] = ui_scale;

    // 2. bar_p: {14.0f, 64.0f, 4.0f, 3.0f}
    cpu_ubo_.bar_p[0] = 14.0f;
    cpu_ubo_.bar_p[1] = 64.0f;
    cpu_ubo_.bar_p[2] = 4.0f;
    cpu_ubo_.bar_p[3] = 3.0f;

    // 3. screen: {w, h, 1/w, 1/h}
    float sw = (fu.screen[0] > 0.0f) ? fu.screen[0] : 640.0f;
    float sh = (fu.screen[1] > 0.0f) ? fu.screen[1] : 360.0f;
    cpu_ubo_.screen[0] = sw;
    cpu_ubo_.screen[1] = sh;
    cpu_ubo_.screen[2] = 1.0f / sw;
    cpu_ubo_.screen[3] = 1.0f / sh;

    // 4. atlas: {1024, 512, 4, 0}
    cpu_ubo_.atlas[0] = 1024.0f;
    cpu_ubo_.atlas[1] = 512.0f;
    cpu_ubo_.atlas[2] = 4.0f;
    cpu_ubo_.atlas[3] = 0.0f;

    // 5. owner_color: 16 colors from overlay_palette
    for (int i = 0; i < 16; ++i) {
        Vec4 c = overlay_palette(i);
        cpu_ubo_.owner_color[i][0] = c.x;
        cpu_ubo_.owner_color[i][1] = c.y;
        cpu_ubo_.owner_color[i][2] = c.z;
        cpu_ubo_.owner_color[i][3] = 1.0f;
    }

    // 6. icon_uv: 32 UV rects
    if (ui_source_) {
        for (u32 i = 0; i < 32; ++i) {
            const auto& uv = ui_source_->icon_uv(i);
            cpu_ubo_.icon_uv[i][0] = uv.u0;
            cpu_ubo_.icon_uv[i][1] = uv.v0;
            cpu_ubo_.icon_uv[i][2] = uv.u1;
            cpu_ubo_.icon_uv[i][3] = uv.v1;
        }
    }

    // Upload to marks_ubo_
    if (marks_ubo_.id() != 0) {
        glNamedBufferSubData(marks_ubo_.id(), 0, sizeof(MarksUbo), &cpu_ubo_);
    }
}

void MarksRenderer::bind() const noexcept {
    if (icons_ssbo_.id() != 0) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_ICONS, icons_ssbo_.id());
    }
    if (bars_ssbo_.id() != 0) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_BARS, bars_ssbo_.id());
    }
    if (cmds_ssbo_.id() != 0) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_MARK_CMDS, cmds_ssbo_.id());
    }
    if (model_marks_ssbo_.id() != 0) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_MODEL_MARKS, model_marks_ssbo_.id());
    }
    if (marks_ubo_.id() != 0) {
        glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_MARKS, marks_ubo_.id());
    }
    ui_atlas_.bind(TEX_MSDF);
}

void MarksRenderer::destroy() noexcept {
    icons_ssbo_.destroy();
    bars_ssbo_.destroy();
    cmds_ssbo_.destroy();
    model_marks_ssbo_.destroy();
    marks_ubo_.destroy();
    ui_atlas_.destroy();
}

} // namespace ron::render
