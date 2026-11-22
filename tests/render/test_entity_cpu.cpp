#include "test_framework.hpp"
#include "render/entity_types.hpp"
#include "render/entity_feed.hpp"
#include "render/entity_math.hpp"
#include "render/entity_source.hpp"
#include "render/camera_rts.hpp"
#include <cmath>
#include <cstring>
#include <vector>

#ifndef RON_SOURCE_DIR
#define RON_SOURCE_DIR "."
#endif

#define RON_CHECK_CLOSE(a, b, eps) \
    RON_CHECK(std::fabs((a) - (b)) <= (eps))

namespace ron::render {

static u16 float_to_half(float f) noexcept {
    u32 x = 0;
    std::memcpy(&x, &f, sizeof(float));
    u32 sign = (x >> 16) & 0x8000u;
    i32 exp = static_cast<i32>((x >> 23) & 0xFFu) - 127 + 15;
    u32 mant = (x >> 13) & 0x03FFu;
    if (exp <= 0) return static_cast<u16>(sign);
    if (exp >= 31) return static_cast<u16>(sign | 0x7C00u);
    return static_cast<u16>(sign | (static_cast<u32>(exp) << 10) | mant);
}

// Test 1: Ent_TypesLayout
RON_TEST(Ent_TypesLayout) {
    RON_CHECK(sizeof(EntityInstance) == 64);
    RON_CHECK(sizeof(ModelGpu) == 80);
    RON_CHECK(sizeof(DrawCmd) == 20);
    RON_CHECK(sizeof(EntityUbo) == 160);

    RON_CHECK(offsetof(EntityInstance, pos_prev) == 0);
    RON_CHECK(offsetof(EntityInstance, yaw_prev) == 12);
    RON_CHECK(offsetof(EntityInstance, pos_curr) == 16);
    RON_CHECK(offsetof(EntityInstance, yaw_curr) == 28);
    RON_CHECK(offsetof(EntityInstance, ids) == 32);
    RON_CHECK(offsetof(EntityInstance, clip) == 36);
    RON_CHECK(offsetof(EntityInstance, anim_t) == 40);
    RON_CHECK(offsetof(EntityInstance, scale) == 44);
    RON_CHECK(offsetof(EntityInstance, hp) == 48);
    RON_CHECK(offsetof(EntityInstance, tint) == 52);
    RON_CHECK(offsetof(EntityInstance, r0) == 56);
    RON_CHECK(offsetof(EntityInstance, r1) == 60);

    RON_CHECK(offsetof(ModelGpu, lod) == 0);
    RON_CHECK(offsetof(ModelGpu, sphere) == 48);
    RON_CHECK(offsetof(ModelGpu, vat_row_base) == 64);

    RON_CHECK(offsetof(DrawCmd, count) == 0);
    RON_CHECK(offsetof(DrawCmd, instance_count) == 4);
    RON_CHECK(offsetof(DrawCmd, first_index) == 8);
    RON_CHECK(offsetof(DrawCmd, base_vertex) == 12);
    RON_CHECK(offsetof(DrawCmd, base_instance) == 16);

    RON_CHECK(offsetof(EntityUbo, planes) == 0);
    RON_CHECK(offsetof(EntityUbo, cam_alpha) == 96);
    RON_CHECK(offsetof(EntityUbo, cull_p) == 112);
    RON_CHECK(offsetof(EntityUbo, counts) == 128);
    RON_CHECK(offsetof(EntityUbo, time_p) == 144);

    u32 packed = entity_pack_ids(123, 7, 0x5A);
    RON_CHECK(entity_model_id(packed) == 123);
    RON_CHECK(entity_owner_id(packed) == 7);
    RON_CHECK(entity_flags(packed) == 0x5A);
}

// Test 2: Ent_Feed
RON_TEST(Ent_Feed) {
    EntityFeed feed;
    RON_CHECK(feed.count() == 0);
    RON_CHECK(feed.dropped() == 0);

    EntityInstance inst{};
    inst.scale = 1.0f;
    for (int i = 0; i < 100; ++i) {
        feed.add(inst);
    }
    RON_CHECK(feed.count() == 100);
    RON_CHECK(feed.dropped() == 0);

    feed.clear();
    RON_CHECK(feed.count() == 0);
    RON_CHECK(feed.dropped() == 0);

    for (u32 i = 0; i < MAX_INSTANCES; ++i) {
        feed.add(inst);
    }
    RON_CHECK(feed.count() == MAX_INSTANCES);
    RON_CHECK(feed.dropped() == 0);

    feed.add(inst);
    RON_CHECK(feed.count() == MAX_INSTANCES);
    RON_CHECK(feed.dropped() == 1);

    feed.reset_dropped();
    RON_CHECK(feed.dropped() == 0);
    RON_CHECK(feed.count() == MAX_INSTANCES);

    feed.clear();
    RON_CHECK(feed.count() == 0);
}

// Test 3: Ent_PlaneExtraction
RON_TEST(Ent_PlaneExtraction) {
    CameraRts cam;
    camera_init(cam, 512.0f, 512.0f, 0.0f, 100.0f);
    camera_snap(cam, nullptr);

    Mat4 view = camera_view(cam, nullptr);
    Mat4 proj = camera_proj(16.0f / 9.0f);
    Mat4 vp = mat4_mul(proj, view);

    float planes[6][4];
    entity_planes_from_viewproj(vp.m, planes);

    for (int i = 0; i < 6; ++i) {
        float len = std::sqrt(planes[i][0] * planes[i][0] +
                              planes[i][1] * planes[i][1] +
                              planes[i][2] * planes[i][2]);
        RON_CHECK_CLOSE(len, 1.0f, 1e-4f);
    }

    Vec3 eye = camera_eye(cam, nullptr);
    float target[3] = {512.0f, 0.0f, 512.0f};
    float mid[3] = {
        0.5f * (eye.x + target[0]),
        0.5f * (eye.y + target[1]),
        0.5f * (eye.z + target[2])
    };

    for (int i = 0; i < 6; ++i) {
        float d = planes[i][0] * mid[0] + planes[i][1] * mid[1] + planes[i][2] * mid[2] + planes[i][3];
        RON_CHECK(d >= 0.0f);
    }

    float behind[3] = {eye.x * 2.0f - target[0], eye.y * 2.0f - target[1], eye.z * 2.0f - target[2]};
    bool any_outside = false;
    for (int i = 0; i < 6; ++i) {
        float d = planes[i][0] * behind[0] + planes[i][1] * behind[1] + planes[i][2] * behind[2] + planes[i][3];
        if (d < 0.0f) any_outside = true;
    }
    RON_CHECK(any_outside);
}

// Test 4: Ent_CullMath
RON_TEST(Ent_CullMath) {
    CameraRts cam;
    camera_init(cam, 512.0f, 512.0f, 0.0f, 100.0f);
    camera_snap(cam, nullptr);

    Mat4 view = camera_view(cam, nullptr);
    Mat4 proj = camera_proj(16.0f / 9.0f);
    Mat4 vp = mat4_mul(proj, view);
    Vec3 eye = camera_eye(cam, nullptr);

    FrameUniforms frame{};
    frame.screen[0] = 1920.0f;
    frame.screen[1] = 1080.0f;
    frame.cam_pos[0] = eye.x;
    frame.cam_pos[1] = eye.y;
    frame.cam_pos[2] = eye.z;
    std::memcpy(frame.view, view.m, sizeof(frame.view));
    std::memcpy(frame.proj, proj.m, sizeof(frame.proj));
    std::memcpy(frame.view_proj, vp.m, sizeof(frame.view_proj));

    EntityUbo ubo;
    entity_fill_ubo(frame, 1.0f, 1, 1, 0.0f, &ubo);

    ModelCpu model{};
    model.sphere[0] = 0.0f;
    model.sphere[1] = 0.0f;
    model.sphere[2] = 0.0f;
    model.sphere[3] = 2.0f; // radius 2m

    // Invalid model test
    EntityInstance inst_inv{};
    inst_inv.ids = entity_pack_ids(5, 0, 0); // model 5 >= model_count 1
    CullResult res_inv = entity_cull_reference(inst_inv, &model, ubo);
    RON_CHECK(res_inv.bucket == 0xFFFFFFFFu);
    RON_CHECK(res_inv.reason == 1);

    // Visible instance at target (512, 0, 512)
    EntityInstance inst{};
    inst.ids = entity_pack_ids(0, 0, 0);
    inst.scale = 1.0f;
    inst.pos_prev[0] = 512.0f; inst.pos_prev[1] = 0.0f; inst.pos_prev[2] = 512.0f;
    inst.pos_curr[0] = 512.0f; inst.pos_curr[1] = 0.0f; inst.pos_curr[2] = 512.0f;
    inst.yaw_prev = 0.0f; inst.yaw_curr = 0.0f;

    CullResult res = entity_cull_reference(inst, &model, ubo);
    RON_CHECK(res.bucket != 0xFFFFFFFFu);
    RON_CHECK(res.reason == 0);

    // Frustum culled (far behind camera)
    EntityInstance inst_far{};
    inst_far.ids = entity_pack_ids(0, 0, 0);
    inst_far.scale = 1.0f;
    inst_far.pos_prev[0] = 9999.0f; inst_far.pos_prev[1] = 0.0f; inst_far.pos_prev[2] = 9999.0f;
    inst_far.pos_curr[0] = 9999.0f; inst_far.pos_curr[1] = 0.0f; inst_far.pos_curr[2] = 9999.0f;
    CullResult res_far = entity_cull_reference(inst_far, &model, ubo);
    RON_CHECK(res_far.bucket == 0xFFFFFFFFu);
    RON_CHECK(res_far.reason == 2);

    // Size culled (tiny scale inside frustum)
    EntityInstance inst_tiny = inst;
    inst_tiny.scale = 0.0001f;
    CullResult res_tiny = entity_cull_reference(inst_tiny, &model, ubo);
    RON_CHECK(res_tiny.bucket == 0xFFFFFFFFu);
    RON_CHECK(res_tiny.reason == 3);
}

// Test 5: Ent_VatSampling
RON_TEST(Ent_VatSampling) {
    std::vector<u16> vat(2 * VAT_FRAMES * VAT_COLS * 4, 0);

    auto set_mat = [&](u32 model, u32 frame, u32 part, const float* m34) {
        size_t row_idx = model * VAT_FRAMES + frame;
        for (int r = 0; r < 3; ++r) {
            size_t texel_idx = (row_idx * VAT_COLS + (part * 3 + r)) * 4;
            vat[texel_idx + 0] = float_to_half(m34[r * 4 + 0]);
            vat[texel_idx + 1] = float_to_half(m34[r * 4 + 1]);
            vat[texel_idx + 2] = float_to_half(m34[r * 4 + 2]);
            vat[texel_idx + 3] = float_to_half(m34[r * 4 + 3]);
        }
    };

    float id[12] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0
    };
    for (u32 p = 0; p < 16; ++p) {
        set_mat(0, 0, p, id);
    }

    VatCpu vcpu{};
    vcpu.halfs = vat.data();
    vcpu.rows = 2 * VAT_FRAMES;
    vcpu.cols = VAT_COLS;

    float m_out[12];
    vat_part_matrix(vcpu, 0, 0, 0.0f, 0, m_out);
    for (int i = 0; i < 12; ++i) {
        RON_CHECK_CLOSE(m_out[i], id[i], 1e-3f);
    }

    vat_part_matrix(vcpu, 0, 0, 8.0f / 16.0f, 0, m_out);
    for (int i = 0; i < 12; ++i) {
        RON_CHECK_CLOSE(m_out[i], id[i], 1e-3f);
    }
}

// Test 6: Ent_ModelTable
RON_TEST(Ent_ModelTable) {
    static const char* mdbin_paths[] = {
        RON_SOURCE_DIR "/assets/compiled/models/models_2026.mdbin",
        "assets/compiled/models/models_2026.mdbin",
        "Projects/dominion2026/assets/compiled/models/models_2026.mdbin"
    };
    static const char* tex26_paths[] = {
        RON_SOURCE_DIR "/assets/compiled/textures/textures_2026.tex26",
        "assets/compiled/textures/textures_2026.tex26",
        "Projects/dominion2026/assets/compiled/textures/textures_2026.tex26"
    };

    const char* mdbin = mdbin_paths[0];
    for (const char* p : mdbin_paths) {
        FILE* f = fopen(p, "rb");
        if (f) { fclose(f); mdbin = p; break; }
    }
    const char* tex26 = tex26_paths[0];
    for (const char* p : tex26_paths) {
        FILE* f = fopen(p, "rb");
        if (f) { fclose(f); tex26 = p; break; }
    }

    EntitySource src;
    bool ok = src.init(mdbin, tex26);
    RON_CHECK(ok);
    RON_CHECK(src.model_count() > 0);
    RON_CHECK(entity_front_face_ccw() == true);

    for (u32 i = 0; i < src.model_count(); ++i) {
        const ModelGpu& mg = src.model_gpu(i);
        RON_CHECK(mg.sphere[3] > 0.0f);
        for (int lod = 0; lod < 3; ++lod) {
            RON_CHECK(mg.lod[lod].index_count > 0);
        }
    }
}

} // namespace ron::render
