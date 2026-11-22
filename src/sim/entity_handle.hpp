#pragma once

#include "core/types.hpp"
#include "core/fixed_math.hpp"
#include "core/fixed_geom.hpp"

namespace ron {

inline constexpr u32 MAX_ENTITIES = 32768;
inline constexpr u32 INVALID_DENSE_IDX = 0xFFFF'FFFFu;
inline constexpr u8 MAX_PLAYERS = 8;
inline constexpr u8 GAIA_PLAYER_ID = 8;
inline constexpr i32 SIM_TICKS_PER_SECOND = 20;

// -------------------------------------------------------------------------
// Exact 20 Hz Odd-Symmetric Velocity/Rate Integrator
// -------------------------------------------------------------------------
constexpr fx32 integrate_dt_20hz(fx32 rate_per_sec) noexcept {
    const i64 r = rate_per_sec.raw;
    const i64 mag = (r < 0) ? -r : r;
    const i64 step = (mag + 10) / 20;
    return fx32::from_raw((r < 0) ? -step : step);
}

// -------------------------------------------------------------------------
// EntityHandle: 32-Bit Generational Handle (20-Bit Slot + 12-Bit Generation)
// -------------------------------------------------------------------------
struct EntityHandle {
    u32 raw{0};

    static constexpr EntityHandle null() noexcept { return EntityHandle{0}; }
    static constexpr EntityHandle pack(u32 slot_idx, u16 gen_12bit) noexcept {
        return EntityHandle{(slot_idx & 0x000F'FFFFu) | (static_cast<u32>(gen_12bit & 0x0FFFu) << 20)};
    }

    constexpr u32 slot() const noexcept { return raw & 0x000F'FFFFu; }
    constexpr u16 generation() const noexcept { return static_cast<u16>(raw >> 20); }
    constexpr bool is_null() const noexcept { return raw == 0; }
    constexpr auto operator<=>(const EntityHandle&) const noexcept = default;
};

inline constexpr EntityHandle NULL_ENTITY = EntityHandle::null();

static_assert(sizeof(EntityHandle) == 4);
static_assert(std::is_trivially_copyable_v<EntityHandle>);
static_assert(std::has_unique_object_representations_v<EntityHandle>);

// -------------------------------------------------------------------------
// Movement Domains
// -------------------------------------------------------------------------
enum class MovementDomain : u8 {
    Land_Infantry           = 0,
    Land_Heavy              = 1,
    Amphibious              = 2,
    Naval_Coastal           = 3,
    Naval_Deep              = 4,
    Air_Low_Rotary_Swarm    = 5,
    Air_High_Sortie         = 6,
    Ballistic_Exo           = 7,
    Static_Building         = 8,
};

// -------------------------------------------------------------------------
// Order Types
// -------------------------------------------------------------------------
enum class OrderType : u8 {
    Idle          = 0,
    Move          = 1,
    AttackMove    = 2,
    AttackTarget  = 3,
    HoldPosition  = 4,
    Construct     = 5,
    Gather        = 6,
};

// -------------------------------------------------------------------------
// Stance Types
// -------------------------------------------------------------------------
enum class StanceType : u8 {
    Aggressive     = 0,
    Defensive      = 1,
    HoldGround     = 2,
    ShootAndScoot  = 3,
};

// -------------------------------------------------------------------------
// Entity Flags Bitmask
// -------------------------------------------------------------------------
namespace EntityFlags {
    inline constexpr u32 Alive             = 1u << 0;
    inline constexpr u32 PendingDestroy    = 1u << 1;
    inline constexpr u32 Moving            = 1u << 2;
    inline constexpr u32 Attacking         = 1u << 3;
    inline constexpr u32 UnderConstruction = 1u << 4;
    inline constexpr u32 Garrisoned        = 1u << 5;
    inline constexpr u32 StealthActive     = 1u << 6;
    inline constexpr u32 RadarActive       = 1u << 7;
}

} // namespace ron
