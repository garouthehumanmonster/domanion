#pragma once

#include "core/types.hpp"
#include "render/vram_ledger.hpp"
#include <glad/gl.h>

namespace ron::render {

class PersistentRing {
public:
    PersistentRing() = default;
    ~PersistentRing() { destroy(); }

    PersistentRing(const PersistentRing&) = delete;
    PersistentRing& operator=(const PersistentRing&) = delete;

    bool create(VramLedger& ledger, VramCat cat, const char* label,
                u64 bytes_per_frame, u32 caps_ssbo_align, u32 caps_ubo_align);

    // FRAME-SAFE: advances slot = frame_index % 3, waits on fence, returns mapped pointer
    u8* begin_frame();

    // FRAME-SAFE: glFenceSync for the current slot
    void end_frame();

    u32 buffer() const { return m_buffer; }
    u64 slot_offset() const { return static_cast<u64>(m_slot_index) * m_slot_stride; }
    u64 slot_stride() const { return m_slot_stride; }
    u32 slot_index() const { return m_slot_index; }
    u64 frame_index() const { return m_frame_index; }

    void destroy();

private:
    u32 m_buffer = 0;
    u8* m_mapped = nullptr;
    u64 m_slot_stride = 0;
    u32 m_slot_index = 0;
    u64 m_frame_index = 0;
    GLsync m_fences[3] = { nullptr, nullptr, nullptr };
    VramLedger* m_ledger = nullptr;
    u32 m_ledger_id = 0;
    VramCat m_cat = VramCat::Staging;
};

} // namespace ron::render
