#pragma once

#include "core/types.hpp"
#include "render/entity_types.hpp"
#include "render/entity_math.hpp"
#include <vector>
#include <memory>

namespace ron::render {

bool entity_front_face_ccw() noexcept;

struct TrimTexView {
    u32 format{0}; // 0=BC1, 1=BC3, 2=BC4, 3=BC5, 4=RGBA8
    u32 width{0};
    u32 height{0};
    u32 mips{0};
    u32 flags{0}; // bit 0: sRGB
    struct Level {
        const u8* data{nullptr};
        u64 bytes{0};
    } levels[16];
};

class EntitySource {
public:
    EntitySource();
    ~EntitySource();

    EntitySource(const EntitySource&) = delete;
    EntitySource& operator=(const EntitySource&) = delete;

    bool init(const char* models_path = nullptr, const char* textures_path = nullptr);
    bool is_valid() const noexcept;

    // Contiguous raw buffers
    const u8* vertex_blob() const noexcept;
    usize vertex_bytes() const noexcept;
    u32 vertex_count() const noexcept;

    const u16* index_blob() const noexcept;
    usize index_bytes() const noexcept;
    u32 index_count() const noexcept;

    const u16* vat_blob() const noexcept;
    usize vat_bytes() const noexcept;
    u32 vat_rows() const noexcept;
    u32 vat_cols() const noexcept;
    VatCpu vat_cpu() const noexcept;

    // Model tables
    u32 model_count() const noexcept;
    const std::vector<ModelCpu>& models_cpu() const noexcept { return models_cpu_; }
    const std::vector<ModelGpu>& models_gpu() const noexcept { return models_gpu_; }
    const ModelCpu& model_cpu(u32 idx) const noexcept { return models_cpu_[idx]; }
    const ModelGpu& model_gpu(u32 idx) const noexcept { return models_gpu_[idx]; }

    // 4 trim textures: 0 albedo, 1 normal, 2 orm, 3 emissive
    const TrimTexView& trim_texture(u32 idx) const noexcept { return trim_textures_[idx]; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<ModelCpu> models_cpu_;
    std::vector<ModelGpu> models_gpu_;
    TrimTexView trim_textures_[4];
};

const EntitySource& get_entity_source();

} // namespace ron::render
