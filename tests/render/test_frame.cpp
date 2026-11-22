#include "render/gl_fixture.hpp"
#include "render/frame_uniforms.hpp"
#include "render/persistent_ring.hpp"
#include "render/shader_library.hpp"
#include "render/gl_objects.hpp"
#include "render/gl_bindings.hpp"
#include "core/assert_log.hpp"

namespace ron::render {

static const char* get_shaders_dir() {
    static const char* paths[] = {
        RON_SOURCE_DIR "/assets/shaders",
        "assets/shaders",
        "../assets/shaders",
        "../../assets/shaders"
    };
    for (const char* p : paths) {
        std::string test_file = std::string(p) + "/common/frame.glsl";
        FILE* f = fopen(test_file.c_str(), "rb");
        if (f) {
            fclose(f);
            return p;
        }
    }
    return paths[0];
}

RON_TEST(Frame_UboLayout) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    VramLedger ledger;
    const char* shaders_dir = get_shaders_dir();
    ShaderLibrary lib;
    std::string init_log;
    RON_CHECK(lib.init(disk_file_reader, const_cast<char*>(shaders_dir), init_log));

    FrameUniforms u;
    float* p = reinterpret_cast<float*>(&u);
    for (int i = 0; i < 84; ++i) {
        p[i] = static_cast<float>(i + 1);
    }

    GpuBuffer ubo_buf;
    RON_CHECK(ubo_buf.create(ledger, VramCat::EntityBuffers, "frame_ubo", sizeof(FrameUniforms), &u, GL_DYNAMIC_STORAGE_BIT));
    glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_FRAME, ubo_buf.id());

    GpuBuffer out_buf;
    RON_CHECK(out_buf.create(ledger, VramCat::EntityBuffers, "out_ssbo", 84 * sizeof(float), nullptr, GL_DYNAMIC_STORAGE_BIT));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_TEST_OUT, out_buf.id());

    u32 prog = lib.program(ProgramId::SelfTestFrame);
    RON_CHECK(prog != 0);
    glUseProgram(prog);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_ALL_BARRIER_BITS);

    float readback[84] = {0};
    glGetNamedBufferSubData(out_buf.id(), 0, 84 * sizeof(float), readback);
    for (int i = 0; i < 84; ++i) {
        RON_CHECK_EQ(readback[i], static_cast<float>(i + 1));
    }

    GLuint block_idx = glGetUniformBlockIndex(prog, "FrameUbo");
    RON_CHECK(block_idx != GL_INVALID_INDEX);
    GLint block_size = 0;
    glGetActiveUniformBlockiv(prog, block_idx, GL_UNIFORM_BLOCK_DATA_SIZE, &block_size);
    RON_CHECK_EQ(block_size, 336);

    glUseProgram(0);
    glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_FRAME, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_TEST_OUT, 0);

    GL_TEST_END();
}

RON_TEST(Ring_Persistent) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    VramLedger ledger;
    const char* shaders_dir = get_shaders_dir();
    ShaderLibrary lib;
    std::string init_log;
    RON_CHECK(lib.init(disk_file_reader, const_cast<char*>(shaders_dir), init_log));

    GLint ssbo_align = 0;
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &ssbo_align);
    GLint ubo_align = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_align);

    PersistentRing ring;
    RON_CHECK(ring.create(ledger, VramCat::Staging, "ring_test", 1024 * 4, static_cast<u32>(ssbo_align), static_cast<u32>(ubo_align)));

    RON_CHECK_EQ(ring.slot_stride() % static_cast<u64>(ssbo_align), 0u);
    RON_CHECK_EQ(ring.slot_stride() % static_cast<u64>(ubo_align), 0u);
    RON_CHECK(ring.slot_stride() >= 4096u);

    GpuBuffer out_buf;
    RON_CHECK(out_buf.create(ledger, VramCat::EntityBuffers, "test_out_buf", 4096, nullptr, GL_DYNAMIC_STORAGE_BIT));
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_TEST_OUT, out_buf.id());

    u32 ring_prog = lib.program(ProgramId::SelfTestRing);
    RON_CHECK(ring_prog != 0);
    glUseProgram(ring_prog);
    glUniform1ui(0, 1024);

    u8* slot_ptrs[3] = { nullptr, nullptr, nullptr };

    for (u32 f = 0; f < 12; ++f) {
        u8* p = ring.begin_frame();
        RON_CHECK_EQ(ring.slot_index(), f % 3u);
        if (f < 3) {
            slot_ptrs[f] = p;
        }

        u32* u32_p = reinterpret_cast<u32*>(p);
        for (u32 i = 0; i < 1024; ++i) {
            u32_p[i] = f * 1000u + i;
        }

        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_TEST_IN, ring.buffer(), static_cast<GLintptr>(ring.slot_offset()), 4096);
        glDispatchCompute(16, 1, 1);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
        ring.end_frame();

        u32 rb[1024] = {0};
        glGetNamedBufferSubData(out_buf.id(), 0, 4096, rb);
        for (u32 i = 0; i < 1024; ++i) {
            RON_CHECK_EQ(rb[i], (f * 1000u + i) * 2u + 1u);
        }
    }

    RON_CHECK(slot_ptrs[0] != nullptr && slot_ptrs[1] != nullptr && slot_ptrs[2] != nullptr);
    RON_CHECK(slot_ptrs[0] != slot_ptrs[1]);
    RON_CHECK(slot_ptrs[1] != slot_ptrs[2]);
    RON_CHECK_EQ(static_cast<u64>(slot_ptrs[1] - slot_ptrs[0]), ring.slot_stride());
    RON_CHECK_EQ(static_cast<u64>(slot_ptrs[2] - slot_ptrs[1]), ring.slot_stride());
    RON_CHECK_EQ(ledger.used(VramCat::Staging), ring.slot_stride() * 3);

    glUseProgram(0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_TEST_IN, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BIND_SSBO_TEST_OUT, 0);

    GL_TEST_END();
}

} // namespace ron::render
