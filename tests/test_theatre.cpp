#include "test_framework.hpp"
#include "core/arena.hpp"
#include "core/fixed_math.hpp"
#include "core/fixed_geom.hpp"
#include "sim/theatre_data.hpp"
#include <vector>
#include <queue>
#include <cstdio>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

namespace ron {

static const char* get_theatre_path() {
    static const char* paths[] = {
        RON_SOURCE_DIR "/assets/compiled/theatres/continent_2026.thtr",
        "assets/compiled/theatres/continent_2026.thtr",
        "../assets/compiled/theatres/continent_2026.thtr",
        "../../assets/compiled/theatres/continent_2026.thtr"
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

RON_TEST(Theatre_LoadHeader) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(32), MiB(1), "theatre_arena").is_ok());

    sim::TheatreData theatre;
    const char* path = get_theatre_path();
    auto res = theatre.load_file(path, arena);
    RON_CHECK(res.is_ok());
    RON_CHECK(theatre.is_valid());

    const auto& hdr = theatre.header();
    RON_CHECK_EQ(hdr.magic, 0x52544854u);
    RON_CHECK_EQ(hdr.version, 1u);
    RON_CHECK_EQ(hdr.section_count, 8u);
    RON_CHECK_EQ(hdr.height_n, 2048u);
    RON_CHECK_EQ(hdr.logic_n, 512u);
    RON_CHECK_EQ(hdr.world_m, 4096u);
    RON_CHECK_EQ(hdr.player_slots, 8u);
    RON_CHECK_EQ(hdr.start_count, 8u);
    RON_CHECK_EQ(hdr.sea_level_code, 13107u);

    RON_CHECK_EQ(theatre.starts().size(), 8u);
    RON_CHECK(theatre.resources().size() >= 120u);
    RON_CHECK(theatre.resources().size() <= 1024u);
    RON_CHECK(theatre.landings().size() >= 6u);
}

RON_TEST(Theatre_HeightQuery) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(32), MiB(1), "theatre_arena").is_ok());

    sim::TheatreData theatre;
    const char* path = get_theatre_path();
    RON_CHECK(theatre.load_file(path, arena).is_ok());

    // 1. Query at all 8 start locations: must match plateau_hcode within +-0.01m (655 in fx32)
    for (const auto& s : theatre.starts()) {
        fx32 sx = sim::TheatreData::q_to_fx32(s.x_q);
        fx32 sz = sim::TheatreData::q_to_fx32(s.z_q);
        fx32 sampled = theatre.sample_height_fx32(sx, sz);
        fx32 expected = sim::TheatreData::hcode_to_fx32(s.plateau_hcode);
        fx32 diff = abs_fx(sampled - expected);
        RON_CHECK(diff.raw <= 655); // <= 0.01m
    }

    // 2. Query at boundary coordinates: no crash, valid finite results
    fx32 h_00 = theatre.sample_height_fx32(fx32::zero(), fx32::zero());
    fx32 h_max = theatre.sample_height_fx32(fx32::from_int(4096), fx32::from_int(4096));
    RON_CHECK(h_00.raw >= -(128 << 16) && h_00.raw <= (512 << 16));
    RON_CHECK(h_max.raw >= -(128 << 16) && h_max.raw <= (512 << 16));

    // 3. Query at logic cell centres
    for (u32 z = 100; z < 110; ++z) {
        for (u32 x = 100; x < 110; ++x) {
            fx32 cx = fx32::from_int((i32)(x * 8 + 4));
            fx32 cz = fx32::from_int((i32)(z * 8 + 4));
            fx32 h = theatre.sample_height_fx32(cx, cz);
            RON_CHECK(h.raw >= -(128 << 16) && h.raw <= (512 << 16));
        }
    }
}

RON_TEST(Theatre_SurfaceConsistency) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(32), MiB(1), "theatre_arena").is_ok());

    sim::TheatreData theatre;
    const char* path = get_theatre_path();
    RON_CHECK(theatre.load_file(path, arena).is_ok());

    const auto surf = theatre.surface_grid();
    const auto slope = theatre.slope_grid();
    const auto h_grid = theatre.height_grid();
    const u32 logic_n = theatre.logic_n();
    const u32 height_n = theatre.height_n();
    const u32 block = height_n / logic_n;
    const u32 block_sq = block * block;

    for (u32 z = 0; z < logic_n; ++z) {
        for (u32 x = 0; x < logic_n; ++x) {
            u32 idx = z * logic_n + x;
            sim::Surface s = static_cast<sim::Surface>(surf[idx]);
            u8 sl = slope[idx];

            u64 sum_code = 0;
            for (u32 r = z * block; r < (z + 1) * block; ++r) {
                for (u32 c = x * block; c < (x + 1) * block; ++c) {
                    sum_code += h_grid[r * height_n + c];
                }
            }
            u16 mean_code = (u16)(sum_code / block_sq);
            fx32 mean_h = sim::TheatreData::hcode_to_fx32(mean_code);

            if (s == sim::Surface::DeepWater) {
                RON_CHECK(mean_h.raw <= -(8 << 16) + 655);
            } else if (s == sim::Surface::ShallowWater) {
                RON_CHECK(mean_h.raw <= -(49152) + 655); // -0.75m in Q16.16 is -49152
            } else if (s == sim::Surface::Beach) {
                RON_CHECK(sl <= 12);
            } else if (s == sim::Surface::Cliff) {
                RON_CHECK(sl >= 40);
            }
        }
    }
}

RON_TEST(Theatre_StartsPlateaus) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(32), MiB(1), "theatre_arena").is_ok());

    sim::TheatreData theatre;
    const char* path = get_theatre_path();
    RON_CHECK(theatre.load_file(path, arena).is_ok());

    const auto starts = theatre.starts();
    RON_CHECK_EQ(starts.size(), 8u);

    // 1. All 8 starts valid flags and slots 0..7
    for (u32 i = 0; i < 8; ++i) {
        RON_CHECK_EQ(starts[i].slot, (u8)i);
        RON_CHECK(starts[i].flags != 0);
    }

    // 2. Pairwise distance >= 600m
    const i64 min_dist_q = 600 * 16;
    const i64 min_dist_q_sq = min_dist_q * min_dist_q;
    for (u32 i = 0; i < 8; ++i) {
        for (u32 j = i + 1; j < 8; ++j) {
            i64 dx = (i64)starts[i].x_q - (i64)starts[j].x_q;
            i64 dz = (i64)starts[i].z_q - (i64)starts[j].z_q;
            i64 d2 = dx * dx + dz * dz;
            RON_CHECK(d2 >= min_dist_q_sq);
        }
    }

    // 3. Within 60m of each start, logic cells have Surface == Land and height within +-0.05m
    for (const auto& s : starts) {
        fx32 sx = sim::TheatreData::q_to_fx32(s.x_q);
        fx32 sz = sim::TheatreData::q_to_fx32(s.z_q);
        fx32 expected_h = sim::TheatreData::hcode_to_fx32(s.plateau_hcode);

        i32 min_cx = ((s.x_q >> 4) - 60) / 8;
        i32 max_cx = ((s.x_q >> 4) + 60) / 8;
        i32 min_cz = ((s.z_q >> 4) - 60) / 8;
        i32 max_cz = ((s.z_q >> 4) + 60) / 8;

        for (i32 cz = min_cz; cz <= max_cz; ++cz) {
            for (i32 cx = min_cx; cx <= max_cx; ++cx) {
                if (cx >= 0 && cx < 512 && cz >= 0 && cz < 512) {
                    fx32 cell_x = fx32::from_int(cx * 8 + 4);
                    fx32 cell_z = fx32::from_int(cz * 8 + 4);
                    Vec2fx p1{cell_x, cell_z};
                    Vec2fx p2{sx, sz};
                    fx32 dist = p1.distance(p2);
                    if (dist.raw <= (60 << 16)) {
                        sim::Surface surf = theatre.surface_at_logic(cx, cz);
                        RON_CHECK_EQ((u8)surf, (u8)sim::Surface::Land);
                        fx32 h = theatre.sample_height_fx32(cell_x, cell_z);
                        RON_CHECK(abs_fx(h - expected_h).raw <= 3276); // <= 0.05m
                    }
                }
            }
        }
    }
}

RON_TEST(Theatre_ResourcesFair) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(32), MiB(1), "theatre_arena").is_ok());

    sim::TheatreData theatre;
    const char* path = get_theatre_path();
    RON_CHECK(theatre.load_file(path, arena).is_ok());

    const auto res = theatre.resources();
    const auto starts = theatre.starts();
    RON_CHECK(res.size() >= 120u && res.size() <= 1024u);

    // Check kit resources per start:
    // (Berries 3, Herd 2, Forest 6, Iron 3, Gold 2, Oil 1, Silicon 1)
    for (u8 slot = 0; slot < 8; ++slot) {
        u32 berries = 0, herd = 0, forest = 0, iron = 0, gold = 0, oil = 0, silicon = 0;
        for (const auto& r : res) {
            if (r.owner == slot) {
                if (r.kind == (u8)sim::NodeKind::Berries) berries++;
                else if (r.kind == (u8)sim::NodeKind::Herd) herd++;
                else if (r.kind == (u8)sim::NodeKind::Forest) forest++;
                else if (r.kind == (u8)sim::NodeKind::Iron) iron++;
                else if (r.kind == (u8)sim::NodeKind::Gold) gold++;
                else if (r.kind == (u8)sim::NodeKind::Oil) oil++;
                else if (r.kind == (u8)sim::NodeKind::Silicon) silicon++;
            }
        }
        RON_CHECK_EQ(berries, 3u);
        RON_CHECK_EQ(herd, 2u);
        RON_CHECK_EQ(forest, 6u);
        RON_CHECK_EQ(iron, 3u);
        RON_CHECK_EQ(gold, 2u);
        RON_CHECK_EQ(oil, 1u);
        RON_CHECK_EQ(silicon, 1u);
    }

    // Neutral resources have owner 0xFF and distance >= 420m from all starts
    const i64 min_dist_q = 420 * 16;
    const i64 min_dist_q_sq = min_dist_q * min_dist_q;

    for (const auto& r : res) {
        if (r.owner == 0xFF) {
            for (const auto& s : starts) {
                i64 dx = (i64)r.x_q - (i64)s.x_q;
                i64 dz = (i64)r.z_q - (i64)s.z_q;
                i64 d2 = dx * dx + dz * dz;
                RON_CHECK(d2 >= min_dist_q_sq);
            }
        }
    }
}

RON_TEST(Theatre_ConnectivityLandings) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(32), MiB(1), "theatre_arena").is_ok());

    sim::TheatreData theatre;
    const char* path = get_theatre_path();
    RON_CHECK(theatre.load_file(path, arena).is_ok());

    const auto starts = theatre.starts();
    const u32 logic_n = theatre.logic_n();

    // 1. BFS 4-connected flood fill from start 0 across passable cells
    std::vector<bool> visited(logic_n * logic_n, false);
    std::queue<std::pair<u32, u32>> q;

    u32 s0_x = (starts[0].x_q >> 4) / 8;
    u32 s0_z = (starts[0].z_q >> 4) / 8;
    visited[s0_z * logic_n + s0_x] = true;
    q.push({s0_x, s0_z});

    while (!q.empty()) {
        auto [cx, cz] = q.front();
        q.pop();

        const int dx[] = {1, -1, 0, 0};
        const int dz[] = {0, 0, 1, -1};

        for (int i = 0; i < 4; ++i) {
            int nx = (int)cx + dx[i];
            int nz = (int)cz + dz[i];
            if (nx >= 0 && nx < (int)logic_n && nz >= 0 && nz < (int)logic_n) {
                u32 nidx = nz * logic_n + nx;
                if (!visited[nidx]) {
                    sim::Surface surf = theatre.surface_at_logic(nx, nz);
                    if (surf == sim::Surface::Beach || surf == sim::Surface::Land || surf == sim::Surface::Rough) {
                        visited[nidx] = true;
                        q.push({(u32)nx, (u32)nz});
                    }
                }
            }
        }
    }

    // All 8 starts must be visited
    for (const auto& s : starts) {
        u32 sx = (s.x_q >> 4) / 8;
        u32 sz = (s.z_q >> 4) / 8;
        RON_CHECK(visited[sz * logic_n + sx]);
    }

    // Landings count >= 6
    const auto landings = theatre.landings();
    RON_CHECK(landings.size() >= 6u);
}

} // namespace ron
