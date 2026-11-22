#pragma once

#include "core/types.hpp"
#include "render/vram_ledger.hpp"
#include "render/gl_objects.hpp"
#include "render/entity_types.hpp"
#include "render/entity_math.hpp"
#include "render/entity_source.hpp"
#include <vector>
#include <glad/gl.h>

namespace ron::render {

class EntityAssets {
public:
    EntityAssets() noexcept = default;
    ~EntityAssets() noexcept { shutdown(); }

    EntityAssets(const EntityAssets&) = delete;
    EntityAssets& operator=(const EntityAssets&) = delete;

    bool init(VramLedger& ledger,
              const char* models_path = nullptr,
              const char* textures_path = nullptr);
    void shutdown() noexcept;

    int model_count() const noexcept { return static_cast<int>(src_.model_count()); }
    const ModelCpu& model(int idx) const noexcept { return src_.model_cpu(static_cast<u32>(idx)); }
    const ModelCpu* models() const noexcept { return src_.models_cpu().data(); }

    GLuint vao() const noexcept { return vao_; }
    GLuint vbo() const noexcept { return vbo_.id(); }
    GLuint ibo() const noexcept { return ibo_.id(); }
    GLuint model_ssbo() const noexcept { return model_ssbo_.id(); }
    GLuint vat_tex() const noexcept { return vat_tex_.id(); }
    GLuint trim_tex(int which) const noexcept {
        return (which >= 0 && which < 4) ? trim_tex_[which].id() : 0;
    }

    bool albedo_needs_decode() const noexcept { return false; }
    bool front_face_ccw() const noexcept { return entity_front_face_ccw(); }

    u64 vram_bytes() const noexcept;

    const EntitySource& source() const noexcept { return src_; }

private:
    EntitySource src_;
    GLuint vao_{0};
    GpuBuffer vbo_;
    GpuBuffer ibo_;
    GpuBuffer model_ssbo_;
    GpuTexture vat_tex_;
    GpuTexture trim_tex_[4];
    u64 vram_bytes_{0};
};

} // namespace ron::render
