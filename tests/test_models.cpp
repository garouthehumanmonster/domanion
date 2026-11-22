#include "test_framework.hpp"
#include "core/arena.hpp"
#include "assets/model_data.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

#ifndef RON_FAIL
#define RON_FAIL(msg) do { \
    std::fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
    ::ron::test::g_assert_failures++; \
    return; \
} while(0)
#endif

namespace ron {

static const char* get_models_path() {
    static const char* paths[] = {
        RON_SOURCE_DIR "/assets/compiled/models/models_2026.mdbin",
        "assets/compiled/models/models_2026.mdbin",
        "../assets/compiled/models/models_2026.mdbin",
        "../../assets/compiled/models/models_2026.mdbin"
    };

    for (const char* p : paths) {
        FILE* f = fopen(p, "rb");
        if (f) {
            fclose(f);
            return p;
        }
    }
    return paths[0];
}

// 1. Model_LoadHeader
RON_TEST(Model_LoadHeader) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(32), MiB(1), "model_arena_1").is_ok());

    const char* path = get_models_path();
    FILE* f = fopen(path, "rb");
    if (!f) {
        RON_FAIL("run: python tools/model_baker/bake_models.py");
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    u8* orig = arena.alloc<u8>(static_cast<usize>(fsize));
    RON_CHECK(orig != nullptr);
    usize nread = fread(orig, 1, static_cast<usize>(fsize), f);
    RON_CHECK_EQ(nread, static_cast<usize>(fsize));
    fclose(f);

    assets::ModelData md;
    assets::ModelError err = md.load_file(path, arena);
    RON_CHECK(err == assets::ModelError::Ok);
    RON_CHECK_EQ(md.model_count(), 71u);
    RON_CHECK_EQ(md.vat_rows(), 3124u);

    // copy into arena with a flipped payload byte (offset >= 64, e.g. 192) -> init_from_blob returns BadHash
    {
        assets::ModelData bad_md;
        u8* copy = arena.alloc<u8>(static_cast<usize>(fsize));
        std::memcpy(copy, orig, static_cast<usize>(fsize));
        copy[192] ^= 0xFF;
        RON_CHECK(bad_md.init_from_blob(copy, static_cast<usize>(fsize)) == assets::ModelError::BadHash);
    }

    // copy with first byte changed -> BadMagic
    {
        assets::ModelData bad_md;
        u8* copy = arena.alloc<u8>(static_cast<usize>(fsize));
        std::memcpy(copy, orig, static_cast<usize>(fsize));
        copy[0] ^= 0xFF;
        RON_CHECK(bad_md.init_from_blob(copy, static_cast<usize>(fsize)) == assets::ModelError::BadMagic);
    }

    // copy truncated by 64 bytes -> BadSize
    {
        assets::ModelData bad_md;
        u8* copy = arena.alloc<u8>(static_cast<usize>(fsize - 64));
        std::memcpy(copy, orig, static_cast<usize>(fsize - 64));
        RON_CHECK(bad_md.init_from_blob(copy, static_cast<usize>(fsize - 64)) == assets::ModelError::BadSize);
    }

    // 10-byte buffer -> TooSmall
    {
        assets::ModelData bad_md;
        u8 buf[10]{0};
        RON_CHECK(bad_md.init_from_blob(buf, 10) == assets::ModelError::TooSmall);
    }

    arena.shutdown();
}

// 2. Model_Lookup
RON_TEST(Model_Lookup) {
    RON_CHECK_EQ(assets::ModelData::fnv1a64(""), 0xCBF29CE484222325ULL);
    RON_CHECK_EQ(assets::ModelData::fnv1a64("a"), 0xAF63DC4C8601EC8CULL);

    MemoryArena arena;
    RON_CHECK(arena.init(MiB(32), MiB(1), "model_arena_2").is_ok());

    assets::ModelData md;
    const char* path = get_models_path();
    assets::ModelError err = md.load_file(path, arena);
    if (err != assets::ModelError::Ok) {
        RON_FAIL("run: python tools/model_baker/bake_models.py");
    }

    RON_CHECK_EQ(md.model_count(), 71u);

    // all 71 id hashes unique
    for (u32 i = 0; i < md.model_count(); ++i) {
        for (u32 j = i + 1; j < md.model_count(); ++j) {
            RON_CHECK_NE(md.model(i).id_hash, md.model(j).id_hash);
        }
    }

    struct NamedModel {
        const char* name;
        u8 expected_cat;
    };
    const NamedModel expected_models[] = {
        {"inf_rifle", 0},
        {"veh_mbt", 1},
        {"nav_carrier", 3},
        {"bld_city_center_c", 4},
        {"proj_hypersonic", 7}
    };

    for (const auto& item : expected_models) {
        u64 h = assets::ModelData::fnv1a64(item.name);
        i32 idx = md.find(h);
        RON_CHECK(idx >= 0);
        if (idx >= 0) {
            RON_CHECK_EQ(md.model(static_cast<u32>(idx)).id_hash, h);
            RON_CHECK_EQ(md.model(static_cast<u32>(idx)).category, item.expected_cat);
        }
    }

    RON_CHECK_EQ(md.find(assets::ModelData::fnv1a64("nope")), -1);

    arena.shutdown();
}

// 3. Model_BufferIntegrity
RON_TEST(Model_BufferIntegrity) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(32), MiB(1), "model_arena_3").is_ok());

    assets::ModelData md;
    const char* path = get_models_path();
    assets::ModelError err = md.load_file(path, arena);
    if (err != assets::ModelError::Ok) {
        RON_FAIL("run: python tools/model_baker/bake_models.py");
    }

    for (u32 m = 0; m < md.model_count(); ++m) {
        const auto& rec = md.model(m);

        RON_CHECK_EQ(rec.tri_count_lod0, static_cast<u16>(rec.idx_count[0] / 3));
        RON_CHECK(rec.bmin[0] < rec.bmax[0]);
        RON_CHECK(rec.bmin[1] < rec.bmax[1]);
        RON_CHECK(rec.bmin[2] < rec.bmax[2]);

        RON_CHECK(rec.idx_count[0] >= rec.idx_count[1]);
        RON_CHECK(rec.idx_count[1] >= rec.idx_count[2]);
        RON_CHECK(rec.vtx_count[0] >= rec.vtx_count[1]);
        RON_CHECK(rec.vtx_count[1] >= rec.vtx_count[2]);

        for (u32 lod = 0; lod < 3; ++lod) {
            RON_CHECK(static_cast<u64>(rec.vtx_base[lod]) + rec.vtx_count[lod] <= md.vertex_count());
            RON_CHECK(static_cast<u64>(rec.idx_first[lod]) + rec.idx_count[lod] <= md.index_count());
            RON_CHECK(rec.idx_count[lod] % 3 == 0);
            RON_CHECK(rec.idx_count[lod] > 0);

            u32 tri_count = rec.idx_count[lod] / 3;
            const u16* idx_ptr = md.indices() + rec.idx_first[lod];
            for (u32 t = 0; t < tri_count; ++t) {
                u16 i0 = idx_ptr[t * 3 + 0];
                u16 i1 = idx_ptr[t * 3 + 1];
                u16 i2 = idx_ptr[t * 3 + 2];
                RON_CHECK(i0 < rec.vtx_count[lod]);
                RON_CHECK(i1 < rec.vtx_count[lod]);
                RON_CHECK(i2 < rec.vtx_count[lod]);
                RON_CHECK(i0 != i1 && i1 != i2 && i0 != i2);
            }
        }
    }

    arena.shutdown();
}

// 4. Model_VertexDecode
RON_TEST(Model_VertexDecode) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(32), MiB(1), "model_arena_4").is_ok());

    assets::ModelData md;
    const char* path = get_models_path();
    assets::ModelError err = md.load_file(path, arena);
    if (err != assets::ModelError::Ok) {
        RON_FAIL("run: python tools/model_baker/bake_models.py");
    }

    for (u32 m = 0; m < md.model_count(); ++m) {
        const auto& rec = md.model(m);
        float min_x = rec.bmin[0] / 65536.0f - 0.01f;
        float min_y = rec.bmin[1] / 65536.0f - 0.01f;
        float min_z = rec.bmin[2] / 65536.0f - 0.01f;
        float max_x = rec.bmax[0] / 65536.0f + 0.01f;
        float max_y = rec.bmax[1] / 65536.0f + 0.01f;
        float max_z = rec.bmax[2] / 65536.0f + 0.01f;

        for (u32 lod = 0; lod < 3; ++lod) {
            const assets::VertexRec* v_ptr = md.vertices() + rec.vtx_base[lod];
            for (u32 v = 0; v < rec.vtx_count[lod]; ++v) {
                const auto& vtx = v_ptr[v];
                RON_CHECK(std::isfinite(vtx.x) && std::isfinite(vtx.y) && std::isfinite(vtx.z));
                RON_CHECK(vtx.x >= min_x && vtx.x <= max_x);
                RON_CHECK(vtx.y >= min_y && vtx.y <= max_y);
                RON_CHECK(vtx.z >= min_z && vtx.z <= max_z);

                float n[3];
                assets::ModelData::decode_oct(vtx.nx, vtx.ny, n);
                float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                RON_CHECK(std::fabs(len - 1.0f) <= 0.002f);

                RON_CHECK(vtx.u <= 65535 && vtx.v <= 65535);
                RON_CHECK(vtx.trim < 32);
                RON_CHECK(vtx.part < rec.part_count);
            }
        }
    }

    // Winding / normal agreement for 5 models (>= 99% of LOD0 triangles)
    const char* test_models[] = {
        "inf_rifle", "veh_mbt", "nav_destroyer", "bld_factory", "air_jet"
    };

    for (const char* name : test_models) {
        i32 idx = md.find(assets::ModelData::fnv1a64(name));
        RON_CHECK(idx >= 0);
        if (idx < 0) continue;

        const auto& rec = md.model(static_cast<u32>(idx));
        u32 tri_count = rec.idx_count[0] / 3;
        const u16* idx_ptr = md.indices() + rec.idx_first[0];
        const assets::VertexRec* v_ptr = md.vertices() + rec.vtx_base[0];

        u32 agree_count = 0;
        u32 valid_tri_count = 0;

        for (u32 t = 0; t < tri_count; ++t) {
            u16 i0 = idx_ptr[t * 3 + 0];
            u16 i1 = idx_ptr[t * 3 + 1];
            u16 i2 = idx_ptr[t * 3 + 2];

            const auto& v0 = v_ptr[i0];
            const auto& v1 = v_ptr[i1];
            const auto& v2 = v_ptr[i2];

            // Geometric normal: (v1 - v0) x (v2 - v0)
            float e1x = v1.x - v0.x;
            float e1y = v1.y - v0.y;
            float e1z = v1.z - v0.z;

            float e2x = v2.x - v0.x;
            float e2y = v2.y - v0.y;
            float e2z = v2.z - v0.z;

            float gx = e1y * e2z - e1z * e2y;
            float gy = e1z * e2x - e1x * e2z;
            float gz = e1x * e2y - e1y * e2x;

            float g_len = std::sqrt(gx * gx + gy * gy + gz * gz);
            if (g_len <= 1e-7f) {
                continue; // Degenerate geometric triangle
            }
            valid_tri_count++;

            // Decoded normals
            float n0[3], n1[3], n2[3];
            assets::ModelData::decode_oct(v0.nx, v0.ny, n0);
            assets::ModelData::decode_oct(v1.nx, v1.ny, n1);
            assets::ModelData::decode_oct(v2.nx, v2.ny, n2);

            float avg_nx = n0[0] + n1[0] + n2[0];
            float avg_ny = n0[1] + n1[1] + n2[1];
            float avg_nz = n0[2] + n1[2] + n2[2];

            float dot = avg_nx * gx + avg_ny * gy + avg_nz * gz;
            if (dot > 0.0f) {
                agree_count++;
            }
        }

        RON_CHECK(valid_tri_count > 0);
        double ratio = (valid_tri_count > 0) ? (static_cast<double>(agree_count) / valid_tri_count) : 0.0;
        RON_CHECK(ratio >= 0.99);
    }

    arena.shutdown();
}

// 5. Model_VatIntegrity
RON_TEST(Model_VatIntegrity) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(32), MiB(1), "model_arena_5").is_ok());

    assets::ModelData md;
    const char* path = get_models_path();
    assets::ModelError err = md.load_file(path, arena);
    if (err != assets::ModelError::Ok) {
        RON_FAIL("run: python tools/model_baker/bake_models.py");
    }

    for (u32 m = 0; m < md.model_count(); ++m) {
        const auto& rec = md.model(m);

        // Frame 0 of each of the 4 clips is identity for all 16 parts (tolerance 1e-3)
        for (u32 clip = 0; clip < 4; ++clip) {
            for (u32 part = 0; part < 16; ++part) {
                float mat[12];
                bool ok = md.vat_matrix(m, clip, 0, part, mat);
                RON_CHECK(ok);
                for (u32 r = 0; r < 3; ++r) {
                    for (u32 c = 0; c < 4; ++c) {
                        float expected = (r == c) ? 1.0f : 0.0f;
                        RON_CHECK(std::fabs(mat[r * 4 + c] - expected) <= 1e-3f);
                    }
                }
            }
        }

        // All matrices finite, every 3x3 row length within 0.01 of 1
        bool has_idle_motion = false;

        for (u32 clip = 0; clip < 4; ++clip) {
            u32 frames = assets::kClipFrames[clip];
            for (u32 f = 0; f < frames; ++f) {
                for (u32 part = 0; part < 16; ++part) {
                    float mat[12];
                    bool ok = md.vat_matrix(m, clip, f, part, mat);
                    RON_CHECK(ok);

                    for (u32 i = 0; i < 12; ++i) {
                        RON_CHECK(std::isfinite(mat[i]));
                    }

                    for (u32 r = 0; r < 3; ++r) {
                        float rlen = std::sqrt(mat[r * 4 + 0] * mat[r * 4 + 0] +
                                               mat[r * 4 + 1] * mat[r * 4 + 1] +
                                               mat[r * 4 + 2] * mat[r * 4 + 2]);
                        RON_CHECK(std::fabs(rlen - 1.0f) <= 0.01f);
                    }

                    if (clip == 0 && f > 0) {
                        for (u32 r = 0; r < 3; ++r) {
                            for (u32 c = 0; c < 4; ++c) {
                                float expected = (r == c) ? 1.0f : 0.0f;
                                if (std::fabs(mat[r * 4 + c] - expected) > 1e-3f) {
                                    has_idle_motion = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        // For models with flags & 2 (animated-idle), at least one Idle frame differs from identity by > 1e-3
        if ((rec.flags & 2) != 0) {
            RON_CHECK(has_idle_motion);
        }
    }

    // bld_barracks has no Idle motion (diff <= 1e-3)
    {
        i32 idx = md.find(assets::ModelData::fnv1a64("bld_barracks"));
        RON_CHECK(idx >= 0);
        if (idx >= 0) {
            u32 frames = assets::kClipFrames[0];
            for (u32 f = 0; f < frames; ++f) {
                for (u32 part = 0; part < 16; ++part) {
                    float mat[12];
                    RON_CHECK(md.vat_matrix(static_cast<u32>(idx), 0, f, part, mat));
                    for (u32 r = 0; r < 3; ++r) {
                        for (u32 c = 0; c < 4; ++c) {
                            float expected = (r == c) ? 1.0f : 0.0f;
                            RON_CHECK(std::fabs(mat[r * 4 + c] - expected) <= 1e-3f);
                        }
                    }
                }
            }
        }
    }

    // air_helo has Idle motion (diff > 1e-3)
    {
        i32 idx = md.find(assets::ModelData::fnv1a64("air_helo"));
        RON_CHECK(idx >= 0);
        if (idx >= 0) {
            bool found_diff = false;
            u32 frames = assets::kClipFrames[0];
            for (u32 f = 0; f < frames; ++f) {
                for (u32 part = 0; part < 16; ++part) {
                    float mat[12];
                    RON_CHECK(md.vat_matrix(static_cast<u32>(idx), 0, f, part, mat));
                    for (u32 r = 0; r < 3; ++r) {
                        for (u32 c = 0; c < 4; ++c) {
                            float expected = (r == c) ? 1.0f : 0.0f;
                            if (std::fabs(mat[r * 4 + c] - expected) > 1e-3f) {
                                found_diff = true;
                            }
                        }
                    }
                }
            }
            RON_CHECK(found_diff);
        }
    }

    arena.shutdown();
}

// 6. Model_Hardpoints
RON_TEST(Model_Hardpoints) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(32), MiB(1), "model_arena_6").is_ok());

    assets::ModelData md;
    const char* path = get_models_path();
    assets::ModelError err = md.load_file(path, arena);
    if (err != assets::ModelError::Ok) {
        RON_FAIL("run: python tools/model_baker/bake_models.py");
    }

    const i32 pad = 65536; // 1.0 m raw fx32

    for (u32 m = 0; m < md.model_count(); ++m) {
        const auto& rec = md.model(m);
        RON_CHECK(static_cast<u64>(rec.hp_first) + rec.hp_count <= md.hardpoint_count());

        const assets::HardpointRec* hp_ptr = md.hardpoints() + rec.hp_first;
        for (u32 h = 0; h < rec.hp_count; ++h) {
            const auto& hp = hp_ptr[h];
            RON_CHECK(hp.kind <= 7);
            RON_CHECK(hp.part < rec.part_count);

            RON_CHECK(hp.x >= rec.bmin[0] - pad && hp.x <= rec.bmax[0] + pad);
            RON_CHECK(hp.y >= rec.bmin[1] - pad && hp.y <= rec.bmax[1] + pad);
            RON_CHECK(hp.z >= rec.bmin[2] - pad && hp.z <= rec.bmax[2] + pad);
        }
    }

    // inf_rifle, veh_mbt, air_jet, nav_destroyer, bld_tower each have >= 1 Muzzle (kind 0)
    const char* muzzle_models[] = {
        "inf_rifle", "veh_mbt", "air_jet", "nav_destroyer", "bld_tower"
    };
    for (const char* name : muzzle_models) {
        i32 idx = md.find(assets::ModelData::fnv1a64(name));
        RON_CHECK(idx >= 0);
        if (idx >= 0) {
            const auto& rec = md.model(static_cast<u32>(idx));
            u32 muzzle_count = 0;
            const assets::HardpointRec* hp_ptr = md.hardpoints() + rec.hp_first;
            for (u32 h = 0; h < rec.hp_count; ++h) {
                if (hp_ptr[h].kind == 0) {
                    muzzle_count++;
                }
            }
            RON_CHECK(muzzle_count >= 1);
        }
    }

    // bld_barracks, bld_factory, bld_dock each have >= 1 Hatch (kind 4)
    const char* hatch_models[] = {
        "bld_barracks", "bld_factory", "bld_dock"
    };
    for (const char* name : hatch_models) {
        i32 idx = md.find(assets::ModelData::fnv1a64(name));
        RON_CHECK(idx >= 0);
        if (idx >= 0) {
            const auto& rec = md.model(static_cast<u32>(idx));
            u32 hatch_count = 0;
            const assets::HardpointRec* hp_ptr = md.hardpoints() + rec.hp_first;
            for (u32 h = 0; h < rec.hp_count; ++h) {
                if (hp_ptr[h].kind == 4) {
                    hatch_count++;
                }
            }
            RON_CHECK(hatch_count >= 1);
        }
    }

    // veh_truck has no Muzzle
    {
        i32 idx = md.find(assets::ModelData::fnv1a64("veh_truck"));
        RON_CHECK(idx >= 0);
        if (idx >= 0) {
            const auto& rec = md.model(static_cast<u32>(idx));
            u32 muzzle_count = 0;
            const assets::HardpointRec* hp_ptr = md.hardpoints() + rec.hp_first;
            for (u32 h = 0; h < rec.hp_count; ++h) {
                if (hp_ptr[h].kind == 0) {
                    muzzle_count++;
                }
            }
            RON_CHECK_EQ(muzzle_count, 0u);
        }
    }

    arena.shutdown();
}

} // namespace ron
