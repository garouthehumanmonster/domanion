#include "render/gl_fixture.hpp"
#include "core/assert_log.hpp"

namespace ron::render::test {

GlContext& test_gl() {
    static GlContext s_ctx;
    static bool s_initialized = false;
    if (!s_initialized) {
        GlContextDesc desc;
        desc.title = "ron_render_tests";
        desc.width = 640;
        desc.height = 360;
        desc.hidden = true;
        desc.debug = true;
        desc.swap_interval = 0;
        bool ok = s_ctx.create(desc);
        RON_ALWAYS_ASSERT(ok, "Failed to create test OpenGL 4.6 debug context");
        s_initialized = true;
    }
    return s_ctx;
}

} // namespace ron::render::test
