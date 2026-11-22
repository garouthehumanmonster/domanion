#include "assert_log.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdarg>

namespace ron {

static SRWLOCK g_log_lock = SRWLOCK_INIT;
static LogLevel g_min_level = LogLevel::Info;

void log_set_min_level(LogLevel level) noexcept {
    AcquireSRWLockExclusive(&g_log_lock);
    g_min_level = level;
    ReleaseSRWLockExclusive(&g_log_lock);
}

static const char* log_level_str(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "UNKNOWN";
}

void log_msg(LogLevel level, const char* file, int line, const char* fmt, ...) noexcept {
    if (static_cast<u8>(level) < static_cast<u8>(g_min_level)) {
        return;
    }

    char buffer[1024];
    int prefix_len = std::snprintf(buffer, sizeof(buffer), "[%s] %s:%d: ",
                                   log_level_str(level), file, line);
    if (prefix_len < 0) {
        prefix_len = 0;
    }

    va_list args;
    va_start(args, fmt);
    int msg_len = 0;
    if (static_cast<usize>(prefix_len) < sizeof(buffer)) {
        msg_len = std::vsnprintf(buffer + prefix_len, sizeof(buffer) - prefix_len, fmt, args);
    }
    va_end(args);

    if (msg_len < 0) {
        msg_len = 0;
    }

    usize total_len = static_cast<usize>(prefix_len) + static_cast<usize>(msg_len);
    if (total_len >= sizeof(buffer) - 1) {
        total_len = sizeof(buffer) - 2;
    }
    buffer[total_len++] = '\n';
    buffer[total_len] = '\0';

    FILE* out_stream = (level >= LogLevel::Warn) ? stderr : stdout;

    AcquireSRWLockExclusive(&g_log_lock);
    std::fwrite(buffer, 1, total_len, out_stream);
    std::fflush(out_stream);
    ReleaseSRWLockExclusive(&g_log_lock);
}

[[noreturn]] void panic_abort(const char* expr, const char* file, int line, const char* msg) noexcept {
    char buffer[1024];
    int len = std::snprintf(buffer, sizeof(buffer),
                            "[FATAL] Assertion failed: %s at %s:%d: %s\n",
                            expr, file, line, msg ? msg : "");
    if (len > 0) {
        AcquireSRWLockExclusive(&g_log_lock);
        std::fwrite(buffer, 1, static_cast<usize>(len), stderr);
        std::fflush(stderr);
        ReleaseSRWLockExclusive(&g_log_lock);
    }
    __builtin_trap();
}

} // namespace ron
