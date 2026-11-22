#pragma once

#include "types.hpp"

namespace ron {

// FOR PROFILING/RENDERING ONLY — BANNED IN src/sim/
void clock_init() noexcept;
u64 clock_now_ns() noexcept;

inline f64 clock_ns_to_ms(u64 ns) noexcept {
    return static_cast<f64>(ns) * 1e-6;
}

} // namespace ron
