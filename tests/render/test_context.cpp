#include "render/gl_fixture.hpp"

namespace ron::render {

RON_TEST(Gl_Context46) {
    auto& gl = ::ron::render::test::test_gl();
    GL_TEST_BEGIN();

    const auto& caps = gl.caps();
    RON_CHECK(caps.major * 10 + caps.minor >= 46);

    GLint profile_mask = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile_mask);
    RON_CHECK(profile_mask & GL_CONTEXT_CORE_PROFILE_BIT);

    GLint flags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    RON_CHECK(flags & GL_CONTEXT_FLAG_DEBUG_BIT);

    RON_CHECK(glClipControl != nullptr);
    RON_CHECK(glCreateBuffers != nullptr);
    RON_CHECK(glNamedBufferStorage != nullptr);
    RON_CHECK(glDispatchCompute != nullptr);
    RON_CHECK(glMultiDrawElementsIndirectCount != nullptr);
    RON_CHECK(glGetTextureSubImage != nullptr);

    RON_CHECK(caps.renderer[0] != '\0');
    RON_CHECK_EQ(gl.width(), 640);
    RON_CHECK_EQ(gl.height(), 360);

    GL_TEST_END();
}

RON_TEST(Gl_DebugCallbackWired) {
    auto& gl = ::ron::render::test::test_gl();
    GL_TEST_BEGIN();

    RON_CHECK_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    RON_CHECK_EQ(gl.debug_error_count(), 0u);

    // Trigger deliberate GL error with invalid enum
    glEnable(0xFFFF);

    GLenum err = glGetError();
    RON_CHECK_EQ(err, static_cast<GLenum>(GL_INVALID_ENUM));
    RON_CHECK(gl.debug_error_count() >= 1u);

    gl.reset_debug_counters();
    RON_CHECK_EQ(gl.debug_error_count(), 0u);

    while (glGetError() != GL_NO_ERROR) {}
}

} // namespace ron::render
