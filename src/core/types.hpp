#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <bit>
#include <span>
#include <string_view>

namespace ron {

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

__extension__ typedef __int128 i128;
__extension__ typedef unsigned __int128 u128;

using f32 = float;
using f64 = double;
using usize = size_t;

inline constexpr usize CACHE_LINE_BYTES = 64;
inline constexpr usize OS_PAGE_GRANULARITY = 65536; // 64 KiB

inline constexpr usize KiB(usize n) { return n * 1024ULL; }
inline constexpr usize MiB(usize n) { return n * 1024ULL * 1024ULL; }

inline constexpr u32 MAX_WORKER_THREADS = 16;

#define RON_CACHE_ALIGN alignas(::ron::CACHE_LINE_BYTES)
#define RON_FORCE_INLINE __attribute__((always_inline)) inline

constexpr usize align_up(usize value, usize alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace ron
