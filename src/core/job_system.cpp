#include "job_system.hpp"

namespace ron {

static thread_local u32 tls_worker_index = 0;

JobSystem::JobSystem() noexcept {
    InitializeSRWLock(&wake_lock_);
    InitializeConditionVariable(&wake_cv_);
    InitializeSRWLock(&done_lock_);
    InitializeConditionVariable(&done_cv_);
}

JobSystem::~JobSystem() noexcept {
    shutdown();
}

Result<void> JobSystem::init(u32 requested_workers, EngineMemory* engine_mem) noexcept {
    if (engine_mem == nullptr) {
        return Result<void>::err(ErrorCode::InvalidArgument);
    }
    RON_ALWAYS_ASSERT(requested_workers <= MAX_WORKER_THREADS, "requested workers <= MAX_WORKER_THREADS");
    engine_mem_ = engine_mem;

    u32 workers = requested_workers;
    if (workers == 0) {
        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        workers = sys_info.dwNumberOfProcessors;
    }
    worker_count_ = std::clamp(workers, 1u, MAX_WORKER_THREADS);

    deques_ = engine_mem->permanent.alloc<WorkStealingDeque>(worker_count_);
    if (deques_ == nullptr) {
        return Result<void>::err(ErrorCode::OutOfMemory);
    }
    for (u32 i = 0; i < worker_count_; ++i) {
        new (&deques_[i]) WorkStealingDeque();
    }

    tls_worker_index = 0;
    shutdown_.store(false, std::memory_order_release);
    approx_queued_jobs_.store(0, std::memory_order_relaxed);

    if (worker_count_ > 1) {
        HANDLE startup_events[MAX_WORKER_THREADS]{};
        for (u32 i = 1; i < worker_count_; ++i) {
            startup_events[i] = CreateEventA(nullptr, TRUE, FALSE, nullptr);
            worker_contexts_[i].sys = this;
            worker_contexts_[i].worker_idx = i;
            worker_contexts_[i].startup_event = startup_events[i];

            uintptr_t handle = _beginthreadex(
                nullptr,
                2 * 1024 * 1024,
                &JobSystem::worker_thread_main,
                &worker_contexts_[i],
                0,
                nullptr
            );

            if (!handle) {
                shutdown_.store(true, std::memory_order_release);
                for (u32 j = 1; j < i; ++j) {
                    if (thread_handles_[j]) {
                        WaitForSingleObject(thread_handles_[j], INFINITE);
                        CloseHandle(thread_handles_[j]);
                        thread_handles_[j] = nullptr;
                    }
                }
                for (u32 j = 1; j <= i; ++j) {
                    if (startup_events[j]) CloseHandle(startup_events[j]);
                }
                return Result<void>::err(ErrorCode::IoError);
            }
            thread_handles_[i] = reinterpret_cast<HANDLE>(handle);
        }

        for (u32 i = 1; i < worker_count_; ++i) {
            WaitForSingleObject(startup_events[i], INFINITE);
            CloseHandle(startup_events[i]);
            worker_contexts_[i].startup_event = nullptr;
        }
    }

    return Result<void>::ok();
}

void JobSystem::shutdown() noexcept {
    if (shutdown_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    AcquireSRWLockExclusive(&wake_lock_);
    ReleaseSRWLockExclusive(&wake_lock_);
    WakeAllConditionVariable(&wake_cv_);

    AcquireSRWLockExclusive(&done_lock_);
    ReleaseSRWLockExclusive(&done_lock_);
    WakeAllConditionVariable(&done_cv_);

    for (u32 i = 1; i < worker_count_; ++i) {
        if (thread_handles_[i]) {
            WaitForSingleObject(thread_handles_[i], INFINITE);
            CloseHandle(thread_handles_[i]);
            thread_handles_[i] = nullptr;
        }
    }
    worker_count_ = 1;
}

u32 JobSystem::current_worker_index() const noexcept {
    return tls_worker_index;
}

MemoryArena& JobSystem::current_scratch_arena() noexcept {
    RON_ALWAYS_ASSERT(engine_mem_ != nullptr, "EngineMemory must not be null");
    return engine_mem_->worker_scratch[current_worker_index()];
}

void JobSystem::reset_all_scratch_arenas() noexcept {
    RON_ALWAYS_ASSERT(engine_mem_ != nullptr, "EngineMemory must not be null");
    for (u32 i = 0; i < worker_count_; ++i) {
        engine_mem_->worker_scratch[i].reset(false);
    }
}

void JobSystem::decrement_counter(JobCounter& counter) noexcept {
    if (counter.pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        AcquireSRWLockExclusive(&done_lock_);
        ReleaseSRWLockExclusive(&done_lock_);
        WakeAllConditionVariable(&done_cv_);
    }
}

bool JobSystem::execute_one_available_job(u32 my_idx) noexcept {
    Job job;
    if (deques_[my_idx].pop_bottom(job)) {
        approx_queued_jobs_.fetch_sub(1, std::memory_order_relaxed);
        job.fn(job.payload, my_idx);
        if (job.counter != nullptr) {
            decrement_counter(*job.counter);
        }
        return true;
    }

    for (u32 offset = 1; offset < worker_count_; ++offset) {
        const u32 victim = (my_idx + offset) % worker_count_;
        if (deques_[victim].try_steal_top(job)) {
            approx_queued_jobs_.fetch_sub(1, std::memory_order_relaxed);
            job.fn(job.payload, my_idx);
            if (job.counter != nullptr) {
                decrement_counter(*job.counter);
            }
            return true;
        }
    }

    return false;
}

void JobSystem::wait(JobCounter& counter) noexcept {
    const u32 my_idx = current_worker_index();

    while (counter.pending.load(std::memory_order_acquire) > 0) {
        if (execute_one_available_job(my_idx)) {
            continue;
        }
        for (int spin = 0; spin < 64; ++spin) {
            if (counter.pending.load(std::memory_order_acquire) <= 0) break;
            if (approx_queued_jobs_.load(std::memory_order_relaxed) > 0) break;
            _mm_pause();
        }
        if (counter.pending.load(std::memory_order_acquire) <= 0) break;

        if (approx_queued_jobs_.load(std::memory_order_acquire) == 0) {
            AcquireSRWLockExclusive(&done_lock_);
            if (counter.pending.load(std::memory_order_acquire) > 0 &&
                approx_queued_jobs_.load(std::memory_order_relaxed) == 0) {
                SleepConditionVariableSRW(&done_cv_, &done_lock_, 1, 0);
            }
            ReleaseSRWLockExclusive(&done_lock_);
        }
    }
}

unsigned __stdcall JobSystem::worker_thread_main(void* user_data) {
    auto* ctx = static_cast<WorkerContext*>(user_data);
    tls_worker_index = ctx->worker_idx;
    JobSystem* sys = ctx->sys;
    const u32 wid = ctx->worker_idx;

    if (ctx->startup_event) {
        SetEvent(ctx->startup_event);
    }

    while (!sys->shutdown_.load(std::memory_order_acquire)) {
        if (sys->execute_one_available_job(wid)) {
            continue;
        }
        for (int spin = 0; spin < 64; ++spin) {
            if (sys->shutdown_.load(std::memory_order_relaxed)) break;
            if (sys->approx_queued_jobs_.load(std::memory_order_relaxed) > 0) break;
            _mm_pause();
        }
        if (sys->shutdown_.load(std::memory_order_acquire)) break;
        if (sys->approx_queued_jobs_.load(std::memory_order_relaxed) > 0) continue;

        AcquireSRWLockExclusive(&sys->wake_lock_);
        if (!sys->shutdown_.load(std::memory_order_relaxed) &&
            sys->approx_queued_jobs_.load(std::memory_order_relaxed) == 0) {
            SleepConditionVariableSRW(&sys->wake_cv_, &sys->wake_lock_, 2, 0);
        }
        ReleaseSRWLockExclusive(&sys->wake_lock_);
    }

    return 0;
}

} // namespace ron
