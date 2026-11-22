#include "sim/game_db.hpp"
#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ron::db {

Result<void> GameDb::init_from_blob(std::span<const u8> blob) noexcept {
    is_valid_ = false;
    constexpr usize min_header_size = sizeof(DbHeader) + 8 * sizeof(DbSectionEntry);
    if (blob.size() < min_header_size) {
        return Result<void>::err(ErrorCode::BufferTooSmall);
    }

    std::memcpy(&header_, blob.data(), sizeof(DbHeader));

    if (header_.magic != DB_MAGIC) {
        return Result<void>::err(ErrorCode::InvalidMagic);
    }
    if (header_.schema_version != DB_VERSION) {
        return Result<void>::err(ErrorCode::VersionMismatch);
    }
    if (header_.layout_hash != LAYOUT_HASH) {
        return Result<void>::err(ErrorCode::VersionMismatch);
    }
    if (header_.section_count != 8) {
        return Result<void>::err(ErrorCode::CorruptData);
    }
    if (header_.total_file_size > blob.size()) {
        return Result<void>::err(ErrorCode::CorruptData);
    }

    // Verify content_xxh64 over everything after the 64-byte header
    const u64 computed_content_hash = xxh64(blob.subspan(sizeof(DbHeader), header_.total_file_size - sizeof(DbHeader)), 0);
    if (computed_content_hash != header_.content_xxh64) {
        return Result<void>::err(ErrorCode::CorruptData);
    }

    // Parse section table (8 entries starting at byte 64)
    const DbSectionEntry* entries = reinterpret_cast<const DbSectionEntry*>(blob.data() + sizeof(DbHeader));

    const u32 expected_strides[8] = {
        sizeof(AgeDef),
        sizeof(ResourceDef),
        sizeof(SensorDef),
        sizeof(WeaponDef),
        sizeof(UnitDef),
        sizeof(BuildingDef),
        sizeof(TechDef),
        1
    };

    for (u32 s = 0; s < 8; ++s) {
        const DbSectionEntry& e = entries[s];
        if (e.stride != expected_strides[s]) {
            return Result<void>::err(ErrorCode::CorruptData);
        }
        if ((e.offset % 64) != 0) { // strictly 64-byte aligned
            return Result<void>::err(ErrorCode::CorruptData);
        }
        const u64 sec_end = static_cast<u64>(e.offset) + static_cast<u64>(e.count) * static_cast<u64>(e.stride);
        if (sec_end > header_.total_file_size) {
            return Result<void>::err(ErrorCode::OutOfBounds);
        }
    }

    // Map spans
    ages_ = std::span<const AgeDef>(reinterpret_cast<const AgeDef*>(blob.data() + entries[0].offset), entries[0].count);
    resources_ = std::span<const ResourceDef>(reinterpret_cast<const ResourceDef*>(blob.data() + entries[1].offset), entries[1].count);
    sensors_ = std::span<const SensorDef>(reinterpret_cast<const SensorDef*>(blob.data() + entries[2].offset), entries[2].count);
    weapons_ = std::span<const WeaponDef>(reinterpret_cast<const WeaponDef*>(blob.data() + entries[3].offset), entries[3].count);
    units_ = std::span<const UnitDef>(reinterpret_cast<const UnitDef*>(blob.data() + entries[4].offset), entries[4].count);
    buildings_ = std::span<const BuildingDef>(reinterpret_cast<const BuildingDef*>(blob.data() + entries[5].offset), entries[5].count);
    techs_ = std::span<const TechDef>(reinterpret_cast<const TechDef*>(blob.data() + entries[6].offset), entries[6].count);

    string_table_ = std::string_view(reinterpret_cast<const char*>(blob.data() + entries[7].offset), entries[7].count);

    is_valid_ = true;
    return Result<void>::ok();
}

Result<void> GameDb::load_file(const char* filepath, MemoryArena& arena) noexcept {
#ifdef _WIN32
    HANDLE hFile = CreateFileA(filepath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return Result<void>::err(ErrorCode::IoError);
    }

    LARGE_INTEGER liSize;
    if (!GetFileSizeEx(hFile, &liSize)) {
        CloseHandle(hFile);
        return Result<void>::err(ErrorCode::IoError);
    }

    const usize fileSize = static_cast<usize>(liSize.QuadPart);
    u8* buf = static_cast<u8*>(arena.alloc_raw(fileSize, CACHE_LINE_BYTES));
    if (!buf) {
        CloseHandle(hFile);
        return Result<void>::err(ErrorCode::OutOfMemory);
    }

    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buf, static_cast<DWORD>(fileSize), &bytesRead, NULL) || bytesRead != fileSize) {
        CloseHandle(hFile);
        return Result<void>::err(ErrorCode::IoError);
    }

    CloseHandle(hFile);
    return init_from_blob(std::span<const u8>(buf, fileSize));
#else
    FILE* fp = std::fopen(filepath, "rb");
    if (!fp) {
        return Result<void>::err(ErrorCode::IoError);
    }
    std::fseek(fp, 0, SEEK_END);
    const long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (sz < 0) {
        std::fclose(fp);
        return Result<void>::err(ErrorCode::IoError);
    }
    const usize fileSize = static_cast<usize>(sz);
    u8* buf = static_cast<u8*>(arena.alloc_raw(fileSize, CACHE_LINE_BYTES));
    if (!buf) {
        std::fclose(fp);
        return Result<void>::err(ErrorCode::OutOfMemory);
    }
    const usize read_bytes = std::fread(buf, 1, fileSize, fp);
    std::fclose(fp);
    if (read_bytes != fileSize) {
        return Result<void>::err(ErrorCode::IoError);
    }
    return init_from_blob(std::span<const u8>(buf, fileSize));
#endif
}

std::string_view GameDb::str(u32 off) const noexcept {
    if (!is_valid_ || off >= string_table_.size()) {
        return "";
    }
    const char* start = string_table_.data() + off;
    usize len = 0;
    while ((off + len) < string_table_.size() && start[len] != '\0') {
        ++len;
    }
    return std::string_view(start, len);
}

const AgeDef* GameDb::get_age(u32 idx) const noexcept {
    if (idx < ages_.size()) return &ages_[idx];
    return nullptr;
}

const ResourceDef* GameDb::get_resource(u32 idx) const noexcept {
    if (idx < resources_.size()) return &resources_[idx];
    return nullptr;
}

const SensorDef* GameDb::get_sensor(u32 idx) const noexcept {
    if (idx < sensors_.size()) return &sensors_[idx];
    return nullptr;
}

const WeaponDef* GameDb::get_weapon(u32 idx) const noexcept {
    if (idx < weapons_.size()) return &weapons_[idx];
    return nullptr;
}

const UnitDef* GameDb::get_unit(u32 idx) const noexcept {
    if (idx < units_.size()) return &units_[idx];
    return nullptr;
}

const BuildingDef* GameDb::get_building(u32 idx) const noexcept {
    if (idx < buildings_.size()) return &buildings_[idx];
    return nullptr;
}

const TechDef* GameDb::get_tech(u32 idx) const noexcept {
    if (idx < techs_.size()) return &techs_[idx];
    return nullptr;
}

const AgeDef* GameDb::find_age(u64 id_hash) const noexcept {
    for (const auto& a : ages_) {
        if (a.id_hash == id_hash) return &a;
    }
    return nullptr;
}

const ResourceDef* GameDb::find_resource(u64 id_hash) const noexcept {
    for (const auto& r : resources_) {
        if (r.id_hash == id_hash) return &r;
    }
    return nullptr;
}

const SensorDef* GameDb::find_sensor(u64 id_hash) const noexcept {
    for (const auto& s : sensors_) {
        if (s.id_hash == id_hash) return &s;
    }
    return nullptr;
}

const WeaponDef* GameDb::find_weapon(u64 id_hash) const noexcept {
    for (const auto& w : weapons_) {
        if (w.id_hash == id_hash) return &w;
    }
    return nullptr;
}

const UnitDef* GameDb::find_unit(u64 id_hash) const noexcept {
    for (const auto& u : units_) {
        if (u.id_hash == id_hash) return &u;
    }
    return nullptr;
}

const BuildingDef* GameDb::find_building(u64 id_hash) const noexcept {
    for (const auto& b : buildings_) {
        if (b.id_hash == id_hash) return &b;
    }
    return nullptr;
}

const TechDef* GameDb::find_tech(u64 id_hash) const noexcept {
    for (const auto& t : techs_) {
        if (t.id_hash == id_hash) return &t;
    }
    return nullptr;
}

} // namespace ron::db
