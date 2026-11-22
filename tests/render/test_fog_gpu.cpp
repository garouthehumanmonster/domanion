#include "test_framework.hpp"
#include "render/gl_fixture.hpp"
#include "render/scene_fixture.hpp"
#include "render/overlay_types.hpp"
#include "render/fog_map.hpp"
#include "render/fog_gpu.hpp"
#include <vector>

namespace ron::render {

RON_TEST(Fog_GpuSmooth) {
    GL_TEST_BEGIN();
    VramLedger ledger;
    FogGpu fog_gpu;
    RON_CHECK(fog_gpu.init(ledger, 512, &test::test_scene().shaders));

    FogMap fog_map;
    fog_map.reveal_all();
    fog_gpu.upload(fog_map);

    float last_x = 0.0f;
    float final_y = 0.0f;
    for (int frame = 0; frame < 30; ++frame) {
        fog_gpu.step(1.0f / 60.0f);

        float center[2] = {0.0f, 0.0f};
        glGetTextureSubImage(fog_gpu.smooth_tex(), 0, 256, 256, 0, 1, 1, 1,
                            GL_RG, GL_FLOAT, sizeof(center), center);

        RON_CHECK(center[0] >= last_x);
        last_x = center[0];
        final_y = center[1];
    }
    RON_CHECK(last_x >= 0.98f);
    RON_CHECK_EQ(final_y, 1.0f);

    fog_map.begin_frame();
    fog_map.commit();
    fog_gpu.upload(fog_map);

    for (int frame = 0; frame < 30; ++frame) {
        fog_gpu.step(1.0f / 60.0f);

        float center[2] = {0.0f, 0.0f};
        glGetTextureSubImage(fog_gpu.smooth_tex(), 0, 256, 256, 0, 1, 1, 1,
                            GL_RG, GL_FLOAT, sizeof(center), center);

        RON_CHECK(center[0] <= last_x);
        last_x = center[0];
        RON_CHECK_EQ(center[1], 1.0f);
    }
    RON_CHECK(last_x >= 0.15f);
    RON_CHECK(last_x <= 0.30f);

    FogMap fresh_map;
    fresh_map.reset();
    fog_gpu.upload(fresh_map);
    fog_gpu.snap();

    float second[2] = {1.0f, 1.0f};
    glGetTextureSubImage(fog_gpu.smooth_tex(), 0, 100, 100, 0, 1, 1, 1,
                        GL_RG, GL_FLOAT, sizeof(second), second);
    RON_CHECK_EQ(second[0], 0.0f);
    RON_CHECK_EQ(second[1], 0.0f);

    GL_TEST_END();
}

} // namespace ron::render
