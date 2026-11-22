#include "render/vram_ledger.hpp"
#include "core/assert_log.hpp"
#include <cstdio>
#include <cstring>

namespace ron::render {

constexpr VramCatInfo K_VRAM_CATS[static_cast<usize>(VramCat::COUNT)] = {
    {"TrimTextures",    64ull  << 20},
    {"TerrainTextures", 64ull  << 20},
    {"VfxAtlas",        32ull  << 20},
    {"UiAtlas",         16ull  << 20},
    {"ModelGeometry",   128ull << 20},
    {"VatTextures",     32ull  << 20},
    {"EntityBuffers",   192ull << 20},
    {"TerrainMaps",     192ull << 20},
    {"Ocean",           64ull  << 20},
    {"ShadowMaps",      320ull << 20},
    {"HdrTargets",      512ull << 20},
    {"PostFx",          256ull << 20},
    {"DepthTargets",    128ull << 20},
    {"Staging",         64ull  << 20},
    {"Reserve",         240ull << 20},
};

static constexpr u64 calculate_sum() {
    u64 sum = 0;
    for (usize i = 0; i < static_cast<usize>(VramCat::COUNT); ++i) {
        sum += K_VRAM_CATS[i].cap;
    }
    return sum;
}
static_assert(calculate_sum() == VRAM_TOTAL_CAP, "VRAM category caps must sum exactly to 2304 MiB");

const VramCatInfo VRAM_CATS[static_cast<usize>(VramCat::COUNT)] = {
    K_VRAM_CATS[0],  K_VRAM_CATS[1],  K_VRAM_CATS[2],  K_VRAM_CATS[3],  K_VRAM_CATS[4],
    K_VRAM_CATS[5],  K_VRAM_CATS[6],  K_VRAM_CATS[7],  K_VRAM_CATS[8],  K_VRAM_CATS[9],
    K_VRAM_CATS[10], K_VRAM_CATS[11], K_VRAM_CATS[12], K_VRAM_CATS[13], K_VRAM_CATS[14],
};

VramLedger::VramLedger() noexcept {
    for (usize i = 0; i < MAX_ENTRIES; ++i) {
        entries_[i] = Entry{};
    }
}

bool VramLedger::reserve(VramCat cat, u64 bytes, const char* label, u32& out_id) noexcept {
    out_id = 0;

    if (frozen_) {
        RON_LOG_ERROR("VRAM reserve failed: ledger is frozen (label='%s', bytes=%llu)",
                      label ? label : "unnamed", static_cast<unsigned long long>(bytes));
        return false;
    }

    if (bytes == 0) {
        RON_LOG_ERROR("VRAM reserve failed: bytes == 0 (label='%s')", label ? label : "unnamed");
        return false;
    }

    usize cat_idx = static_cast<usize>(cat);
    if (cat_idx >= static_cast<usize>(VramCat::COUNT)) {
        RON_LOG_ERROR("VRAM reserve failed: invalid category %u", static_cast<unsigned>(cat_idx));
        return false;
    }

    u64 cat_cap = VRAM_CATS[cat_idx].cap;
    if (bytes > cat_cap || cat_used_[cat_idx] > cat_cap - bytes) {
        RON_LOG_ERROR("VRAM reserve failed: category '%s' cap exceeded (used=%llu, req=%llu, cap=%llu)",
                      VRAM_CATS[cat_idx].name,
                      static_cast<unsigned long long>(cat_used_[cat_idx]),
                      static_cast<unsigned long long>(bytes),
                      static_cast<unsigned long long>(cat_cap));
        return false;
    }

    if (bytes > VRAM_TOTAL_CAP || total_used_ > VRAM_TOTAL_CAP - bytes) {
        RON_LOG_ERROR("VRAM reserve failed: total VRAM cap exceeded (total_used=%llu, req=%llu, cap=%llu)",
                      static_cast<unsigned long long>(total_used_),
                      static_cast<unsigned long long>(bytes),
                      static_cast<unsigned long long>(VRAM_TOTAL_CAP));
        return false;
    }

    for (usize i = 0; i < MAX_ENTRIES; ++i) {
        if (!entries_[i].active) {
            entries_[i].active = true;
            entries_[i].cat = cat;
            entries_[i].bytes = bytes;
            if (label) {
                std::strncpy(entries_[i].label, label, sizeof(entries_[i].label) - 1);
                entries_[i].label[sizeof(entries_[i].label) - 1] = '\0';
            } else {
                entries_[i].label[0] = '\0';
            }

            cat_used_[cat_idx] += bytes;
            total_used_ += bytes;
            out_id = static_cast<u32>(i + 1); // 1-based IDs
            return true;
        }
    }

    RON_LOG_ERROR("VRAM reserve failed: ledger table full (%u entries)", static_cast<unsigned>(MAX_ENTRIES));
    return false;
}

void VramLedger::release(u32 id) noexcept {
    if (id == 0 || id > MAX_ENTRIES) {
        return;
    }

    usize idx = static_cast<usize>(id - 1);
    if (!entries_[idx].active) {
        return;
    }

    usize cat_idx = static_cast<usize>(entries_[idx].cat);
    u64 bytes = entries_[idx].bytes;

    entries_[idx].active = false;
    entries_[idx].bytes = 0;
    entries_[idx].label[0] = '\0';

    if (cat_used_[cat_idx] >= bytes) {
        cat_used_[cat_idx] -= bytes;
    } else {
        cat_used_[cat_idx] = 0;
    }

    if (total_used_ >= bytes) {
        total_used_ -= bytes;
    } else {
        total_used_ = 0;
    }
}

u64 VramLedger::used(VramCat cat) const noexcept {
    usize cat_idx = static_cast<usize>(cat);
    if (cat_idx >= static_cast<usize>(VramCat::COUNT)) return 0;
    return cat_used_[cat_idx];
}

u64 VramLedger::total_used() const noexcept {
    return total_used_;
}

void VramLedger::dump() const noexcept {
    RON_LOG_INFO("=================== VRAM LEDGER DUMP ===================");
    RON_LOG_INFO("Total VRAM used: %.2f / %.2f MiB",
                 static_cast<double>(total_used_) / (1024.0 * 1024.0),
                 static_cast<double>(VRAM_TOTAL_CAP) / (1024.0 * 1024.0));

    for (usize c = 0; c < static_cast<usize>(VramCat::COUNT); ++c) {
        RON_LOG_INFO("  Category %-16s: %8.2f / %8.2f MiB",
                     VRAM_CATS[c].name,
                     static_cast<double>(cat_used_[c]) / (1024.0 * 1024.0),
                     static_cast<double>(VRAM_CATS[c].cap) / (1024.0 * 1024.0));
    }

    for (usize i = 0; i < MAX_ENTRIES; ++i) {
        if (entries_[i].active) {
            RON_LOG_INFO("    [%3u] %-16s | %8.2f KiB | %s",
                         static_cast<unsigned>(i + 1),
                         VRAM_CATS[static_cast<usize>(entries_[i].cat)].name,
                         static_cast<double>(entries_[i].bytes) / 1024.0,
                         entries_[i].label);
        }
    }
    RON_LOG_INFO("========================================================");
}

} // namespace ron::render
