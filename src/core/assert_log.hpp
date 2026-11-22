#pragma once

#include "types.hpp"

namespace ron {

enum class LogLevel : u8 {
    Trace = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3,
    Fatal = 4
};

void log_set_min_level(LogLevel level) noexcept;

void log_msg(LogLevel level, const char* file, int line, const char* fmt, ...) noexcept
    __attribute__((format(printf, 4, 5)));

[[noreturn]] void panic_abort(const char* expr, const char* file, int line, const char* msg) noexcept;

} // namespace ron

#define RON_LOG_INFO(fmt, ...) \
    ::ron::log_msg(::ron::LogLevel::Info, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define RON_LOG_WARN(fmt, ...) \
    ::ron::log_msg(::ron::LogLevel::Warn, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define RON_LOG_ERROR(fmt, ...) \
    ::ron::log_msg(::ron::LogLevel::Error, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define RON_ALWAYS_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            ::ron::panic_abort(#cond, __FILE__, __LINE__, msg); \
        } \
    } while (0)

#if defined(RON_DEBUG) && (RON_DEBUG == 1)
    #define RON_ASSERT(cond, msg) RON_ALWAYS_ASSERT(cond, msg)
#else
    #define RON_ASSERT(cond, msg) ((void)0)
#endif
