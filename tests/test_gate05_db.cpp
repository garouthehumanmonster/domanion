#include "test_framework.hpp"
#include "core/arena.hpp"
#include "core/hash.hpp"
#include "sim/game_db.hpp"
#include <vector>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

namespace ron {

static const char* get_dbbin_path() {
    static const char* paths[] = {
        RON_SOURCE_DIR "/assets/compiled/game.dbbin",
        "assets/compiled/game.dbbin",
        "../assets/compiled/game.dbbin",
        "../../assets/compiled/game.dbbin"
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

RON_TEST(Db_LoadValidatesHeaderHashAndSections) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(4), MiB(1), "db_test_arena").is_ok());

    db::GameDb db;
    const char* path = get_dbbin_path();
    auto res = db.load_file(path, arena);
    RON_CHECK(res.is_ok());
    RON_CHECK(db.is_valid());

    const auto& hdr = db.header();
    RON_CHECK_EQ(hdr.magic, db::DB_MAGIC);
    RON_CHECK_EQ(hdr.schema_version, db::DB_VERSION);
    RON_CHECK_EQ(hdr.layout_hash, db::LAYOUT_HASH);
    RON_CHECK_EQ(hdr.section_count, 8u);

    RON_CHECK_EQ(db.ages().size(), 9u);
    RON_CHECK_EQ(db.resources().size(), 7u);
    RON_CHECK_EQ(db.sensors().size(), 7u);
    RON_CHECK_EQ(db.weapons().size(), 26u);
    RON_CHECK_EQ(db.units().size(), 29u);
    RON_CHECK_EQ(db.buildings().size(), 15u);
    RON_CHECK_EQ(db.techs().size(), 36u);

    RON_CHECK(hdr.string_table_offset > 0);
    RON_CHECK(hdr.string_table_size > 0);
    RON_CHECK_EQ(db.str(0), "");

    arena.shutdown();
}

RON_TEST(Db_AllCrossReferencesValid) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(4), MiB(1), "db_test_arena").is_ok());

    db::GameDb db;
    RON_CHECK(db.load_file(get_dbbin_path(), arena).is_ok());
    RON_CHECK(db.is_valid());

    // Validate units cross-references
    for (u16 u_idx = 0; u_idx < static_cast<u16>(db.units().size()); ++u_idx) {
        const auto& u = db.units()[u_idx];

        // trained_at building must exist and include this unit
        const auto* bld = db.get_building(u.trained_at);
        RON_CHECK(bld != nullptr);
        if (bld) {
            bool found_in_trains = false;
            for (u32 t = 0; t < 8; ++t) {
                if (bld->trains[t] == u_idx) {
                    found_in_trains = true;
                    break;
                }
            }
            RON_CHECK(found_in_trains);
        }

        // upgrades_to must have higher age and matching line_id
        if (u.upgrades_to != db::NONE16) {
            const auto* next_u = db.get_unit(u.upgrades_to);
            RON_CHECK(next_u != nullptr);
            if (next_u) {
                RON_CHECK(next_u->age_index > u.age_index);
                RON_CHECK_EQ(next_u->line_id, u.line_id);
            }
        }

        // weapons
        if (u.weapon_primary != db::NONE16) {
            RON_CHECK(db.get_weapon(u.weapon_primary) != nullptr);
        }
        if (u.weapon_secondary != db::NONE16) {
            RON_CHECK(db.get_weapon(u.weapon_secondary) != nullptr);
        }

        // sensors
        if (u.sensor_primary != db::NONE16) {
            RON_CHECK(db.get_sensor(u.sensor_primary) != nullptr);
        }
        if (u.sensor_secondary != db::NONE16) {
            RON_CHECK(db.get_sensor(u.sensor_secondary) != nullptr);
        }
    }

    // Validate buildings cross-references
    for (const auto& b : db.buildings()) {
        if (b.gather_resource != db::NONE8) {
            RON_CHECK(db.get_resource(b.gather_resource) != nullptr);
        }
        if (b.weapon_primary != db::NONE16) {
            RON_CHECK(db.get_weapon(b.weapon_primary) != nullptr);
        }
        if (b.sensor_primary != db::NONE16) {
            RON_CHECK(db.get_sensor(b.sensor_primary) != nullptr);
        }
        for (u32 t = 0; t < 8; ++t) {
            if (b.trains[t] != db::NONE16) {
                RON_CHECK(db.get_unit(b.trains[t]) != nullptr);
            }
        }
    }

    // Validate techs cross-references
    for (const auto& tech : db.techs()) {
        if (tech.prereq_tech != db::NONE16) {
            const auto* prq = db.get_tech(tech.prereq_tech);
            RON_CHECK(prq != nullptr);
            if (prq) {
                RON_CHECK(prq->tier < tech.tier);
                RON_CHECK(prq->age_required <= tech.age_required);
            }
        }
    }

    arena.shutdown();
}

RON_TEST(Db_GoldenValuesAndFixedPoint) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(4), MiB(1), "db_test_arena").is_ok());

    db::GameDb db;
    RON_CHECK(db.load_file(get_dbbin_path(), arena).is_ok());
    RON_CHECK(db.is_valid());

    // Citizen
    const auto* citizen = db.find_unit("citizen");
    RON_CHECK(citizen != nullptr);
    if (citizen) {
        RON_CHECK_EQ(db.str(citizen->name_off), "Citizen");
        RON_CHECK_EQ(citizen->age_index, 0u);
        RON_CHECK_EQ(citizen->cost[db::res_id::food], 50u);
        RON_CHECK_EQ(citizen->cost[db::res_id::timber], 0u);
        RON_CHECK_EQ(citizen->max_hp, 50);
        RON_CHECK(citizen->move_speed == fx32::from_int(5));
    }

    // Hoplite
    const auto* hoplite = db.find_unit("hoplite");
    RON_CHECK(hoplite != nullptr);
    if (hoplite) {
        RON_CHECK_EQ(hoplite->cost[db::res_id::food], 50u);
        RON_CHECK_EQ(hoplite->cost[db::res_id::metal], 30u);
        RON_CHECK_EQ(hoplite->weapon_primary, db::weapon_id::hoplite_spear);
    }

    // Hoplite spear weapon
    const auto* spear = db.get_weapon(db::weapon_id::hoplite_spear);
    RON_CHECK(spear != nullptr);
    if (spear) {
        RON_CHECK_EQ(spear->damage, 12);
        const i32 expected_raw_2_5 = static_cast<i32>(2.5 * 65536.0 + 0.5);
        RON_CHECK_EQ(spear->range_max.raw, expected_raw_2_5);
        RON_CHECK_EQ(spear->class_mult_q8[static_cast<u8>(db::UnitClass::Cavalry)], 512u);
    }

    // Autonomous Age 8
    const auto* age8 = db.get_age(db::age_id::autonomous_2026);
    RON_CHECK(age8 != nullptr);
    if (age8) {
        RON_CHECK_EQ(age8->advance_cost[db::res_id::silicon], 120u);
    }

    arena.shutdown();
}

RON_TEST(Db_CorruptionAndVersionRejected) {
    // Read original binary directly to manipulate
    FILE* f = fopen(get_dbbin_path(), "rb");
    RON_CHECK(f != nullptr);
    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<u8> original(sz);
    fread(original.data(), 1, sz, f);
    fclose(f);

    std::vector<u8> test_buf = original;
    db::GameDb db;

    // Test 1: Corrupt magic
    test_buf[0] = 'X';
    auto res1 = db.init_from_blob(test_buf);
    RON_CHECK_EQ(res1.error, ErrorCode::InvalidMagic);

    // Test 2: Corrupt version
    test_buf = original;
    *reinterpret_cast<u32*>(&test_buf[4]) = 999;
    auto res2 = db.init_from_blob(test_buf);
    RON_CHECK_EQ(res2.error, ErrorCode::VersionMismatch);

    // Test 3: Corrupt layout_hash
    test_buf = original;
    *reinterpret_cast<u64*>(&test_buf[8]) ^= 1;
    auto res3 = db.init_from_blob(test_buf);
    RON_CHECK_EQ(res3.error, ErrorCode::VersionMismatch);

    // Test 4: Corrupt single byte in payload (should fail content_xxh64)
    test_buf = original;
    test_buf[250] ^= 0x55;
    auto res4 = db.init_from_blob(test_buf);
    RON_CHECK_EQ(res4.error, ErrorCode::CorruptData);

    // Test 5: Truncated blob
    auto res5 = db.init_from_blob(std::span<const u8>(original.data(), 32));
    RON_CHECK_EQ(res5.error, ErrorCode::BufferTooSmall);
}

RON_TEST(Db_EraInvariants) {
    MemoryArena arena;
    RON_CHECK(arena.init(MiB(4), MiB(1), "db_test_arena").is_ok());

    db::GameDb db;
    RON_CHECK(db.load_file(get_dbbin_path(), arena).is_ok());
    RON_CHECK(db.is_valid());

    // Era resource unlocking laws:
    // Resource unlock_age must be <= unit/building age_index or tech age_required
    for (const auto& u : db.units()) {
        for (u32 r = 0; r < 7; ++r) {
            if (u.cost[r] > 0) {
                const auto* res = db.get_resource(r);
                RON_CHECK(res != nullptr);
                if (res) {
                    RON_CHECK(res->unlock_age <= u.age_index);
                }
            }
        }
    }

    for (const auto& b : db.buildings()) {
        for (u32 r = 0; r < 7; ++r) {
            if (b.cost[r] > 0) {
                const auto* res = db.get_resource(r);
                RON_CHECK(res != nullptr);
                if (res) {
                    RON_CHECK(res->unlock_age <= b.age_index);
                }
            }
        }
    }

    for (const auto& t : db.techs()) {
        for (u32 r = 0; r < 7; ++r) {
            if (t.cost[r] > 0) {
                const auto* res = db.get_resource(r);
                RON_CHECK(res != nullptr);
                if (res) {
                    RON_CHECK(res->unlock_age <= t.age_required);
                }
            }
        }
    }

    // Verify silicon unlock_age is 7
    const auto* silicon = db.get_resource(db::res_id::silicon);
    RON_CHECK(silicon != nullptr);
    if (silicon) {
        RON_CHECK_EQ(silicon->unlock_age, 7u);
    }

    // Verify oil unlock_age is 5
    const auto* oil = db.get_resource(db::res_id::oil);
    RON_CHECK(oil != nullptr);
    if (oil) {
        RON_CHECK_EQ(oil->unlock_age, 5u);
    }

    arena.shutdown();
}

RON_TEST(Db_ZeroCopyAlignmentAndDeterminism) {
    MemoryArena arena1;
    MemoryArena arena2;
    RON_CHECK(arena1.init(MiB(4), MiB(1), "db_arena_1").is_ok());
    RON_CHECK(arena2.init(MiB(4), MiB(1), "db_arena_2").is_ok());

    db::GameDb db1;
    db::GameDb db2;
    const char* path = get_dbbin_path();
    RON_CHECK(db1.load_file(path, arena1).is_ok());
    RON_CHECK(db2.load_file(path, arena2).is_ok());

    // Verify 64-byte alignment of every mapped section in memory
    auto check_aligned_64 = [](const void* ptr) {
        return (reinterpret_cast<uintptr_t>(ptr) % 64) == 0;
    };

    RON_CHECK(check_aligned_64(db1.ages().data()));
    RON_CHECK(check_aligned_64(db1.resources().data()));
    RON_CHECK(check_aligned_64(db1.sensors().data()));
    RON_CHECK(check_aligned_64(db1.weapons().data()));
    RON_CHECK(check_aligned_64(db1.units().data()));
    RON_CHECK(check_aligned_64(db1.buildings().data()));
    RON_CHECK(check_aligned_64(db1.techs().data()));
    RON_CHECK(check_aligned_64(db1.string_table().data()));

    // Verify bit-for-bit identical loads
    RON_CHECK_EQ(db1.header().content_xxh64, db2.header().content_xxh64);
    RON_CHECK_EQ(db1.header().total_file_size, db2.header().total_file_size);

    const u64 hash1 = xxh64(arena1.used_span(), 0);
    const u64 hash2 = xxh64(arena2.used_span(), 0);
    RON_CHECK_EQ(hash1, hash2);

    arena1.shutdown();
    arena2.shutdown();
}

} // namespace ron
