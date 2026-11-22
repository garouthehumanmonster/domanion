#pragma once

#include "core/types.hpp"
#include "render/gl_objects.hpp"
#include "render/gl_bindings.hpp"
#include "render/light_types.hpp"
#include "render/marks_math.hpp"
#include "render/ui_atlas.hpp"
#include "render/ui_source.hpp"
#include "render/entity_source.hpp"
#include "render/entity_types.hpp"
#include "render/shader_library.hpp"
#include "render/overlay_types.hpp"
#include <type_traits>

namespace ron::render {

#pragma pack(push, 1)
struct DrawArraysIndirectCommand {
    u32 count;
    u32 instance_count;
    u32 first;
    u32 base_instance;
};
#pragma pack(pop)
static_assert(sizeof(DrawArraysIndirectCommand) == 16);
static_assert(std::is_trivially_copyable_v<DrawArraysIndirectCommand>);

struct alignas(16) IconGpu {
    float rect[4]; // screen x, y, w, h
    u32   icon_id; // 0..31
    u32   owner;   // 0..15
    float alpha;   // 0..1
    u32   shape;   // 0..3
};
static_assert(sizeof(IconGpu) == 32);
static_assert(alignof(IconGpu) == 16);
static_assert(std::is_trivially_copyable_v<IconGpu>);

struct alignas(16) BarGpu {
    float rect[4];  // screen x0, y0, w, h
    float color[3]; // rgb
    float hp;       // 0..1
};
static_assert(sizeof(BarGpu) == 32);
static_assert(alignof(BarGpu) == 16);
static_assert(std::is_trivially_copyable_v<BarGpu>);

struct ModelMarkGpu {
    u32 icon;  // 0..31 or 0xFFFFu
    u32 shape; // 0..3
};
static_assert(sizeof(ModelMarkGpu) == 8);
static_assert(std::is_trivially_copyable_v<ModelMarkGpu>);

class MarksRenderer {
public:
    MarksRenderer() noexcept = default;
    ~MarksRenderer() noexcept { destroy(); }

    MarksRenderer(const MarksRenderer&) = delete;
    MarksRenderer& operator=(const MarksRenderer&) = delete;
    MarksRenderer(MarksRenderer&&) noexcept = default;
    MarksRenderer& operator=(MarksRenderer&&) noexcept = default;

    bool init(VramLedger& ledger,
              ShaderLibrary& shaders,
              const UiSource& ui_source,
              const EntitySource& entity_source) noexcept;

    void update_ubo(const FrameUniforms& fu, float ui_scale = 1.0f) noexcept;
    void bind() const noexcept;
    void destroy() noexcept;
    void destroy(VramLedger&) noexcept { destroy(); }

    const GpuBuffer& icons_ssbo() const noexcept { return icons_ssbo_; }
    const GpuBuffer& bars_ssbo() const noexcept { return bars_ssbo_; }
    const GpuBuffer& cmds_ssbo() const noexcept { return cmds_ssbo_; }
    const GpuBuffer& model_marks_ssbo() const noexcept { return model_marks_ssbo_; }
    const GpuBuffer& marks_ubo() const noexcept { return marks_ubo_; }
    const UiAtlas& ui_atlas() const noexcept { return ui_atlas_; }
    const MarksUbo& cpu_ubo() const noexcept { return cpu_ubo_; }

private:
    GpuBuffer icons_ssbo_;
    GpuBuffer bars_ssbo_;
    GpuBuffer cmds_ssbo_;
    GpuBuffer model_marks_ssbo_;
    GpuBuffer marks_ubo_;
    UiAtlas ui_atlas_;

    MarksUbo cpu_ubo_{};
    const UiSource* ui_source_{nullptr};
};

} // namespace ron::render
