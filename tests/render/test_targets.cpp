#include "render/gl_fixture.hpp"
#include "render/render_target.hpp"
#include "render/gl_state.hpp"
#include "render/gpu_timer.hpp"
#include "render/shader_library.hpp"
#include "render/gl_bindings.hpp"
#include "core/assert_log.hpp"
#include <cmath>
#include <algorithm>

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

RON_TEST(Depth_ReverseZ) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    VramLedger ledger;
    const char* shaders_dir = get_shaders_dir();
    ShaderLibrary lib;
    std::string init_log;
    RON_CHECK(lib.init(disk_file_reader, const_cast<char*>(shaders_dir), init_log));

    RenderTarget target;
    RON_CHECK(target.create(ledger, VramCat::HdrTargets, VramCat::DepthTargets, "rt_depth", 64, 64, TexFmt::RGBA8, true));

    gl_state_init();
    const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    target.clear(black);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo());
    glViewport(0, 0, 64, 64);

    u32 probe_prog = lib.program(ProgramId::DepthProbe);
    RON_CHECK(probe_prog != 0);
    glUseProgram(probe_prog);

    // Draw with DepthProbe u_z = 0.5, color = (1, 0, 0, 1)
    glUniform1f(0, 0.5f);
    glUniform4f(1, 1.0f, 0.0f, 0.0f, 1.0f);
    draw_fullscreen_triangle();

    // Draw with DepthProbe u_z = 0.25 (reverse-Z: 0.25 is farther than 0.5, rejected by GL_GREATER), color = (0, 1, 0, 1)
    glUniform1f(0, 0.25f);
    glUniform4f(1, 0.0f, 1.0f, 0.0f, 1.0f);
    draw_fullscreen_triangle();

    // Read back center pixel -> red (1, 0, 0, 1)
    u8 px[4] = {0};
    glGetTextureSubImage(target.color_tex(), 0, 32, 32, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sizeof(px), px);
    RON_CHECK_EQ(px[0], 255);
    RON_CHECK_EQ(px[1], 0);
    RON_CHECK_EQ(px[2], 0);
    RON_CHECK_EQ(px[3], 255);

    // Draw with u_z = 0.75 (closer to near; 0.75 > 0.5, passes GL_GREATER), color = (0, 0, 1, 1)
    glUniform1f(0, 0.75f);
    glUniform4f(1, 0.0f, 0.0f, 1.0f, 1.0f);
    draw_fullscreen_triangle();

    // Read back center pixel -> blue (0, 0, 1, 1)
    glGetTextureSubImage(target.color_tex(), 0, 32, 32, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sizeof(px), px);
    RON_CHECK_EQ(px[0], 0);
    RON_CHECK_EQ(px[1], 0);
    RON_CHECK_EQ(px[2], 255);
    RON_CHECK_EQ(px[3], 255);

    // Read back depth texture via glGetTextureSubImage: center depth equals 0.75f within 1e-6
    float depth_val = 0.0f;
    glGetTextureSubImage(target.depth_tex(), 0, 32, 32, 0, 1, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, sizeof(float), &depth_val);
    RON_CHECK(std::abs(depth_val - 0.75f) <= 1e-6f);

    glDisable(GL_DEPTH_TEST);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    GL_TEST_END();
}

RON_TEST(Present_Tonemap) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    VramLedger ledger;
    const char* shaders_dir = get_shaders_dir();
    ShaderLibrary lib;
    std::string init_log;
    RON_CHECK(lib.init(disk_file_reader, const_cast<char*>(shaders_dir), init_log));

    gl_state_init();

    RenderTarget rt_hdr;
    RON_CHECK(rt_hdr.create(ledger, VramCat::HdrTargets, VramCat::DepthTargets, "rt_hdr", 128, 128, TexFmt::RGBA16F, false));

    RenderTarget rt_post;
    RON_CHECK(rt_post.create(ledger, VramCat::PostFx, VramCat::DepthTargets, "rt_post", 128, 128, TexFmt::RGBA8, false));

    // Clear HDR target with color (2.0, 0.5, 0.1, 1.0)
    const float clear_hdr[4] = { 2.0f, 0.5f, 0.1f, 1.0f };
    rt_hdr.clear(clear_hdr);

    glBindTextureUnit(TEX_HDR, rt_hdr.color_tex());
    glBindFramebuffer(GL_FRAMEBUFFER, rt_post.fbo());
    glViewport(0, 0, 128, 128);
    glDisable(GL_DEPTH_TEST);

    u32 tonemap_prog = lib.program(ProgramId::Tonemap);
    RON_CHECK(tonemap_prog != 0);
    glUseProgram(tonemap_prog);
    glUniform1f(0, 1.0f); // u_exposure = 1.0

    draw_fullscreen_triangle();

    auto srgb_encode = [](float linear) -> u8 {
        float s = (linear <= 0.0031308f) ? (12.92f * linear) : (1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f);
        int val = static_cast<int>(std::round(s * 255.0f));
        return static_cast<u8>(std::clamp(val, 0, 255));
    };

    u8 out_px[4] = {0};
    glGetTextureSubImage(rt_post.color_tex(), 0, 64, 64, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sizeof(out_px), out_px);

    float c_r = 2.0f / 3.0f;
    float c_g = 0.5f / 1.5f;
    float c_b = 0.1f / 1.1f;
    u8 exp_r = srgb_encode(c_r);
    u8 exp_g = srgb_encode(c_g);
    u8 exp_b = srgb_encode(c_b);

    RON_CHECK(std::abs(static_cast<int>(out_px[0]) - static_cast<int>(exp_r)) <= 2);
    RON_CHECK(std::abs(static_cast<int>(out_px[1]) - static_cast<int>(exp_g)) <= 2);
    RON_CHECK(std::abs(static_cast<int>(out_px[2]) - static_cast<int>(exp_b)) <= 2);

    // Repeat with TonemapPassthrough: input (0.5, 0.2, 0.8) -> exact sRGB encode within +/- 2 levels
    const float clear_pass[4] = { 0.5f, 0.2f, 0.8f, 1.0f };
    rt_hdr.clear(clear_pass);

    u32 pass_prog = lib.program(ProgramId::TonemapPassthrough);
    RON_CHECK(pass_prog != 0);
    glUseProgram(pass_prog);
    glUniform1f(0, 1.0f);

    draw_fullscreen_triangle();

    glGetTextureSubImage(rt_post.color_tex(), 0, 64, 64, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sizeof(out_px), out_px);
    u8 exp_pass_r = srgb_encode(0.5f);
    u8 exp_pass_g = srgb_encode(0.2f);
    u8 exp_pass_b = srgb_encode(0.8f);

    RON_CHECK(std::abs(static_cast<int>(out_px[0]) - static_cast<int>(exp_pass_r)) <= 2);
    RON_CHECK(std::abs(static_cast<int>(out_px[1]) - static_cast<int>(exp_pass_g)) <= 2);
    RON_CHECK(std::abs(static_cast<int>(out_px[2]) - static_cast<int>(exp_pass_b)) <= 2);

    glUseProgram(0);
    glBindTextureUnit(TEX_HDR, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    GL_TEST_END();
}

RON_TEST(GpuTimer_Basic) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    VramLedger ledger;
    const char* shaders_dir = get_shaders_dir();
    ShaderLibrary lib;
    std::string init_log;
    RON_CHECK(lib.init(disk_file_reader, const_cast<char*>(shaders_dir), init_log));

    gl_state_init();

    RenderTarget dummy;
    RON_CHECK(dummy.create(ledger, VramCat::HdrTargets, VramCat::DepthTargets, "dummy", 64, 64, TexFmt::RGBA8, false));

    const float clear_col[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    dummy.clear(clear_col);

    glBindFramebuffer(GL_FRAMEBUFFER, dummy.fbo());
    glViewport(0, 0, 64, 64);
    glDisable(GL_DEPTH_TEST);

    u32 probe_prog = lib.program(ProgramId::DepthProbe);
    glUseProgram(probe_prog);
    glUniform1f(0, 0.0f);
    glUniform4f(1, 1.0f, 1.0f, 1.0f, 1.0f);

    GpuTimer timer;
    RON_CHECK(timer.create());

    timer.begin(0);
    for (int i = 0; i < 1000; ++i) {
        draw_fullscreen_triangle();
    }
    timer.end(0);

    float elapsed = timer.elapsed_ms(0);
    RON_CHECK(elapsed > 0.0f);
    RON_CHECK(elapsed < 1000.0f);

    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    GL_TEST_END();
}

} // namespace ron::render
