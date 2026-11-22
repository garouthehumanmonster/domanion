#include "test_framework.hpp"
#include "render/gl_fixture.hpp"
#include "render/scene_fixture.hpp"
#include "render/lighting_system.hpp"
#include "render/ui_atlas.hpp"
#include "render/marks_renderer.hpp"
#include "render/ui_source.hpp"
#include "render/entity_source.hpp"
#include "render/gl_bindings.hpp"
#include "render/camera_rts.hpp"
#include <vector>

namespace ron::render {

// Test 7: Lit_ShadowResources
RON_TEST(Lit_ShadowResources) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    VramLedger ledger;
    auto& scene = test::test_scene();
    scene.init();

    LightingSystem lighting;
    RON_CHECK(lighting.init(ledger, scene.shaders));

    // Verify 4 FBOs are GL_FRAMEBUFFER_COMPLETE
    for (u32 c = 0; c < 4; ++c) {
        u32 fbo = lighting.shadow_fbo(c);
        RON_CHECK(fbo != 0);
        GLenum status = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER);
        RON_CHECK_EQ(status, GL_FRAMEBUFFER_COMPLETE);
    }

    // Verify shadow map format, dimensions, layers
    const auto& sm = lighting.shadow_map();
    RON_CHECK(sm.id() != 0);
    RON_CHECK_EQ(sm.width(), 2048u);
    RON_CHECK_EQ(sm.height(), 2048u);
    RON_CHECK_EQ(sm.layers(), 4u);
    RON_CHECK(sm.fmt() == TexFmt::D32F);

    // Verify VRAM ledger charge equals 64 MiB (ShadowMaps)
    u64 shadow_bytes = ledger.used(VramCat::ShadowMaps);
    RON_CHECK_EQ(shadow_bytes >> 20, 64ull);

    // Call update with dummy camera/sun
    FrameUniforms fu{};
    fu.sun_dir[0] = 0.3f;
    fu.sun_dir[1] = 0.7f;
    fu.sun_dir[2] = 0.65f;
    fu.sun_dir[3] = 3.0f;
    fu.sun_color[0] = 1.0f;
    fu.sun_color[1] = 0.95f;
    fu.sun_color[2] = 0.85f;
    fu.screen[0] = 640.0f;
    fu.screen[1] = 360.0f;

    CameraRts cam;
    camera_init(cam, 2048.0f, 2048.0f, 0.0f, 150.0f);
    lighting.update(fu, cam, LIGHT_FLAG_CSM | LIGHT_FLAG_TERRAIN_SHADOW);

    // Bind test
    lighting.bind(0);

    // Destroy leaves ledger net zero
    lighting.destroy(ledger);
    RON_CHECK_EQ(ledger.total_used(), 0ull);

    GL_TEST_END();
}

// Test 8: Ui_AtlasUpload
RON_TEST(Ui_AtlasUpload) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    VramLedger ledger;
    UiSource ui_source;
    RON_CHECK(ui_source.init());
    RON_CHECK(ui_source.is_valid());

    UiAtlas atlas;
    RON_CHECK(atlas.init(ledger, ui_source));
    RON_CHECK(atlas.is_valid());
    RON_CHECK_EQ(atlas.width(), 1024u);
    RON_CHECK_EQ(atlas.height(), 512u);

    // Read back first 64x64 icon block (x=0, y=384, w=64, h=64)
    std::vector<u8> icon_block(64 * 64 * 4, 0);
    glGetTextureSubImage(atlas.texture_id(), 0,
                        0, 384, 0,
                        64, 64, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE,
                        static_cast<GLsizei>(icon_block.size()),
                        icon_block.data());

    // Confirm non-zero alpha texels
    bool has_nonzero_alpha = false;
    for (size_t i = 3; i < icon_block.size(); i += 4) {
        if (icon_block[i] > 0) {
            has_nonzero_alpha = true;
            break;
        }
    }
    RON_CHECK(has_nonzero_alpha);

    // Verify VRAM charge: 2 MiB (1024 * 512 * 4 = 2,097,152)
    RON_CHECK_EQ(ledger.used(VramCat::UiAtlas), 2097152ull);

    atlas.bind(TEX_MSDF);

    // Destroy leaves ledger net zero
    atlas.destroy(ledger);
    RON_CHECK_EQ(ledger.total_used(), 0ull);

    GL_TEST_END();
}

// Test 9: Marks_Resources
RON_TEST(Marks_Resources) {
    auto& gl = ::ron::render::test::test_gl();
    (void)gl;
    GL_TEST_BEGIN();

    VramLedger ledger;
    auto& scene = test::test_scene();
    scene.init();

    UiSource ui_source;
    RON_CHECK(ui_source.init());

    EntitySource entity_source;
    RON_CHECK(entity_source.init());

    MarksRenderer marks;
    RON_CHECK(marks.init(ledger, scene.shaders, ui_source, entity_source));

    // Verify buffer sizes
    RON_CHECK_EQ(marks.icons_ssbo().size(), static_cast<u64>(MAX_INSTANCES) * sizeof(IconGpu));
    RON_CHECK_EQ(marks.bars_ssbo().size(), static_cast<u64>(MAX_INSTANCES) * sizeof(BarGpu));
    RON_CHECK_EQ(marks.cmds_ssbo().size(), 2ull * sizeof(DrawArraysIndirectCommand));
    RON_CHECK_EQ(marks.model_marks_ssbo().size(), static_cast<u64>(MAX_MODELS) * sizeof(ModelMarkGpu));
    RON_CHECK_EQ(marks.marks_ubo().size(), sizeof(MarksUbo));

    // Verify bindings by calling bind()
    marks.bind();

    // Verify model_marks SSBO matches 71-model table
    ModelMarkGpu readback_marks[MAX_MODELS]{};
    glGetNamedBufferSubData(marks.model_marks_ssbo().id(), 0,
                            sizeof(readback_marks), readback_marks);

    u32 marked = 0;
    u32 excluded = 0;
    for (u32 i = 0; i < 71; ++i) {
        if (readback_marks[i].icon != 0xFFFFu) {
            marked++;
        } else {
            excluded++;
        }
    }
    RON_CHECK_EQ(marked, 54u);
    RON_CHECK_EQ(excluded, 17u);

    // Check specific iconic model mark mappings
    RON_CHECK_EQ(readback_marks[0].icon, 4u);   // inf_spear -> sym_infantry
    RON_CHECK_EQ(readback_marks[0].shape, 0u);  // ground
    RON_CHECK_EQ(readback_marks[14].icon, 5u);  // veh_mbt -> sym_armor
    RON_CHECK_EQ(readback_marks[23].icon, 7u);  // air_biplane -> sym_air
    RON_CHECK_EQ(readback_marks[23].shape, 1u); // air
    RON_CHECK_EQ(readback_marks[30].icon, 8u);  // nav_galley -> sym_naval
    RON_CHECK_EQ(readback_marks[30].shape, 2u); // naval
    RON_CHECK_EQ(readback_marks[36].icon, 12u); // bld_city_center_a -> sym_logistics
    RON_CHECK_EQ(readback_marks[36].shape, 3u); // building
    RON_CHECK_EQ(readback_marks[54].icon, 0xFFFFu); // prop_tree_oak

    // update_ubo() verifies atlas UVs inside [0,1]^2 and ordered v0 < v1
    FrameUniforms fu{};
    fu.screen[0] = 640.0f;
    fu.screen[1] = 360.0f;
    marks.update_ubo(fu, 1.0f);

    const MarksUbo& cpu_ubo = marks.cpu_ubo();
    for (u32 i = 0; i < 32; ++i) {
        float u0 = cpu_ubo.icon_uv[i][0];
        float v0 = cpu_ubo.icon_uv[i][1];
        float u1 = cpu_ubo.icon_uv[i][2];
        float v1 = cpu_ubo.icon_uv[i][3];

        RON_CHECK(u0 >= 0.0f && u0 < u1 && u1 <= 1.0f);
        RON_CHECK(v0 >= 0.0f && v0 < v1 && v1 <= 1.0f);
    }

    // Destroy leaves ledger net zero
    marks.destroy(ledger);
    RON_CHECK_EQ(ledger.total_used(), 0ull);

    GL_TEST_END();
}

} // namespace ron::render
