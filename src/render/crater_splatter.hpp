#pragma once

#include "core/types.hpp"
#include "render/vram_ledger.hpp"
#include "render/gl_objects.hpp"
#include "render/shader_library.hpp"
#include "render/overlay_types.hpp"
#include "render/crater_math.hpp"
#include <glad/gl.h>

namespace ron::render {

class CraterSplatter {
public:
    CraterSplatter() noexcept = default;
    ~CraterSplatter() noexcept { destroy(); }

    CraterSplatter(const CraterSplatter&) = delete;
    CraterSplatter& operator=(const CraterSplatter&) = delete;

    bool init(VramLedger& ledger, int n = CRATER_N, const ShaderLibrary* shaders = nullptr) noexcept;
    void destroy() noexcept;

    bool enqueue(CraterSplat splat) noexcept;
    int flush(int max_per_frame = MAX_SPLATS_PER_FLUSH) noexcept;
    void clear() noexcept;

    void set_shaders(const ShaderLibrary* shaders) noexcept { m_shaders = shaders; }

    int pending() const noexcept { return m_count; }
    u32 dropped() const noexcept { return m_dropped; }
    u32 applied_total() const noexcept { return m_applied_total; }
    GLuint texture() const noexcept { return m_crater_tex.id(); }
    int n() const noexcept { return m_n; }
    u64 vram_bytes() const noexcept;

private:
    int m_n{0};
    const ShaderLibrary* m_shaders{nullptr};

    GpuTexture m_crater_tex;
    GpuBuffer m_splat_ssbo;

    CraterSplat m_queue[SPLAT_QUEUE_CAP]{};
    int m_head{0};
    int m_tail{0};
    int m_count{0};
    u32 m_dropped{0};
    u32 m_applied_total{0};
};

} // namespace ron::render
