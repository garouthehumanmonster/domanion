#include "render/entity_source.hpp"
#include "assets/model_data.hpp"
#include "assets/texture_data.hpp"
#include "core/arena.hpp"
#include "core/assert_log.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

namespace ron::render {

bool entity_front_face_ccw() noexcept {
    return true; // CCW winding per Q5
}

static const char* resolve_models_path(const char* explicit_path) {
    if (explicit_path) return explicit_path;
    static const char* paths[] = {
        RON_SOURCE_DIR "/assets/compiled/models/models_2026.mdbin",
        "assets/compiled/models/models_2026.mdbin",
        "../assets/compiled/models/models_2026.mdbin",
        "../../assets/compiled/models/models_2026.mdbin"
    };
    for (const char* p : paths) {
        FILE* f = std::fopen(p, "rb");
        if (f) {
            std::fclose(f);
            return p;
        }
    }
    return paths[0];
}

static const char* resolve_textures_path(const char* explicit_path) {
    if (explicit_path) return explicit_path;
    static const char* paths[] = {
        RON_SOURCE_DIR "/assets/compiled/textures/textures_2026.tex26",
        "assets/compiled/textures/textures_2026.tex26",
        "../assets/compiled/textures/textures_2026.tex26",
        "../../assets/compiled/textures/textures_2026.tex26"
    };
    for (const char* p : paths) {
        FILE* f = std::fopen(p, "rb");
        if (f) {
            std::fclose(f);
            return p;
        }
    }
    return paths[0];
}

struct EntitySource::Impl {
    MemoryArena model_arena;
    MemoryArena tex_arena;
    assets::ModelData model_data;
    assets::TextureData tex_data;
    bool valid{false};
};

EntitySource::EntitySource() : impl_(std::make_unique<Impl>()) {}
EntitySource::~EntitySource() = default;

bool EntitySource::init(const char* models_path, const char* textures_path) {
    impl_->valid = false;
    models_cpu_.clear();
    models_gpu_.clear();

    if (!impl_->model_arena.init(MiB(64), MiB(2), "model_arena").is_ok()) {
        RON_LOG_ERROR("EntitySource: Failed to initialize model arena");
        return false;
    }
    if (!impl_->tex_arena.init(MiB(64), MiB(2), "tex_arena").is_ok()) {
        RON_LOG_ERROR("EntitySource: Failed to initialize texture arena");
        return false;
    }

    const char* m_path = resolve_models_path(models_path);
    if (impl_->model_data.load_file(m_path, impl_->model_arena) != assets::ModelError::Ok) {
        RON_LOG_ERROR("EntitySource: Failed to load models from %s", m_path);
        return false;
    }

    const char* t_path = resolve_textures_path(textures_path);
    if (impl_->tex_data.load_file(t_path, impl_->tex_arena) != assets::TextureError::Ok) {
        RON_LOG_ERROR("EntitySource: Failed to load textures from %s", t_path);
        return false;
    }

    u32 count = impl_->model_data.model_count();
    if (count > MAX_MODELS) {
        RON_LOG_ERROR("EntitySource: model count %u exceeds MAX_MODELS %u", count, MAX_MODELS);
        return false;
    }

    models_cpu_.resize(count);
    models_gpu_.resize(count);

    const u16* vat_halfs = impl_->model_data.vat_halfs();

    for (u32 m = 0; m < count; ++m) {
        const auto& rec = impl_->model_data.model(m);
        ModelCpu& cpu = models_cpu_[m];
        ModelGpu& gpu = models_gpu_[m];

        cpu.name = "model_" + std::to_string(m);
        cpu.category = rec.category;
        cpu.part_count = rec.part_count;
        cpu.vat_row_base = rec.vat_row_base;

        for (u32 l = 0; l < LODS_PER_MODEL; ++l) {
            cpu.lod_index_count[l]  = rec.idx_count[l];
            cpu.lod_first_index[l]  = rec.idx_first[l];
            cpu.lod_base_vertex[l]  = static_cast<i32>(rec.vtx_base[l]);
        }

        // Bounding sphere calculation across all 44 VAT frames on LOD0 vertices
        u32 vtx_count = rec.vtx_count[0];
        const auto* vtx_base = impl_->model_data.vertices() + rec.vtx_base[0];

        if (vtx_count == 0) {
            cpu.sphere[0] = 0.0f;
            cpu.sphere[1] = 0.0f;
            cpu.sphere[2] = 0.0f;
            cpu.sphere[3] = 1.0f;
        } else {
            float min_p[3] = {  1e30f,  1e30f,  1e30f };
            float max_p[3] = { -1e30f, -1e30f, -1e30f };

            // Pass 1: find AABB over all 44 frames
            for (u32 f = 0; f < VAT_FRAMES; ++f) {
                u32 row = rec.vat_row_base + f;
                float part_m[16][12];
                for (u32 p = 0; p < 16; ++p) {
                    for (u32 r = 0; r < 3; ++r) {
                        u32 col = p * 3 + r;
                        u32 idx = (row * VAT_COLS + col) * 4;
                        for (u32 c = 0; c < 4; ++c) {
                            part_m[p][r * 4 + c] = assets::ModelData::half_to_float(vat_halfs[idx + c]);
                        }
                    }
                }

                for (u32 v = 0; v < vtx_count; ++v) {
                    const auto& vert = vtx_base[v];
                    u32 p = vert.part < 16 ? vert.part : 0;
                    float tx = part_m[p][0] * vert.x + part_m[p][1] * vert.y + part_m[p][2]  * vert.z + part_m[p][3];
                    float ty = part_m[p][4] * vert.x + part_m[p][5] * vert.y + part_m[p][6]  * vert.z + part_m[p][7];
                    float tz = part_m[p][8] * vert.x + part_m[p][9] * vert.y + part_m[p][10] * vert.z + part_m[p][11];

                    min_p[0] = std::min(min_p[0], tx);
                    min_p[1] = std::min(min_p[1], ty);
                    min_p[2] = std::min(min_p[2], tz);
                    max_p[0] = std::max(max_p[0], tx);
                    max_p[1] = std::max(max_p[1], ty);
                    max_p[2] = std::max(max_p[2], tz);
                }
            }

            float centre[3] = {
                0.5f * (min_p[0] + max_p[0]),
                0.5f * (min_p[1] + max_p[1]),
                0.5f * (min_p[2] + max_p[2])
            };

            // Pass 2: find max distance from centre
            float max_d2 = 0.0f;
            for (u32 f = 0; f < VAT_FRAMES; ++f) {
                u32 row = rec.vat_row_base + f;
                float part_m[16][12];
                for (u32 p = 0; p < 16; ++p) {
                    for (u32 r = 0; r < 3; ++r) {
                        u32 col = p * 3 + r;
                        u32 idx = (row * VAT_COLS + col) * 4;
                        for (u32 c = 0; c < 4; ++c) {
                            part_m[p][r * 4 + c] = assets::ModelData::half_to_float(vat_halfs[idx + c]);
                        }
                    }
                }

                for (u32 v = 0; v < vtx_count; ++v) {
                    const auto& vert = vtx_base[v];
                    u32 p = vert.part < 16 ? vert.part : 0;
                    float tx = part_m[p][0] * vert.x + part_m[p][1] * vert.y + part_m[p][2]  * vert.z + part_m[p][3];
                    float ty = part_m[p][4] * vert.x + part_m[p][5] * vert.y + part_m[p][6]  * vert.z + part_m[p][7];
                    float tz = part_m[p][8] * vert.x + part_m[p][9] * vert.y + part_m[p][10] * vert.z + part_m[p][11];

                    float dx = tx - centre[0];
                    float dy = ty - centre[1];
                    float dz = tz - centre[2];
                    max_d2 = std::max(max_d2, dx * dx + dy * dy + dz * dz);
                }
            }

            cpu.sphere[0] = centre[0];
            cpu.sphere[1] = centre[1];
            cpu.sphere[2] = centre[2];
            cpu.sphere[3] = std::sqrt(max_d2) * 1.02f; // pad x1.02
        }

        // Fill ModelGpu
        for (u32 l = 0; l < LODS_PER_MODEL; ++l) {
            gpu.lod[l].index_count = cpu.lod_index_count[l];
            gpu.lod[l].first_index = cpu.lod_first_index[l];
            gpu.lod[l].base_vertex = cpu.lod_base_vertex[l];
            gpu.lod[l].pad = 0;
        }

        gpu.sphere[0] = cpu.sphere[0];
        gpu.sphere[1] = cpu.sphere[1];
        gpu.sphere[2] = cpu.sphere[2];
        gpu.sphere[3] = cpu.sphere[3];

        gpu.vat_row_base = cpu.vat_row_base;
        gpu.vat_pad[0] = 0;
        gpu.vat_pad[1] = 0;
        gpu.vat_pad[2] = 0;
    }

    // Populate trim texture views (IDs 0..3)
    for (u32 t = 0; t < 4; ++t) {
        const auto& rec = impl_->tex_data.texture(t);
        TrimTexView& view = trim_textures_[t];
        view.format = rec.format;
        view.width  = rec.width;
        view.height = rec.height;
        view.mips   = rec.mips;
        view.flags  = rec.flags;

        for (u32 lvl = 0; lvl < rec.mips && lvl < 16; ++lvl) {
            view.levels[lvl].data = impl_->tex_data.level_data(t, lvl, 0);
            u32 lw = std::max(1u, static_cast<u32>(rec.width) >> lvl);
            u32 lh = std::max(1u, static_cast<u32>(rec.height) >> lvl);
            view.levels[lvl].bytes = assets::TextureData::level_size(rec.format, lw, lh);
        }
    }

    impl_->valid = true;
    return true;
}

bool EntitySource::is_valid() const noexcept {
    return impl_->valid;
}

const u8* EntitySource::vertex_blob() const noexcept {
    return reinterpret_cast<const u8*>(impl_->model_data.vertices());
}

usize EntitySource::vertex_bytes() const noexcept {
    return static_cast<usize>(impl_->model_data.vertex_count()) * sizeof(assets::VertexRec);
}

u32 EntitySource::vertex_count() const noexcept {
    return impl_->model_data.vertex_count();
}

const u16* EntitySource::index_blob() const noexcept {
    return impl_->model_data.indices();
}

usize EntitySource::index_bytes() const noexcept {
    return static_cast<usize>(impl_->model_data.index_count()) * sizeof(u16);
}

u32 EntitySource::index_count() const noexcept {
    return impl_->model_data.index_count();
}

const u16* EntitySource::vat_blob() const noexcept {
    return impl_->model_data.vat_halfs();
}

usize EntitySource::vat_bytes() const noexcept {
    return static_cast<usize>(impl_->model_data.vat_rows()) * VAT_COLS * 4 * sizeof(u16);
}

u32 EntitySource::vat_rows() const noexcept {
    return impl_->model_data.vat_rows();
}

u32 EntitySource::vat_cols() const noexcept {
    return VAT_COLS;
}

VatCpu EntitySource::vat_cpu() const noexcept {
    VatCpu v{};
    v.halfs = impl_->model_data.vat_halfs();
    v.rows  = impl_->model_data.vat_rows();
    v.cols  = VAT_COLS;
    return v;
}

u32 EntitySource::model_count() const noexcept {
    return impl_->model_data.model_count();
}

const EntitySource& get_entity_source() {
    static EntitySource s_source;
    static bool s_inited = false;
    if (!s_inited) {
        s_source.init();
        s_inited = true;
    }
    return s_source;
}

} // namespace ron::render
