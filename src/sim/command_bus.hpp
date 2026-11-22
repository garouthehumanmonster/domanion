#pragma once

#include "entity_handle.hpp"
#include "core/arena.hpp"
#include "core/result.hpp"
#include "core/compression.hpp"
#include <span>

namespace ron {

// -------------------------------------------------------------------------
// Command Types
// -------------------------------------------------------------------------
enum class CommandType : u8 {
    Nop               = 0,
    SpawnEntity       = 1,
    DestroyEntity     = 2,
    Move              = 3,
    AttackMove        = 4,
    AttackTarget      = 5,
    Stop              = 6,
    SetStance         = 7,
    ConstructBuilding = 8,
    TrainUnit         = 9,
    ResearchTech      = 10,
    AdvanceAge        = 11,
    StrategicPower    = 12,
};

namespace CommandFlags {
    inline constexpr u8 None             = 0;
    inline constexpr u8 ShiftQueueAppend = 1u << 0;
    inline constexpr u8 FormationMove    = 1u << 1;
}

// -------------------------------------------------------------------------
// SimCommand: 32 Bytes Exact POD
// -------------------------------------------------------------------------
struct SimCommand {
    u32          execute_tick{0};
    u32          sequence_id{0};
    EntityHandle subject{NULL_ENTITY};
    EntityHandle target{NULL_ENTITY};
    Vec2fx       target_xz{};
    u16          param_id{0};
    u8           player_id{0};
    CommandType  type{CommandType::Nop};
    u8           flags{0};
    u8           sub_param{0};
    u16          reserved_pad{0};
};

static_assert(sizeof(SimCommand) == 32);
static_assert(std::is_trivially_copyable_v<SimCommand>);
static_assert(std::has_unique_object_representations_v<SimCommand>);

// -------------------------------------------------------------------------
// Replay Header: 32 Bytes Exact
// -------------------------------------------------------------------------
struct ReplayHeader {
    u32 magic{0x36325052u}; // 'RP26'
    u32 version{1u};
    u64 match_seed{0};
    u32 total_ticks{0};
    u32 total_commands{0};
    u64 final_state_xxh64{0};
};

static_assert(sizeof(ReplayHeader) == 32);
static_assert(std::is_trivially_copyable_v<ReplayHeader>);
static_assert(std::has_unique_object_representations_v<ReplayHeader>);

// -------------------------------------------------------------------------
// CommandBus: Ingestion, Deterministic Sorting & Replay Serialization
// -------------------------------------------------------------------------
class CommandBus {
public:
    static constexpr u32 MAX_PENDING_COMMANDS = 32768;
    static constexpr u32 MAX_REPLAY_COMMANDS  = 131072;

    CommandBus() noexcept = default;

    Result<void> init(MemoryArena& match_arena) noexcept;

    Result<void> enqueue(const SimCommand& cmd) noexcept;
    std::span<const SimCommand> extract_sorted_for_tick(u32 tick, MemoryArena& scratch) noexcept;

    Result<std::span<u8>> serialize_replay_lz4(
        u64 match_seed,
        u32 total_ticks,
        u64 final_state_hash,
        MemoryArena& dst_arena,
        MemoryArena& scratch) const noexcept;

    Result<ReplayHeader> load_replay_lz4(
        std::span<const u8> compressed_blob,
        MemoryArena& scratch) noexcept;

    u32 pending_count() const noexcept { return pending_count_; }
    u32 history_count() const noexcept { return history_count_; }
    std::span<const SimCommand> history() const noexcept { return {history_, history_count_}; }

private:
    SimCommand* pending_{nullptr};
    u32 pending_count_{0};

    SimCommand* history_{nullptr};
    u32 history_count_{0};

    u32 next_sequence_id_{0};
};

} // namespace ron
