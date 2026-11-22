#pragma once

#include "types.hpp"
#include "result.hpp"
#include "assert_log.hpp"
#include <algorithm>

namespace ron {

class MemoryArena {
public:
    MemoryArena() noexcept = default;
    ~MemoryArena() noexcept;

    MemoryArena(const MemoryArena&) = delete;
    MemoryArena& operator=(const MemoryArena&) = delete;

    MemoryArena(MemoryArena&& other) noexcept;
    MemoryArena& operator=(MemoryArena&& other) noexcept;

    Result<void> init(usize reserve_bytes, usize initial_commit_bytes, const char* name = "unnamed") noexcept;
    void shutdown() noexcept;

    void* alloc_raw(usize size, usize alignment = CACHE_LINE_BYTES) noexcept;

    template <typename T>
    T* alloc(usize count = 1) noexcept {
        static_assert(std::is_trivially_destructible_v<T>, "T must be trivially destructible in MemoryArena");
        if (count == 0) {
            return nullptr;
        }
        const usize align = (count > 1) ? std::max(alignof(T), CACHE_LINE_BYTES) : alignof(T);
        return static_cast<T*>(alloc_raw(sizeof(T) * count, align));
    }

    template <typename T>
    std::span<T> alloc_span(usize count) noexcept {
        if (count == 0) {
            return std::span<T>{};
        }
        T* ptr = alloc<T>(count);
        return std::span<T>{ptr, count};
    }

    usize save_marker() const noexcept { return offset_; }
    void restore_marker(usize marker) noexcept;
    void reset(bool decommit_excess = false) noexcept;

    usize used_bytes() const noexcept { return offset_; }
    usize committed_bytes() const noexcept { return committed_; }
    usize reserved_bytes() const noexcept { return reserved_; }
    const char* name() const noexcept { return debug_name_; }

    std::span<const u8> used_span() const noexcept {
        return (base_ && offset_) ? std::span<const u8>{base_, offset_} : std::span<const u8>{};
    }

    std::span<u8> used_span_mut() noexcept {
        return (base_ && offset_) ? std::span<u8>{base_, offset_} : std::span<u8>{};
    }

    u8* base() noexcept { return base_; }
    const u8* base() const noexcept { return base_; }

private:
    u8* base_{nullptr};
    usize reserved_{0};
    usize committed_{0};
    usize offset_{0};
    const char* debug_name_{"unnamed"};
};

struct ArenaTempScope {
    MemoryArena& arena;
    usize marker;

    explicit ArenaTempScope(MemoryArena& a) noexcept
        : arena(a), marker(a.save_marker()) {}

    ~ArenaTempScope() noexcept {
        arena.restore_marker(marker);
    }

    ArenaTempScope(const ArenaTempScope&) = delete;
    ArenaTempScope& operator=(const ArenaTempScope&) = delete;
};

struct EngineMemory {
    MemoryArena permanent;
    MemoryArena match;
    MemoryArena render_staging;
    MemoryArena worker_scratch[MAX_WORKER_THREADS];

    Result<void> init_all(u32 worker_count = MAX_WORKER_THREADS) noexcept;
    void shutdown_all() noexcept;
};

} // namespace ron
