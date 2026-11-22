#pragma once

#include "core/types.hpp"
#include <glad/gl.h>

namespace ron::render {

class GpuTimer {
public:
    GpuTimer() = default;
    ~GpuTimer() { destroy(); }

    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    bool create();
    void begin(u32 slot);
    void end(u32 slot);
    float elapsed_ms(u32 slot);
    void destroy();

private:
    GLuint m_queries[4][2] = {};
    bool m_valid = false;
};

} // namespace ron::render
