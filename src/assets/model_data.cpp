#include "assets/model_data.hpp"
#include "core/hash.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>

namespace ron::assets {

u64 ModelData::fnv1a64(const char* s) noexcept {
    if (!s) return 0xCBF29CE484222325ULL;
    u64 hash = 0xCBF29CE484222325ULL;
    while (*s != '\0') {
        hash ^= static_cast<u64>(static_cast<u8>(*s));
        hash *= 0x100000001B3ULL;
        ++s;
    }
    return hash;
}

void ModelData::decode_oct(i16 x, i16 y, float out[3]) noexcept {
    float px = static_cast<float>(x) / 32767.0f;
    float py = static_cast<float>(y) / 32767.0f;
    float pz = 1.0f - std::fabs(px) - std::fabs(py);

    if (pz < 0.0f) {
        float sx = (px >= 0.0f) ? 1.0f : -1.0f;
        float sy = (py >= 0.0f) ? 1.0f : -1.0f;
        float px_orig = px;
        float py_orig = py;
        px = (1.0f - std::fabs(py_orig)) * sx;
        py = (1.0f - std::fabs(px_orig)) * sy;
    }

    float len_sq = px * px + py * py + pz * pz;
    if (len_sq > 0.0f) {
        float inv_len = 1.0f / std::sqrt(len_sq);
        out[0] = px * inv_len;
        out[1] = py * inv_len;
        out[2] = pz * inv_len;
    } else {
        out[0] = 0.0f;
        out[1] = 1.0f;
        out[2] = 0.0f;
    }
}

float ModelData::half_to_float(u16 h) noexcept {
    u32 sign = (static_cast<u32>(h) & 0x8000u) << 16;
    u32 exp = (static_cast<u32>(h) >> 10) & 0x1Fu;
    u32 mant = static_cast<u32>(h) & 0x03FFu;

    u32 f = 0;
    if (exp == 0) {
        if (mant == 0) {
            f = sign;
        } else {
            // Subnormal: (-1)^sign * 2^(-14) * (mant / 1024)
            int e = 0;
            while ((mant & 0x0400u) == 0) {
                mant <<= 1;
                e--;
            }
            mant &= 0x03FFu;
            u32 exp32 = static_cast<u32>(127 - 14 + e);
            f = sign | (exp32 << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        // Inf / NaN
        f = sign | (0xFFu << 23) | (mant << 13);
    } else {
        // Normal
        u32 exp32 = exp + (127 - 15);
        f = sign | (exp32 << 23) | (mant << 13);
    }

    float result = 0.0f;
    std::memcpy(&result, &f, sizeof(float));
    return result;
}

ModelError ModelData::init_from_blob(const u8* data, usize size) noexcept {
    is_valid_ = false;
    models_ = nullptr;
    vertices_ = nullptr;
    indices_ = nullptr;
    hardpoints_ = nullptr;
    vat_halfs_ = nullptr;

    // 1. size >= 64 (TooSmall)
    if (!data || size < 64) {
        return ModelError::TooSmall;
    }

    std::memcpy(&header_, data, sizeof(ModelHeader));

    // 2. magic == 0x36324D44 (BadMagic)
    if (header_.magic != 0x36324D44u) {
        return ModelError::BadMagic;
    }

    // 3. version == 1 (BadVersion)
    if (header_.version != 1) {
        return ModelError::BadVersion;
    }

    // 4. total_bytes == size (BadSize)
    if (header_.total_bytes != size) {
        return ModelError::BadSize;
    }

    // 5. section_count == 5 && vat_cols == 48 && frames_per_model == 44 && 1 <= model_count && model_count <= 256 && vat_rows == 44 * model_count (BadHeader)
    if (header_.section_count != 5 ||
        header_.vat_cols != 48 ||
        header_.frames_per_model != 44 ||
        header_.model_count < 1 ||
        header_.model_count > 256 ||
        header_.vat_rows != 44 * header_.model_count) {
        return ModelError::BadHeader;
    }

    // 6. Section table (8 slots starting at offset 64, size >= 192):
    //    For slots 0..4: id == (i+1), offset % 64 == 0, offset >= 192, (u64)offset + size_bytes <= total_bytes (BadSectionTable)
    if (size < 192) {
        return ModelError::BadSectionTable;
    }

    const SectionEntry* sec = reinterpret_cast<const SectionEntry*>(data + 64);
    for (u32 i = 0; i < 5; ++i) {
        if (sec[i].id != (i + 1) ||
            (sec[i].offset % 64) != 0 ||
            sec[i].offset < 192 ||
            static_cast<u64>(sec[i].offset) + sec[i].size_bytes > header_.total_bytes) {
            return ModelError::BadSectionTable;
        }
    }

    // 7. Exact section sizes:
    //    sec 0: size_bytes == model_count * 96 && count == model_count
    //    sec 1: size_bytes == vertex_count * 24 && count == vertex_count
    //    sec 2: size_bytes == index_count * 2 && count == index_count
    //    sec 3: size_bytes == hardpoint_count * 16 && count == hardpoint_count
    //    sec 4: size_bytes == vat_rows * 48 * 8 && count == vat_rows
    //    (BadSectionSize)
    if (sec[0].size_bytes != header_.model_count * 96u || sec[0].count != header_.model_count ||
        sec[1].size_bytes != header_.vertex_count * 24u || sec[1].count != header_.vertex_count ||
        sec[2].size_bytes != header_.index_count * 2u || sec[2].count != header_.index_count ||
        sec[3].size_bytes != header_.hardpoint_count * 16u || sec[3].count != header_.hardpoint_count ||
        sec[4].size_bytes != header_.vat_rows * 48u * 8u || sec[4].count != header_.vat_rows) {
        return ModelError::BadSectionSize;
    }

    // 8. Hash check:
    //    ron::xxh64(data + 64, size - 64, 0) == header_.content_xxh64 (BadHash)
    u64 computed_hash = ron::xxh64(std::span<const u8>(data + 64, size - 64), 0);
    if (computed_hash != header_.content_xxh64) {
        return ModelError::BadHash;
    }

    // 9. Ranges in model table:
    //    For each model:
    //      for lod 0..2:
    //        vtx_base[lod] + vtx_count[lod] <= vertex_count
    //        idx_first[lod] + idx_count[lod] <= index_count
    //        idx_count[lod] % 3 == 0
    //      hp_first + hp_count <= hardpoint_count
    //      part_count <= 16
    //    (BadModelTable)
    const ModelRec* model_table = reinterpret_cast<const ModelRec*>(data + sec[0].offset);
    for (u32 m = 0; m < header_.model_count; ++m) {
        const ModelRec& rec = model_table[m];
        for (u32 lod = 0; lod < 3; ++lod) {
            if (static_cast<u64>(rec.vtx_base[lod]) + rec.vtx_count[lod] > header_.vertex_count) {
                return ModelError::BadModelTable;
            }
            if (static_cast<u64>(rec.idx_first[lod]) + rec.idx_count[lod] > header_.index_count) {
                return ModelError::BadModelTable;
            }
            if ((rec.idx_count[lod] % 3) != 0) {
                return ModelError::BadModelTable;
            }
        }
        if (static_cast<u64>(rec.hp_first) + rec.hp_count > header_.hardpoint_count) {
            return ModelError::BadModelTable;
        }
        if (rec.part_count > 16) {
            return ModelError::BadModelTable;
        }
    }

    models_ = model_table;
    vertices_ = reinterpret_cast<const VertexRec*>(data + sec[1].offset);
    indices_ = reinterpret_cast<const u16*>(data + sec[2].offset);
    hardpoints_ = reinterpret_cast<const HardpointRec*>(data + sec[3].offset);
    vat_halfs_ = reinterpret_cast<const u16*>(data + sec[4].offset);
    is_valid_ = true;

    return ModelError::Ok;
}

ModelError ModelData::load_file(const char* filepath, MemoryArena& arena) noexcept {
    if (!filepath) {
        return ModelError::TooSmall;
    }
    FILE* f = std::fopen(filepath, "rb");
    if (!f) {
        return ModelError::TooSmall;
    }

    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (size < 64) {
        std::fclose(f);
        return ModelError::TooSmall;
    }

    u8* buf = arena.alloc<u8>(static_cast<usize>(size));
    if (!buf) {
        std::fclose(f);
        return ModelError::TooSmall;
    }

    usize read_bytes = std::fread(buf, 1, static_cast<usize>(size), f);
    std::fclose(f);

    if (read_bytes != static_cast<usize>(size)) {
        return ModelError::BadSize;
    }

    return init_from_blob(buf, static_cast<usize>(size));
}

i32 ModelData::find(u64 id_hash) const noexcept {
    if (!is_valid_) return -1;
    for (u32 i = 0; i < header_.model_count; ++i) {
        if (models_[i].id_hash == id_hash) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

bool ModelData::vat_matrix(u32 model_idx, u32 clip_idx, u32 frame_idx, u32 part_idx, float m[12]) const noexcept {
    if (!is_valid_ || !m) {
        return false;
    }
    if (model_idx >= header_.model_count || clip_idx >= 4 || part_idx >= 16) {
        return false;
    }
    if (frame_idx >= kClipFrames[clip_idx]) {
        return false;
    }

    u32 row = models_[model_idx].vat_row_base + kClipStart[clip_idx] + frame_idx;
    if (row >= header_.vat_rows) {
        return false;
    }

    for (u32 r = 0; r < 3; ++r) {
        u32 col = part_idx * 3 + r;
        u32 idx = row * 48 * 4 + col * 4;
        m[r * 4 + 0] = half_to_float(vat_halfs_[idx + 0]);
        m[r * 4 + 1] = half_to_float(vat_halfs_[idx + 1]);
        m[r * 4 + 2] = half_to_float(vat_halfs_[idx + 2]);
        m[r * 4 + 3] = half_to_float(vat_halfs_[idx + 3]);
    }

    return true;
}

} // namespace ron::assets
