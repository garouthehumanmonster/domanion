#pragma once

#include "core/types.hpp"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace ron::render {

class FogMap {
public:
    FogMap() noexcept { reset(); }

    void begin_frame() noexcept {
        std::memset(m_visible, 0, sizeof(m_visible));
    }

    void reveal_circle_mm(i32 x_mm, i32 z_mm, i32 r_mm) noexcept {
        if (r_mm <= 0) return;
        i64 r2 = static_cast<i64>(r_mm) * static_cast<i64>(r_mm);
        // cell size is 8000 mm, cell centre is (cx*8000 + 4000, cz*8000 + 4000)
        i32 min_cx = std::clamp((x_mm - r_mm) / 8000, 0, 511);
        i32 max_cx = std::clamp((x_mm + r_mm) / 8000, 0, 511);
        i32 min_cz = std::clamp((z_mm - r_mm) / 8000, 0, 511);
        i32 max_cz = std::clamp((z_mm + r_mm) / 8000, 0, 511);

        for (i32 cz = min_cz; cz <= max_cz; ++cz) {
            i64 cz_centre = static_cast<i64>(cz) * 8000 + 4000;
            i64 dz = cz_centre - z_mm;
            i64 dz2 = dz * dz;
            if (dz2 > r2) continue;
            for (i32 cx = min_cx; cx <= max_cx; ++cx) {
                i64 cx_centre = static_cast<i64>(cx) * 8000 + 4000;
                i64 dx = cx_centre - x_mm;
                if (dx * dx + dz2 <= r2) {
                    m_visible[cz * 512 + cx] = 255;
                }
            }
        }
    }

    void reveal_circle(float x, float z, float r) noexcept {
        i32 x_mm = static_cast<i32>(std::llround(x * 1000.0f));
        i32 z_mm = static_cast<i32>(std::llround(z * 1000.0f));
        i32 r_mm = static_cast<i32>(std::llround(r * 1000.0f));
        reveal_circle_mm(x_mm, z_mm, r_mm);
    }

    void commit() noexcept {
        bool changed = false;
        for (int i = 0; i < 512 * 512; ++i) {
            u8 old_e = m_explored[i];
            m_explored[i] |= m_visible[i];
            if (m_explored[i] != old_e) {
                changed = true;
            }
        }
        if (changed) {
            m_dirty = true;
        }
    }

    void reveal_all() noexcept {
        std::memset(m_visible, 255, sizeof(m_visible));
        std::memset(m_explored, 255, sizeof(m_explored));
        m_dirty = true;
    }

    void reset() noexcept {
        std::memset(m_visible, 0, sizeof(m_visible));
        std::memset(m_explored, 0, sizeof(m_explored));
        m_dirty = false;
    }

    void test_fill(u8 vis, u8 exp, int min_cx, int max_cx) noexcept {
        int x0 = std::clamp(min_cx, 0, 511);
        int x1 = std::clamp(max_cx, 0, 511);
        for (int z = 0; z < 512; ++z) {
            for (int x = x0; x <= x1; ++x) {
                m_visible[z * 512 + x] = vis;
                m_explored[z * 512 + x] = exp;
            }
        }
        m_dirty = true;
    }

    void test_clear_rect(int min_cx, int max_cx, int min_cz, int max_cz) noexcept {
        int x0 = std::clamp(min_cx, 0, 511);
        int x1 = std::clamp(max_cx, 0, 511);
        int z0 = std::clamp(min_cz, 0, 511);
        int z1 = std::clamp(max_cz, 0, 511);
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                m_visible[z * 512 + x] = 0;
                m_explored[z * 512 + x] = 0;
            }
        }
        m_dirty = true;
    }

    const u8* visible_data() const noexcept { return m_visible; }
    const u8* explored_data() const noexcept { return m_explored; }

    bool consume_dirty() noexcept {
        bool d = m_dirty;
        m_dirty = false;
        return d;
    }

private:
    u8 m_visible[512 * 512];
    u8 m_explored[512 * 512];
    bool m_dirty{false};
};

} // namespace ron::render
