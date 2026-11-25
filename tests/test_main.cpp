#include "test_framework.hpp"
#include "core/clock.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace ron::test {

namespace {

std::atomic<int> g_no_heap_alloc_depth{0};
std::atomic<u64> g_heap_alloc_count{0};

[[noreturn]] void trap_heap_alloc(const char* op_name, std::size_t alloc_bytes, std::size_t alloc_align) noexcept {
    const char* test_name = g_current_test ? g_current_test : "unknown";
    std::fprintf(
        stderr,
        "[FATAL] Heap allocation (%s, size=%zu, align=%zu) inside ScopedNoHeapAlloc (depth=%d, test=%s)\n",
        op_name,
        alloc_bytes,
        alloc_align,
        g_no_heap_alloc_depth.load(std::memory_order_acquire),
        test_name);
    std::fflush(stderr);
    std::abort();
}

void* checked_malloc(const char* op_name, std::size_t alloc_bytes) noexcept {
    if (g_no_heap_alloc_depth.load(std::memory_order_acquire) > 0) {
        trap_heap_alloc(op_name, alloc_bytes, alignof(std::max_align_t));
    }
    g_heap_alloc_count.fetch_add(1, std::memory_order_relaxed);
    const std::size_t safe_size = (alloc_bytes == 0) ? 1 : alloc_bytes;
    return std::malloc(safe_size);
}

void* checked_aligned_malloc(const char* op_name, std::size_t alloc_bytes, std::align_val_t alloc_align) noexcept {
    const std::size_t align_bytes = static_cast<std::size_t>(alloc_align);
    if (g_no_heap_alloc_depth.load(std::memory_order_acquire) > 0) {
        trap_heap_alloc(op_name, alloc_bytes, align_bytes);
    }
    g_heap_alloc_count.fetch_add(1, std::memory_order_relaxed);
    const std::size_t safe_size = (alloc_bytes == 0) ? 1 : alloc_bytes;
    const std::size_t safe_align = (align_bytes < sizeof(void*)) ? sizeof(void*) : align_bytes;
#if defined(_WIN32)
    return _aligned_malloc(safe_size, safe_align);
#else
    const std::size_t padded_size = ((safe_size + safe_align - 1) / safe_align) * safe_align;
    return std::aligned_alloc(safe_align, padded_size);
#endif
}

void aligned_free_impl(void* ptr) noexcept {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

} // namespace

u64 heap_alloc_count() noexcept {
    return g_heap_alloc_count.load(std::memory_order_acquire);
}

int no_heap_alloc_depth() noexcept {
    return g_no_heap_alloc_depth.load(std::memory_order_acquire);
}

ScopedNoHeapAlloc::ScopedNoHeapAlloc() noexcept {
    g_no_heap_alloc_depth.fetch_add(1, std::memory_order_acq_rel);
}

ScopedNoHeapAlloc::~ScopedNoHeapAlloc() noexcept {
    g_no_heap_alloc_depth.fetch_sub(1, std::memory_order_acq_rel);
}

} // namespace ron::test

void* operator new(std::size_t size) {
    void* ptr = ron::test::checked_malloc("operator new", size);
    if (!ptr) std::abort();
    return ptr;
}

void* operator new[](std::size_t size) {
    void* ptr = ron::test::checked_malloc("operator new[]", size);
    if (!ptr) std::abort();
    return ptr;
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return ron::test::checked_malloc("operator new(nothrow)", size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return ron::test::checked_malloc("operator new[](nothrow)", size);
}

void* operator new(std::size_t size, std::align_val_t align) {
    void* ptr = ron::test::checked_aligned_malloc("operator new(align)", size, align);
    if (!ptr) std::abort();
    return ptr;
}

void* operator new[](std::size_t size, std::align_val_t align) {
    void* ptr = ron::test::checked_aligned_malloc("operator new[](align)", size, align);
    if (!ptr) std::abort();
    return ptr;
}

void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
    return ron::test::checked_aligned_malloc("operator new(align, nothrow)", size, align);
}

void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
    return ron::test::checked_aligned_malloc("operator new[](align, nothrow)", size, align);
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    ron::test::aligned_free_impl(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
    ron::test::aligned_free_impl(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    ron::test::aligned_free_impl(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    ron::test::aligned_free_impl(ptr);
}

void operator delete(void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
    ron::test::aligned_free_impl(ptr);
}

void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
    ron::test::aligned_free_impl(ptr);
}

int main() {
    ron::clock_init();
    return ron::test::run_all_tests();
}
