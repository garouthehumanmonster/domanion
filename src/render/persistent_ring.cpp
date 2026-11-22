#include "render/persistent_ring.hpp"
#include "core/assert_log.hpp"
#include <algorithm>

namespace ron::render {

bool PersistentRing::create(
    VramLedger& ledger,
    VramCat cat,
    const char* label,
    u64 bytes_per_frame,
    u32 caps_ssbo_align,
    u32 caps_ubo_align)
{
    destroy();

    u64 align = std::max({ static_cast<u64>(caps_ssbo_align), static_cast<u64>(caps_ubo_align), static_cast<u64>(256) });
    u64 slot_stride = (bytes_per_frame + align - 1) & ~(align - 1);
    u64 total_bytes = slot_stride * 3;

    u32 lid = 0;
    if (!ledger.reserve(cat, total_bytes, label, lid)) {
        return false;
    }

    m_ledger = &ledger;
    m_ledger_id = lid;
    m_cat = cat;
    m_slot_stride = slot_stride;

    glCreateBuffers(1, &m_buffer);
    GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    glNamedBufferStorage(m_buffer, static_cast<GLsizeiptr>(total_bytes), nullptr, flags);

    m_mapped = static_cast<u8*>(glMapNamedBufferRange(m_buffer, 0, static_cast<GLsizeiptr>(total_bytes), flags));
    if (!m_mapped) {
        glDeleteBuffers(1, &m_buffer);
        m_buffer = 0;
        return false;
    }

    m_frame_index = 0;
    m_slot_index = 0;
    for (int i = 0; i < 3; ++i) {
        m_fences[i] = nullptr;
    }

    return true;
}

u8* PersistentRing::begin_frame() {
    u32 slot = static_cast<u32>(m_frame_index % 3);
    m_slot_index = slot;

    if (m_fences[slot] != nullptr) {
        GLenum res = glClientWaitSync(m_fences[slot], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        if (res == GL_TIMEOUT_EXPIRED) {
            RON_LOG_ERROR("PersistentRing fence wait timed out on slot %u", slot);
        }
        glDeleteSync(m_fences[slot]);
        m_fences[slot] = nullptr;
    }

    return m_mapped + (static_cast<u64>(slot) * m_slot_stride);
}

void PersistentRing::end_frame() {
    if (m_fences[m_slot_index] != nullptr) {
        glDeleteSync(m_fences[m_slot_index]);
    }
    m_fences[m_slot_index] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    m_frame_index++;
}

void PersistentRing::destroy() {
    for (int i = 0; i < 3; ++i) {
        if (m_fences[i] != nullptr) {
            glDeleteSync(m_fences[i]);
            m_fences[i] = nullptr;
        }
    }
    if (m_buffer != 0) {
        glUnmapNamedBuffer(m_buffer);
        glDeleteBuffers(1, &m_buffer);
        m_buffer = 0;
        m_mapped = nullptr;
    }
    if (m_ledger && m_ledger_id != 0) {
        m_ledger->release(m_ledger_id);
        m_ledger_id = 0;
    }
    m_slot_stride = 0;
    m_slot_index = 0;
    m_frame_index = 0;
    m_ledger = nullptr;
}

} // namespace ron::render
