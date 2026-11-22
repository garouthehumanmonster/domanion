#pragma once

#include "render/gl_context.hpp"
#include "test_framework.hpp"
#include <cstdio>

namespace ron::render::test {

GlContext& test_gl();

inline void gl_test_begin() {
    test_gl();
    while (glGetError() != GL_NO_ERROR) {}
    test_gl().reset_debug_counters();
}

inline void gl_test_end(const char* file, int line) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::fprintf(stderr, "[FAIL] %s:%d: Unexpected GL error: 0x%X\n", file, line, err);
        ::ron::test::g_assert_failures++;
    }
    u32 dbg_err = test_gl().debug_error_count();
    if (dbg_err != 0) {
        std::fprintf(stderr, "[FAIL] %s:%d: Unexpected debug error count: %u\n", file, line, dbg_err);
        ::ron::test::g_assert_failures++;
    }
}

} // namespace ron::render::test

#define GL_TEST_BEGIN() ::ron::render::test::gl_test_begin()
#define GL_TEST_END() ::ron::render::test::gl_test_end(__FILE__, __LINE__)
