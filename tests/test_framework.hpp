#pragma once

#include "core/types.hpp"
#include "core/clock.hpp"
#include <cstdio>
#include <string_view>

namespace ron::test {

using TestFn = void (*)();

struct TestCase {
    const char* name;
    TestFn fn;
};

inline constexpr usize MAX_TESTS = 128;
inline TestCase g_tests[MAX_TESTS];
inline usize g_test_count = 0;
inline const char* g_current_test = nullptr;
inline usize g_assert_failures = 0;

u64 heap_alloc_count() noexcept;
int no_heap_alloc_depth() noexcept;

struct ScopedNoHeapAlloc {
    ScopedNoHeapAlloc() noexcept;
    ~ScopedNoHeapAlloc() noexcept;
    ScopedNoHeapAlloc(const ScopedNoHeapAlloc&) = delete;
    ScopedNoHeapAlloc& operator=(const ScopedNoHeapAlloc&) = delete;
};

inline void register_test(const char* name, TestFn fn) noexcept {
    if (g_test_count < MAX_TESTS) {
        g_tests[g_test_count++] = TestCase{name, fn};
    }
}

struct AutoRegister {
    AutoRegister(const char* name, TestFn fn) noexcept {
        register_test(name, fn);
    }
};

#define RON_TEST(name) \
    static void test_##name(); \
    static ::ron::test::AutoRegister auto_reg_##name(#name, test_##name); \
    static void test_##name()

#define RON_CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d: Assertion failed: %s (in %s)\n", \
                __FILE__, __LINE__, #cond, ::ron::test::g_current_test ? ::ron::test::g_current_test : "unknown"); \
            ::ron::test::g_assert_failures++; \
        } \
    } while (0)

#define RON_EXPECT_TRUE(cond) RON_CHECK(cond)

#define RON_CHECK_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            std::fprintf(stderr, "[FAIL] %s:%d: Assertion failed: %s == %s (in %s)\n", \
                __FILE__, __LINE__, #a, #b, ::ron::test::g_current_test ? ::ron::test::g_current_test : "unknown"); \
            ::ron::test::g_assert_failures++; \
        } \
    } while (0)

#define RON_EXPECT_EQ(a, b) RON_CHECK_EQ(a, b)

#define RON_CHECK_NE(a, b) \
    do { \
        if (!((a) != (b))) { \
            std::fprintf(stderr, "[FAIL] %s:%d: Assertion failed: %s != %s (in %s)\n", \
                __FILE__, __LINE__, #a, #b, ::ron::test::g_current_test ? ::ron::test::g_current_test : "unknown"); \
            ::ron::test::g_assert_failures++; \
        } \
    } while (0)

#define RON_EXPECT_NE(a, b) RON_CHECK_NE(a, b)

inline int run_all_tests() noexcept {
    std::printf("========================================\n");
    std::printf("Running %zu automated tests...\n", g_test_count);
    std::printf("========================================\n");

    usize passed = 0;
    usize failed = 0;

    for (usize i = 0; i < g_test_count; ++i) {
        g_current_test = g_tests[i].name;
        const usize failures_before = g_assert_failures;

        const u64 start_ns = clock_now_ns();
        std::printf("[ RUN      ] %s\n", g_tests[i].name);
        std::fflush(stdout);
        g_tests[i].fn();
        const u64 duration_ns = clock_now_ns() - start_ns;
        const f64 duration_ms = clock_ns_to_ms(duration_ns);

        if (g_assert_failures == failures_before) {
            std::printf("[       OK ] %s (%.3f ms)\n", g_tests[i].name, duration_ms);
            passed++;
        } else {
            std::printf("[  FAILED  ] %s (%zu failure(s), %.3f ms)\n",
                        g_tests[i].name, g_assert_failures - failures_before, duration_ms);
            failed++;
        }
        std::fflush(stdout);
    }

    std::printf("========================================\n");
    std::printf("Test results: %zu passed, %zu failed\n", passed, failed);
    std::printf("========================================\n");

    return failed == 0 ? 0 : 1;
}

} // namespace ron::test
