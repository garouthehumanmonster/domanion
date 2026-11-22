#include "render/gpu_timer.hpp"

namespace ron::render {

bool GpuTimer::create() {
    destroy();
    for (u32 s = 0; s < 4; ++s) {
        glCreateQueries(GL_TIMESTAMP, 2, m_queries[s]);
    }
    m_valid = true;
    return true;
}

void GpuTimer::begin(u32 slot) {
    if (m_valid && slot < 4) {
        glQueryCounter(m_queries[slot][0], GL_TIMESTAMP);
    }
}

void GpuTimer::end(u32 slot) {
    if (m_valid && slot < 4) {
        glQueryCounter(m_queries[slot][1], GL_TIMESTAMP);
    }
}

float GpuTimer::elapsed_ms(u32 slot) {
    if (!m_valid || slot >= 4) return 0.0f;
    GLuint64 t0 = 0;
    GLuint64 t1 = 0;
    glGetQueryObjectui64v(m_queries[slot][0], GL_QUERY_RESULT, &t0);
    glGetQueryObjectui64v(m_queries[slot][1], GL_QUERY_RESULT, &t1);
    if (t1 <= t0) return 0.0f;
    return static_cast<float>(t1 - t0) * 1e-6f;
}

void GpuTimer::destroy() {
    if (m_valid) {
        for (u32 s = 0; s < 4; ++s) {
            glDeleteQueries(2, m_queries[s]);
            m_queries[s][0] = 0;
            m_queries[s][1] = 0;
        }
        m_valid = false;
    }
}

} // namespace ron::render
