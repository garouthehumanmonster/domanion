#include "arena.hpp"
#include <windows.h>

namespace ron {

MemoryArena::~MemoryArena() noexcept {
    shutdown();
}

MemoryArena::MemoryArena(MemoryArena&& other) noexcept
    : base_(other.base_)
    , reserved_(other.reserved_)
    , committed_(other.committed_)
    , offset_(other.offset_)
    , debug_name_(other.debug_name_) {
    other.base_ = nullptr;
    other.reserved_ = 0;
    other.committed_ = 0;
    other.offset_ = 0;
    other.debug_name_ = "unnamed";
}

MemoryArena& MemoryArena::operator=(MemoryArena&& other) noexcept {
    if (this != &other) {
        shutdown();
        base_ = other.base_;
        reserved_ = other.reserved_;
        committed_ = other.committed_;
        offset_ = other.offset_;
        debug_name_ = other.debug_name_;

        other.base_ = nullptr;
        other.reserved_ = 0;
        other.committed_ = 0;
        other.offset_ = 0;
        other.debug_name_ = "unnamed";
    }
    return *this;
}

Result<void> MemoryArena::init(usize reserve_bytes, usize initial_commit_bytes, const char* name) noexcept {
    if (base_ != nullptr) {
        shutdown();
    }

    reserve_bytes = align_up(reserve_bytes, OS_PAGE_GRANULARITY);
    initial_commit_bytes = align_up(initial_commit_bytes, OS_PAGE_GRANULARITY);
    if (initial_commit_bytes > reserve_bytes) {
        initial_commit_bytes = reserve_bytes;
    }

    void* res_ptr = VirtualAlloc(nullptr, reserve_bytes, MEM_RESERVE, PAGE_NOACCESS);
    if (!res_ptr) {
        return Result<void>::err(ErrorCode::OutOfMemory);
    }

    if (initial_commit_bytes > 0) {
        void* com_ptr = VirtualAlloc(res_ptr, initial_commit_bytes, MEM_COMMIT, PAGE_READWRITE);
        if (!com_ptr) {
            VirtualFree(res_ptr, 0, MEM_RELEASE);
            return Result<void>::err(ErrorCode::OutOfMemory);
        }
    }

    base_ = static_cast<u8*>(res_ptr);
    reserved_ = reserve_bytes;
    committed_ = initial_commit_bytes;
    offset_ = 0;
    debug_name_ = name ? name : "unnamed";

    return Result<void>::ok();
}

void MemoryArena::shutdown() noexcept {
    if (base_ != nullptr) {
        VirtualFree(base_, 0, MEM_RELEASE);
        base_ = nullptr;
    }
    reserved_ = 0;
    committed_ = 0;
    offset_ = 0;
    debug_name_ = "unnamed";
}

void* MemoryArena::alloc_raw(usize size, usize alignment) noexcept {
    RON_ALWAYS_ASSERT(std::has_single_bit(alignment), "Alignment must be power of two");

    if (size == 0) {
        return base_ + offset_;
    }

    const usize aligned_off = align_up(offset_, alignment);
    const usize next_off = aligned_off + size;

    if (next_off > reserved_) {
        panic_abort("next_off <= reserved_", __FILE__, __LINE__, "Arena reserve capacity exhausted");
    }

    if (next_off > committed_) {
        usize new_commit = align_up(next_off, OS_PAGE_GRANULARITY);
        if (new_commit > reserved_) {
            new_commit = reserved_;
        }
        const usize additional = new_commit - committed_;
        void* ptr = VirtualAlloc(base_ + committed_, additional, MEM_COMMIT, PAGE_READWRITE);
        RON_ALWAYS_ASSERT(ptr != nullptr, "VirtualAlloc MEM_COMMIT failed in arena");
        committed_ = new_commit;
    }

    // CRITICAL DETERMINISM INVARIANT: Explicitly zero padding gap AND payload
    std::memset(base_ + offset_, 0, next_off - offset_);
    offset_ = next_off;
    return base_ + aligned_off;
}

void MemoryArena::restore_marker(usize marker) noexcept {
    RON_ALWAYS_ASSERT(marker <= offset_, "Cannot restore marker beyond current offset");
    if (offset_ > marker) {
        std::memset(base_ + marker, 0, offset_ - marker);
    }
    offset_ = marker;
}

void MemoryArena::reset(bool decommit_excess) noexcept {
    if (offset_ > 0 && base_ != nullptr) {
        std::memset(base_, 0, offset_);
    }
    offset_ = 0;
    if (decommit_excess && committed_ > OS_PAGE_GRANULARITY && base_ != nullptr) {
        VirtualFree(base_ + OS_PAGE_GRANULARITY, committed_ - OS_PAGE_GRANULARITY, MEM_DECOMMIT);
        committed_ = OS_PAGE_GRANULARITY;
    }
}

Result<void> EngineMemory::init_all(u32 worker_count) noexcept {
    if (worker_count > MAX_WORKER_THREADS) {
        return Result<void>::err(ErrorCode::InvalidArgument);
    }

    auto res = permanent.init(MiB(256), MiB(4), "Permanent");
    if (res.is_err()) {
        shutdown_all();
        return res;
    }

    res = match.init(MiB(512), MiB(16), "Match");
    if (res.is_err()) {
        shutdown_all();
        return res;
    }

    res = render_staging.init(MiB(128), MiB(2), "RenderStaging");
    if (res.is_err()) {
        shutdown_all();
        return res;
    }

    for (u32 i = 0; i < MAX_WORKER_THREADS; ++i) {
        res = worker_scratch[i].init(MiB(32), KiB(256), "WorkerScratch");
        if (res.is_err()) {
            shutdown_all();
            return res;
        }
    }

    return Result<void>::ok();
}

void EngineMemory::shutdown_all() noexcept {
    permanent.shutdown();
    match.shutdown();
    render_staging.shutdown();
    for (u32 i = 0; i < MAX_WORKER_THREADS; ++i) {
        worker_scratch[i].shutdown();
    }
}

} // namespace ron
