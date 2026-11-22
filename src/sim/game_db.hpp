#pragma once

#include "core/types.hpp"
#include "core/result.hpp"
#include "core/arena.hpp"
#include "core/hash.hpp"
#include "sim/generated_db_types.hpp"

namespace ron::db {

class GameDb {
public:
    GameDb() noexcept = default;

    Result<void> init_from_blob(std::span<const u8> blob) noexcept;
    Result<void> load_file(const char* filepath, MemoryArena& arena) noexcept;

    bool is_valid() const noexcept { return is_valid_; }
    const DbHeader& header() const noexcept { return header_; }

    std::span<const AgeDef> ages() const noexcept { return ages_; }
    std::span<const ResourceDef> resources() const noexcept { return resources_; }
    std::span<const SensorDef> sensors() const noexcept { return sensors_; }
    std::span<const WeaponDef> weapons() const noexcept { return weapons_; }
    std::span<const UnitDef> units() const noexcept { return units_; }
    std::span<const BuildingDef> buildings() const noexcept { return buildings_; }
    std::span<const TechDef> techs() const noexcept { return techs_; }
    std::string_view string_table() const noexcept { return string_table_; }

    std::string_view str(u32 off) const noexcept;

    const AgeDef* get_age(u32 idx) const noexcept;
    const ResourceDef* get_resource(u32 idx) const noexcept;
    const SensorDef* get_sensor(u32 idx) const noexcept;
    const WeaponDef* get_weapon(u32 idx) const noexcept;
    const UnitDef* get_unit(u32 idx) const noexcept;
    const BuildingDef* get_building(u32 idx) const noexcept;
    const TechDef* get_tech(u32 idx) const noexcept;

    const AgeDef* find_age(u64 id_hash) const noexcept;
    const ResourceDef* find_resource(u64 id_hash) const noexcept;
    const SensorDef* find_sensor(u64 id_hash) const noexcept;
    const WeaponDef* find_weapon(u64 id_hash) const noexcept;
    const UnitDef* find_unit(u64 id_hash) const noexcept;
    const BuildingDef* find_building(u64 id_hash) const noexcept;
    const TechDef* find_tech(u64 id_hash) const noexcept;

    const AgeDef* find_age(std::string_view id_str) const noexcept { return find_age(fnv1a_64(id_str)); }
    const ResourceDef* find_resource(std::string_view id_str) const noexcept { return find_resource(fnv1a_64(id_str)); }
    const SensorDef* find_sensor(std::string_view id_str) const noexcept { return find_sensor(fnv1a_64(id_str)); }
    const WeaponDef* find_weapon(std::string_view id_str) const noexcept { return find_weapon(fnv1a_64(id_str)); }
    const UnitDef* find_unit(std::string_view id_str) const noexcept { return find_unit(fnv1a_64(id_str)); }
    const BuildingDef* find_building(std::string_view id_str) const noexcept { return find_building(fnv1a_64(id_str)); }
    const TechDef* find_tech(std::string_view id_str) const noexcept { return find_tech(fnv1a_64(id_str)); }

private:
    bool is_valid_{false};
    DbHeader header_{};
    std::span<const AgeDef> ages_{};
    std::span<const ResourceDef> resources_{};
    std::span<const SensorDef> sensors_{};
    std::span<const WeaponDef> weapons_{};
    std::span<const UnitDef> units_{};
    std::span<const BuildingDef> buildings_{};
    std::span<const TechDef> techs_{};
    std::string_view string_table_{};
};

} // namespace ron::db
