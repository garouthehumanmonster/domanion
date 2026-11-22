#pragma once

#include "entity_handle.hpp"
#include "entity_soa.hpp"
#include "command_bus.hpp"
#include "core/prng.hpp"
#include "core/task_graph.hpp"
#include "core/job_system.hpp"
#include "core/profiler.hpp"
#include "core/hash.hpp"

namespace ron {

// -------------------------------------------------------------------------
// SimState: Pointer-Free, ASLR-Invariant POD Root State
// -------------------------------------------------------------------------
struct RON_CACHE_ALIGN SimState {
    u32              magic{0x534D3236u}; // 'SM26'
    u32              current_tick{0};
    u64              match_seed{0};
    DeterministicRng tick_rng{1};
    u32              total_spawned{0};
    u32              total_destroyed{0};
    u8               pad_header[24]{};
    EntityStore      entities;
};

static_assert(std::is_trivially_copyable_v<SimState>);

// -------------------------------------------------------------------------
// SimWorld: 20 Hz Deterministic Simulation Manager
// -------------------------------------------------------------------------
class SimWorld {
public:
    SimWorld() noexcept = default;

    Result<void> init(MemoryArena& match_arena, u64 match_seed) noexcept;

    void step_tick(JobSystem& js, Profiler* profiler = nullptr) noexcept;
    u64 compute_state_hash() const noexcept;

    Result<std::span<u8>> save_snapshot_lz4(MemoryArena& dst_arena) const noexcept;
    Result<void> load_snapshot_lz4(std::span<const u8> compressed_snapshot, MemoryArena& scratch) noexcept;

    SimState& state() noexcept { return *state_; }
    const SimState& state() const noexcept { return *state_; }

    EntityStore& entities() noexcept { return state_->entities; }
    const EntityStore& entities() const noexcept { return state_->entities; }

    CommandBus& command_bus() noexcept { return cmd_bus_; }
    const CommandBus& command_bus() const noexcept { return cmd_bus_; }

private:
    SimState* state_{nullptr};
    CommandBus cmd_bus_{};
    TaskGraph tick_graph_{};
};

} // namespace ron
