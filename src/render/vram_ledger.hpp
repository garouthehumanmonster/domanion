#pragma once

#include "core/types.hpp"
#include <cstddef>

namespace ron::render {

enum class VramCat : u8 {
    TrimTextures,
    TerrainTextures,
    VfxAtlas,
    UiAtlas,
    ModelGeometry,
    VatTextures,
    EntityBuffers,
    TerrainMaps,
    Ocean,
    ShadowMaps,
    HdrTargets,
    PostFx,
    DepthTargets,
    Staging,
    Reserve,
    COUNT
};

constexpr u64 VRAM_TOTAL_CAP = 2304ull << 20;

struct VramCatInfo {
    const char* name;
    u64 cap;
};

extern const VramCatInfo VRAM_CATS[static_cast<usize>(VramCat::COUNT)];

class VramLedger {
public:
    struct Entry {
        bool active{false};
        VramCat cat{VramCat::Reserve};
        u64 bytes{0};
        char label[48]{0};
    };

    static constexpr usize MAX_ENTRIES = 512;

    VramLedger() noexcept;

    bool reserve(VramCat cat, u64 bytes, const char* label, u32& out_id) noexcept;
    void release(u32 id) noexcept;

    u64 used(VramCat cat) const noexcept;
    u64 total_used() const noexcept;

    void freeze() noexcept { frozen_ = true; }
    bool frozen() const noexcept { return frozen_; }

    void dump() const noexcept;

private:
    Entry entries_[MAX_ENTRIES];
    u64 cat_used_[static_cast<usize>(VramCat::COUNT)]{0};
    u64 total_used_{0};
    bool frozen_{false};
};

} // namespace ron::render
