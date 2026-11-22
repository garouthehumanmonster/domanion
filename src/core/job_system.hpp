#pragma once

#include "types.hpp"
#include "arena.hpp"
#include "result.hpp"
#include "assert_log.hpp"
#include "clock.hpp"

#include <atomic>
#include <algorithm>
#include <span>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#include <emmintrin.h>

namespace ron {

// -------------------------------------------------------------------------
// Cache-Line Isolated Job Counter (64 Bytes)
// -------------------------------------------------------------------------

struct RON_CACHE_ALIGN JobCounter {
    std::atomic<i32> pending{0};
    u8 pad[CACHE_LINE_BYTES - sizeof(std::atomic<i32>)]{};

    constexpr JobCounter() noexcept = default;
    explicit constexpr JobCounter(i32 count) noexcept : pending(count), pad{} {}

    JobCounter(const JobCounter&) = delete;
    JobCounter& operator=(const JobCounter&) = delete;
};

static_assert(sizeof(JobCounter) == CACHE_LINE_BYTES);

// -------------------------------------------------------------------------
// Job Callable & 64-Byte POD Representation
// -------------------------------------------------------------------------

using JobEntryFn = void(*)(void* payload, u32 worker_idx) noexcept;

inline constexpr usize JOB_PAYLOAD_BYTES = 48;

struct RON_CACHE_ALIGN Job {
    JobEntryFn  fn{nullptr};                  // 8 bytes
    JobCounter* counter{nullptr};             // 8 bytes (optional; nullptr if detached)
    u8          payload[JOB_PAYLOAD_BYTES]{}; // 48 bytes inline POD callable storage
};

static_assert(sizeof(Job) == 64 && std::is_trivially_copyable_v<Job>);

// -------------------------------------------------------------------------
// Cache-Line Isolated Work-Stealing Deque (Fixed Ring Buffer 1024)
// -------------------------------------------------------------------------

class alignas(CACHE_LINE_BYTES) WorkStealingDeque {
public:
    static constexpr u32 DEQUE_CAPACITY = 1024;
    static constexpr u32 DEQUE_MASK = DEQUE_CAPACITY - 1;

    WorkStealingDeque() noexcept {
        InitializeSRWLock(&lock_);
    }

    bool push_bottom(const Job& job) noexcept {
        AcquireSRWLockExclusive(&lock_);
        if ((bottom_ - top_) >= static_cast<i32>(DEQUE_CAPACITY)) {
            ReleaseSRWLockExclusive(&lock_);
            return false;
        }
        buffer_[bottom_ & DEQUE_MASK] = job;
        bottom_++;
        ReleaseSRWLockExclusive(&lock_);
        return true;
    }

    bool pop_bottom(Job& out_job) noexcept {
        AcquireSRWLockExclusive(&lock_);
        if (bottom_ <= top_) {
            ReleaseSRWLockExclusive(&lock_);
            return false;
        }
        bottom_--;
        out_job = buffer_[bottom_ & DEQUE_MASK];
        ReleaseSRWLockExclusive(&lock_);
        return true;
    }

    bool try_steal_top(Job& out_job) noexcept {
        if (!TryAcquireSRWLockExclusive(&lock_)) {
            return false;
        }
        if (top_ >= bottom_) {
            ReleaseSRWLockExclusive(&lock_);
            return false;
        }
        out_job = buffer_[top_ & DEQUE_MASK];
        top_++;
        ReleaseSRWLockExclusive(&lock_);
        return true;
    }

    i32 count() const noexcept {
        return bottom_ - top_;
    }

private:
    RON_CACHE_ALIGN SRWLOCK lock_{};
    i32 top_{0};
    i32 bottom_{0};
    Job buffer_[DEQUE_CAPACITY]{};
};

// -------------------------------------------------------------------------
// JobSystem: Multi-Core Work-Stealing Scheduler
// -------------------------------------------------------------------------

class JobSystem {
public:
    JobSystem() noexcept;
    ~JobSystem() noexcept;

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    Result<void> init(u32 requested_workers, EngineMemory* engine_mem) noexcept;
    void shutdown() noexcept;

    u32 worker_count() const noexcept { return worker_count_; }
    u32 current_worker_index() const noexcept;
    MemoryArena& current_scratch_arena() noexcept;
    void reset_all_scratch_arenas() noexcept;

    void decrement_counter(JobCounter& counter) noexcept;
    bool execute_one_available_job(u32 my_idx) noexcept;

    template <typename Callable>
    void submit(JobCounter* counter, Callable&& fn) noexcept {
        using FnT = std::decay_t<Callable>;
        static_assert(sizeof(FnT) <= JOB_PAYLOAD_BYTES && std::is_trivially_copyable_v<FnT>,
                      "Job payload must be <= 48B POD");

        if (counter != nullptr) {
            counter->pending.fetch_add(1, std::memory_order_relaxed);
        }

        Job job{};
        job.counter = counter;
        if constexpr (std::is_invocable_v<FnT, u32>) {
            job.fn = [](void* payload, u32 wid) noexcept {
                auto* p = reinterpret_cast<FnT*>(payload);
                (*p)(wid);
            };
        } else {
            job.fn = [](void* payload, u32) noexcept {
                auto* p = reinterpret_cast<FnT*>(payload);
                (*p)();
            };
        }
        std::memcpy(job.payload, &fn, sizeof(FnT));

        const u32 my_idx = current_worker_index();
        if (!deques_[my_idx].push_bottom(job)) {
            // Graceful saturation fallback: run inline on current thread
            job.fn(job.payload, my_idx);
            if (counter != nullptr) {
                decrement_counter(*counter);
            }
            return;
        }

        approx_queued_jobs_.fetch_add(1, std::memory_order_relaxed);
        AcquireSRWLockExclusive(&wake_lock_);
        ReleaseSRWLockExclusive(&wake_lock_);
        WakeConditionVariable(&wake_cv_);
    }

    void wait(JobCounter& counter) noexcept;

    template <typename RangeFn>
    void parallel_for_deterministic(u32 total_count, u32 grain_size, RangeFn&& fn) noexcept {
        if (total_count == 0) return;
        const u32 safe_grain = (grain_size == 0) ? 1u : grain_size;
        const u32 chunk_count = (total_count + safe_grain - 1u) / safe_grain;

        if (worker_count_ == 1 || chunk_count == 1) {
            for (u32 c = 0; c < chunk_count; ++c) {
                ArenaTempScope scope(current_scratch_arena());
                const u32 begin = c * safe_grain;
                const u32 end = std::min(begin + safe_grain, total_count);
                fn(begin, end, c, current_scratch_arena());
            }
            return;
        }

        using DecayedFn = std::decay_t<RangeFn>;
        struct SharedState {
            const DecayedFn* fn_ptr;
            JobSystem* js;
            u32 total;
            u32 grain;
            u32 chunks;
            RON_CACHE_ALIGN std::atomic<u32> next_chunk{0};
        };

        SharedState state{
            .fn_ptr = &fn,
            .js = this,
            .total = total_count,
            .grain = safe_grain,
            .chunks = chunk_count,
            .next_chunk = 0
        };

        const u32 task_count = std::min(chunk_count, worker_count_ * 4u);
        JobCounter counter{static_cast<i32>(task_count)};

        struct RunnerPayload {
            SharedState* state;
        };
        static_assert(sizeof(RunnerPayload) <= JOB_PAYLOAD_BYTES);

        auto runner = [](void* payload, u32) noexcept {
            auto* p = reinterpret_cast<RunnerPayload*>(payload);
            SharedState* s = p->state;
            u32 c = 0;
            while ((c = s->next_chunk.fetch_add(1u, std::memory_order_relaxed)) < s->chunks) {
                const u32 begin = c * s->grain;
                const u32 end = std::min(begin + s->grain, s->total);
                MemoryArena& scratch = s->js->current_scratch_arena();
                ArenaTempScope scope(scratch);
                (*s->fn_ptr)(begin, end, c, scratch);
            }
        };

        RunnerPayload payload{&state};

        // Push task_count - 1 background jobs, calling thread executes 1 task
        for (u32 t = 1; t < task_count; ++t) {
            Job job{};
            job.fn = runner;
            job.counter = &counter;
            std::memcpy(job.payload, &payload, sizeof(RunnerPayload));
            const u32 target = (current_worker_index() + t) % worker_count_;
            if (!deques_[target].push_bottom(job)) {
                runner(job.payload, current_worker_index());
                decrement_counter(counter);
            } else {
                approx_queued_jobs_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        AcquireSRWLockExclusive(&wake_lock_);
        ReleaseSRWLockExclusive(&wake_lock_);
        WakeAllConditionVariable(&wake_cv_);

        // Calling thread participates directly
        runner(&payload, current_worker_index());
        decrement_counter(counter);

        wait(counter);
    }

    template <typename AccT, typename MapChunkFn, typename ReduceFn>
    AccT parallel_reduce_deterministic(
        u32 total_count,
        u32 grain_size,
        AccT identity,
        MapChunkFn&& map_chunk_fn,
        ReduceFn&& reduce_fn) noexcept {
        static_assert(std::is_trivially_copyable_v<AccT>);
        if (total_count == 0) return identity;

        const u32 safe_grain = (grain_size == 0) ? 1u : grain_size;
        const u32 chunk_count = (total_count + safe_grain - 1u) / safe_grain;

        ArenaTempScope outer_scope(current_scratch_arena());
        std::span<AccT> partials = current_scratch_arena().alloc_span<AccT>(chunk_count);
        RON_ALWAYS_ASSERT(!partials.empty(), "Scratch arena allocation failed in parallel_reduce_deterministic");

        parallel_for_deterministic(total_count, safe_grain, [&](u32 begin, u32 end, u32 chunk_id, MemoryArena& chunk_scratch) {
            partials[chunk_id] = map_chunk_fn(begin, end, chunk_id, chunk_scratch);
        });

        AccT acc = identity;
        for (u32 c = 0; c < chunk_count; ++c) {
            acc = reduce_fn(acc, partials[c]);
        }
        return acc;
    }

private:
    static unsigned __stdcall worker_thread_main(void* user_data);

    struct WorkerContext {
        JobSystem* sys{nullptr};
        u32 worker_idx{0};
        HANDLE startup_event{nullptr};
    };

    EngineMemory* engine_mem_{nullptr};
    u32 worker_count_{1};
    WorkStealingDeque* deques_{nullptr};
    HANDLE thread_handles_[MAX_WORKER_THREADS]{};
    WorkerContext worker_contexts_[MAX_WORKER_THREADS]{};

    std::atomic<bool> shutdown_{false};
    std::atomic<i32> approx_queued_jobs_{0};

    RON_CACHE_ALIGN SRWLOCK wake_lock_{};
    CONDITION_VARIABLE wake_cv_{};

    RON_CACHE_ALIGN SRWLOCK done_lock_{};
    CONDITION_VARIABLE done_cv_{};
};

} // namespace ron
