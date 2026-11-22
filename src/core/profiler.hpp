#pragma once

#include "types.hpp"
#include "arena.hpp"
#include "result.hpp"
#include "clock.hpp"
#include "hash.hpp"

#include <span>
#include <string_view>

namespace ron {

class JobSystem;

inline constexpr u32 MAX_PROFILE_EVENTS_PER_WORKER = 512;
inline constexpr u32 MAX_PROFILE_STATS = 64;
inline constexpr u32 PROFILE_HISTORY_LEN = 64;

// -------------------------------------------------------------------------
// ProfileEvent: 32 Bytes
// -------------------------------------------------------------------------

struct ProfileEvent {
    const char* name{nullptr};
    u64 name_hash{0};
    u64 start_ns{0};
    u64 end_ns{0};
    u16 depth{0};
    u16 pad0{0};
    u32 pad1{0};
};

// -------------------------------------------------------------------------
// WorkerProfileStream: Cache-Line Aligned Per-Worker Event Stream
// -------------------------------------------------------------------------

struct RON_CACHE_ALIGN WorkerProfileStream {
    u32 count{0};
    u16 current_depth{0};
    u16 pad0{0};
    ProfileEvent events[MAX_PROFILE_EVENTS_PER_WORKER]{};
};

// -------------------------------------------------------------------------
// ProfileScopeStat: Aggregated Telemetry Stats Across Workers
// -------------------------------------------------------------------------

struct ProfileScopeStat {
    const char* name{nullptr};
    u64 name_hash{0};
    u64 last_total_ns{0};
    u64 min_ns{0};
    u64 max_ns{0};
    u64 rolling_avg_ns{0};
    u32 last_call_count{0};
    u32 sample_count{0};
    u64 history_ns[PROFILE_HISTORY_LEN]{};
};

// -------------------------------------------------------------------------
// Profiler: Zero-Contention Telemetry Engine
// FOR TELEMETRY / UI ONLY — NEVER READ BY SIMULATION LOGIC!
// -------------------------------------------------------------------------

class Profiler {
public:
    Profiler() noexcept = default;

    Result<void> init(MemoryArena& permanent_arena, const JobSystem* js) noexcept;
    void begin_frame() noexcept;
    u32 push_scope(const char* name, u64 name_hash) noexcept;
    void pop_scope(u32 slot_handle) noexcept;
    void end_frame() noexcept;

    std::span<const ProfileScopeStat> stats() const noexcept {
        return {stats_, stat_count_};
    }

    const ProfileScopeStat* find_stat(std::string_view name) const noexcept;

private:
    const JobSystem* js_{nullptr};
    WorkerProfileStream* streams_{nullptr};
    u64 frame_start_ns_{0};
    ProfileScopeStat stats_[MAX_PROFILE_STATS]{};
    u32 stat_count_{0};
};

// -------------------------------------------------------------------------
// RAII Profile Scope Guard & Macro
// -------------------------------------------------------------------------

class ProfileScopeGuard {
public:
    ProfileScopeGuard(Profiler* profiler, const char* name, u64 name_hash) noexcept
        : profiler_(profiler), slot_(0xFFFF'FFFF) {
        if (profiler_ != nullptr) {
            slot_ = profiler_->push_scope(name, name_hash);
        }
    }

    ~ProfileScopeGuard() noexcept {
        if (profiler_ != nullptr && slot_ != 0xFFFF'FFFF) {
            profiler_->pop_scope(slot_);
        }
    }

    ProfileScopeGuard(const ProfileScopeGuard&) = delete;
    ProfileScopeGuard& operator=(const ProfileScopeGuard&) = delete;

private:
    Profiler* profiler_{nullptr};
    u32 slot_{0xFFFF'FFFF};
};

consteval u64 fnv1a_64_literal(std::string_view s) noexcept {
    u64 h = 0xcbf29ce484222325ULL;
    for (char c : s) {
        h ^= static_cast<u8>(c);
        h *= 0x100000001b3ULL;
    }
    return h;
}

#define RON_PROFILE_SCOPE(profiler_ptr, literal_name) \
    ::ron::ProfileScopeGuard RON_CONCAT(_ron_prof_guard_, __LINE__)( \
        (profiler_ptr), (literal_name), ::ron::fnv1a_64_literal(literal_name))

} // namespace ron
