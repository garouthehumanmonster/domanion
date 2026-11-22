#include "render/gl_state.hpp"
#include <glad/gl.h>

namespace ron::render {

static GLuint s_empty_vao = 0;

void gl_state_init() {
    if (s_empty_vao == 0) {
        glCreateVertexArrays(1, &s_empty_vao);
    }
    glBindVertexArray(s_empty_vao);

    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glDepthFunc(GL_GREATER);

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glDisable(GL_DITHER);
    glDisable(GL_FRAMEBUFFER_SRGB);
}

void draw_fullscreen_triangle() {
    if (s_empty_vao != 0) {
        glBindVertexArray(s_empty_vao);
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

} // namespace ron::render
