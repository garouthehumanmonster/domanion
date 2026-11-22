#include "clock.hpp"
#include <windows.h>

namespace ron {

static u64 g_perf_frequency = 0;

void clock_init() noexcept {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_perf_frequency = static_cast<u64>(freq.QuadPart);
}

u64 clock_now_ns() noexcept {
    if (g_perf_frequency == 0) {
        clock_init();
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    const u64 count = static_cast<u64>(counter.QuadPart);
    const u128 total_ns = static_cast<u128>(count) * 1'000'000'000ULL;
    return static_cast<u64>(total_ns / g_perf_frequency);
}

} // namespace ron
