#include "test_framework.hpp"
#include "render/rmath.hpp"
#include "render/height_sampler.hpp"
#include "render/camera_rts.hpp"
#include "render/frame_fill.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace ron::render {

RON_TEST(Height_Sampler) {
    // 1. code <-> metres
    float h13107 = height_code_to_m(13107);
    RON_CHECK(std::abs(h13107 - 0.0f) < 1e-4f);

    float h0 = height_code_to_m(0);
    RON_CHECK(std::abs(h0 - (-128.0f)) < 1e-4f);

    float h65535 = height_code_to_m(65535);
    RON_CHECK(std::abs(h65535 - 512.0f) < 1e-4f);

    RON_CHECK(height_m_to_code(0.0f) == 13107);
    RON_CHECK(height_m_to_code(-128.0f) == 0);
    RON_CHECK(height_m_to_code(512.0f) == 65535);

    // 2. Synthetic N=8 heightmap with code(i,j) = 13107 + 1000*i
    u16 codes[64];
    for (int j = 0; j < 8; ++j) {
        for (int i = 0; i < 8; ++i) {
            codes[j * 8 + i] = static_cast<u16>(13107 + 1000 * i);
        }
    }
    HeightSampler sampler;
    sampler.init(codes, 8);
    RON_CHECK(sampler.n() == 8);
    RON_CHECK(std::abs(sampler.cell_m() - 512.0f) < 1e-4f);

    for (int i = 0; i < 8; ++i) {
        float x = (static_cast<float>(i) + 0.5f) * 512.0f;
        float h = sampler.height_m(x, 100.0f);
        float exp_h = height_code_to_m(static_cast<u16>(13107 + 1000 * i));
        RON_CHECK(std::abs(h - exp_h) < 1e-3f);
    }

    for (int i = 0; i < 7; ++i) {
        float x = (static_cast<float>(i) + 1.0f) * 512.0f;
        float h = sampler.height_m(x, 500.0f);
        float h_i = height_code_to_m(static_cast<u16>(13107 + 1000 * i));
        float h_next = height_code_to_m(static_cast<u16>(13107 + 1000 * (i + 1)));
        float exp_mean = 0.5f * (h_i + h_next);
        RON_CHECK(std::abs(h - exp_mean) < 1e-3f);
    }

    float h_neg = sampler.height_m(-50.0f, 100.0f);
    float exp_tex0 = height_code_to_m(static_cast<u16>(13107));
    RON_CHECK(std::abs(h_neg - exp_tex0) < 1e-3f);

    float h_pos = sampler.height_m(4200.0f, 100.0f);
    float exp_tex7 = height_code_to_m(static_cast<u16>(13107 + 7000));
    RON_CHECK(std::abs(h_pos - exp_tex7) < 1e-3f);

    // z has no effect
    RON_CHECK(sampler.height_m(200.0f, 0.0f) == sampler.height_m(200.0f, 3000.0f));

    // min_m / max_m
    RON_CHECK(std::abs(sampler.min_m() - exp_tex0) < 1e-3f);
    RON_CHECK(std::abs(sampler.max_m() - exp_tex7) < 1e-3f);

    // 3. Vertical ray
    u16 flat_codes[64];
    std::fill(std::begin(flat_codes), std::end(flat_codes), static_cast<u16>(13107));
    HeightSampler flat_sampler;
    flat_sampler.init(flat_codes, 8);

    Vec3 hit{};
    bool ok = pick_terrain(flat_sampler, Vec3(1000.0f, 500.0f, 2000.0f), Vec3(0.0f, -1.0f, 0.0f), 1000.0f, hit);
    RON_CHECK(ok);
    RON_CHECK(std::abs(hit.y - 0.0f) < 0.01f);
    RON_CHECK(std::abs(hit.x - 1000.0f) < 1e-3f);
    RON_CHECK(std::abs(hit.z - 2000.0f) < 1e-3f);

    // Ray pointing up -> false
    bool up_ok = pick_terrain(flat_sampler, Vec3(1000.0f, 500.0f, 2000.0f), Vec3(0.0f, 1.0f, 0.0f), 1000.0f, hit);
    RON_CHECK(!up_ok);

    // Origin below terrain (flat 100m map, origin y=50) -> false
    u16 c100 = height_m_to_code(100.0f);
    u16 flat100_codes[64];
    std::fill(std::begin(flat100_codes), std::end(flat100_codes), c100);
    HeightSampler s100;
    s100.init(flat100_codes, 8);

    bool below_ok = pick_terrain(s100, Vec3(1000.0f, 50.0f, 2000.0f), Vec3(0.0f, -1.0f, 0.0f), 1000.0f, hit);
    RON_CHECK(!below_ok);
}

RON_TEST(Cam_Matrices) {
    // 1. Camera at focus (1000,*,2000), y=0, dist 200
    CameraRts cam;
    camera_init(cam, 1000.0f, 2000.0f, 0.0f, 200.0f);
    camera_snap(cam, nullptr);

    float p = camera_pitch(200.0f);
    Vec3 eye0 = camera_eye(cam, nullptr);
    Vec3 exp_eye0(1000.0f, 200.0f * std::sin(p), 2000.0f + 200.0f * std::cos(p));
    RON_CHECK((eye0 - exp_eye0).length() < 1e-3f);

    cam.target_yaw = cam.yaw = std::numbers::pi_v<float> * 0.5f;
    Vec3 eye90 = camera_eye(cam, nullptr);
    Vec3 exp_eye90(1000.0f - 200.0f * std::cos(p), 200.0f * std::sin(p), 2000.0f);
    RON_CHECK((eye90 - exp_eye90).length() < 1e-3f);

    // view * focus == (0,0,-200) within 1e-3 (both yaws)
    Vec3 focus(1000.0f, 0.0f, 2000.0f);
    Mat4 v90 = camera_view(cam, nullptr);
    Vec3 vf90 = transform_point(v90, focus);
    RON_CHECK(std::abs(vf90.x) < 1e-3f && std::abs(vf90.y) < 1e-3f && std::abs(vf90.z - (-200.0f)) < 1e-3f);

    cam.target_yaw = cam.yaw = 0.0f;
    Mat4 v0 = camera_view(cam, nullptr);
    Vec3 vf0 = transform_point(v0, focus);
    RON_CHECK(std::abs(vf0.x) < 1e-3f && std::abs(vf0.y) < 1e-3f && std::abs(vf0.z - (-200.0f)) < 1e-3f);

    // 2. camera_pitch checks
    float rad28 = 28.0f * (std::numbers::pi_v<float> / 180.0f);
    float rad62 = 62.0f * (std::numbers::pi_v<float> / 180.0f);
    float rad45 = 45.0f * (std::numbers::pi_v<float> / 180.0f);
    float deg_tol = 0.01f * (std::numbers::pi_v<float> / 180.0f);

    RON_CHECK(std::abs(camera_pitch(30.0f) - rad28) < deg_tol);
    RON_CHECK(std::abs(camera_pitch(1500.0f) - rad62) < deg_tol);
    RON_CHECK(std::abs(camera_pitch(std::sqrt(30.0f * 1500.0f)) - rad45) < deg_tol);

    float prev_pitch = 0.0f;
    for (int i = 0; i < 20; ++i) {
        float d = 30.0f + (1500.0f - 30.0f) * (static_cast<float>(i) / 19.0f);
        float cur_p = camera_pitch(d);
        RON_CHECK(cur_p > prev_pitch);
        prev_pitch = cur_p;
    }
    RON_CHECK(camera_pitch(10.0f) == camera_pitch(30.0f));
    RON_CHECK(camera_pitch(2000.0f) == camera_pitch(1500.0f));

    // 3. Projection (aspect 16/9)
    float aspect = 16.0f / 9.0f;
    Mat4 P = camera_proj(aspect);

    float ndc[3];
    project(P, Vec3(0.0f, 0.0f, -1.0f), ndc);
    RON_CHECK(std::abs(ndc[2] - 1.0f) < 1e-5f);

    project(P, Vec3(0.0f, 0.0f, -16000.0f), ndc);
    RON_CHECK(std::abs(ndc[2] - 0.0f) < 1e-5f);

    float n = CAM_NEAR;
    float f_far = CAM_FAR;
    float A = -n / (f_far - n);
    float B = (n * f_far) / (f_far - n);
    project(P, Vec3(0.0f, 0.0f, -100.0f), ndc);
    RON_CHECK(std::abs(ndc[2] - (A + B / 100.0f)) < 1e-6f);

    float fovy = CAM_FOV_Y_DEG * (std::numbers::pi_v<float> / 180.0f);
    float d = 300.0f;
    project(P, Vec3(std::tan(fovy * 0.5f) * aspect * d, 0.0f, -d), ndc);
    RON_CHECK(std::abs(ndc[0] - 1.0f) < 1e-5f);

    project(P, Vec3(0.0f, std::tan(fovy * 0.5f) * d, -d), ndc);
    RON_CHECK(std::abs(ndc[1] - 1.0f) < 1e-5f);

    // 4. mat4_inverse
    Mat4 VP = mat4_mul(P, v0);
    Mat4 invVP{};
    RON_CHECK(mat4_inverse(VP, invVP));

    // In DOUBLE multiplication M * inv(M) is identity within 1e-3 absolute
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) {
                sum += static_cast<double>(VP.m[k * 4 + r]) * static_cast<double>(invVP.m[c * 4 + k]);
            }
            double exp_val = (r == c) ? 1.0 : 0.0;
            RON_CHECK(std::abs(sum - exp_val) < 1e-3);
        }
    }

    Mat4 zeroM{};
    Mat4 dummyOut{};
    RON_CHECK(!mat4_inverse(zeroM, dummyOut));

    // 5. fill_frame_uniforms
    FrameUniforms fu{};
    fill_frame_uniforms(fu, v0, P, eye0, 1600, 900, 1.25f, 10.0f, 0.5f, Vec3(1.0f, 2.0f, 3.0f), 2.5f, Vec3(1.0f, 0.9f, 0.8f));
    RON_CHECK(fu.screen[0] == 1600.0f);
    RON_CHECK(fu.screen[1] == 900.0f);
    RON_CHECK(std::abs(fu.screen[2] - (1.0f / 1600.0f)) < 1e-7f);
    RON_CHECK(std::abs(fu.screen[3] - (1.0f / 900.0f)) < 1e-7f);

    for (int i = 0; i < 16; ++i) {
        RON_CHECK(std::abs(fu.view_proj[i] - VP.m[i]) < 1e-4f);
    }
    RON_CHECK(fu.time[3] == 0.0f);
    static_assert(sizeof(FrameUniforms) == 336);
}

RON_TEST(Cam_Control) {
    u16 flat_codes[64];
    std::fill(std::begin(flat_codes), std::end(flat_codes), static_cast<u16>(13107));
    HeightSampler flat_sampler;
    flat_sampler.init(flat_codes, 8);

    CameraRts cam;
    camera_init(cam, 2048.0f, 2048.0f, 0.0f, 300.0f);
    camera_snap(cam, &flat_sampler);

    CameraInput in{};
    in.pan_z = 1.0f;
    for (int k = 0; k < 60; ++k) {
        camera_update(cam, in, 1.0f / 60.0f, &flat_sampler);
    }
    RON_CHECK(std::abs(cam.target_z - (2048.0f - 270.0f)) < 0.05f);
    RON_CHECK(std::abs(cam.target_x - 2048.0f) < 1e-3f);

    CameraInput in0{};
    for (int k = 0; k < 240; ++k) {
        camera_update(cam, in0, 1.0f / 60.0f, &flat_sampler);
    }
    RON_CHECK(std::abs(cam.z - cam.target_z) < 0.01f);

    // New camera with yaw set to pi/2
    CameraRts cam2;
    camera_init(cam2, 2048.0f, 2048.0f, std::numbers::pi_v<float> * 0.5f, 300.0f);
    camera_snap(cam2, &flat_sampler);
    for (int k = 0; k < 30; ++k) {
        camera_update(cam2, in, 1.0f / 60.0f, &flat_sampler);
    }
    RON_CHECK(std::abs(cam2.target_x - (2048.0f + 135.0f)) < 0.05f);
    RON_CHECK(std::abs(cam2.target_z - 2048.0f) < 0.05f);

    // pan_x = +1 at yaw 0 moves target_x by +270 per second
    CameraRts cam3;
    camera_init(cam3, 2048.0f, 2048.0f, 0.0f, 300.0f);
    camera_snap(cam3, &flat_sampler);
    CameraInput in_pan_x{};
    in_pan_x.pan_x = 1.0f;
    for (int k = 0; k < 60; ++k) {
        camera_update(cam3, in_pan_x, 1.0f / 60.0f, &flat_sampler);
    }
    RON_CHECK(std::abs(cam3.target_x - (2048.0f + 270.0f)) < 0.05f);

    // rotate = +1 for 60 updates: target_yaw == 1.6 within 1e-3
    CameraRts cam4;
    camera_init(cam4, 2048.0f, 2048.0f, 0.0f, 300.0f);
    camera_snap(cam4, &flat_sampler);
    CameraInput in_rot{};
    in_rot.rotate = 1.0f;
    for (int k = 0; k < 60; ++k) {
        camera_update(cam4, in_rot, 1.0f / 60.0f, &flat_sampler);
    }
    RON_CHECK(std::abs(cam4.target_yaw - 1.6f) < 1e-3f);

    // zoom_steps = +1 once: target_dist == 255 (300*0.85) within 1e-3
    CameraRts cam5;
    camera_init(cam5, 2048.0f, 2048.0f, 0.0f, 300.0f);
    camera_snap(cam5, &flat_sampler);
    CameraInput in_zoom{};
    in_zoom.zoom_steps = 1.0f;
    camera_update(cam5, in_zoom, 1.0f / 60.0f, &flat_sampler);
    RON_CHECK(std::abs(cam5.target_dist - 255.0f) < 1e-3f);

    in_zoom.zoom_steps = 100.0f;
    camera_update(cam5, in_zoom, 1.0f / 60.0f, &flat_sampler);
    RON_CHECK(std::abs(cam5.target_dist - 30.0f) < 1e-3f);

    in_zoom.zoom_steps = -100.0f;
    camera_update(cam5, in_zoom, 1.0f / 60.0f, &flat_sampler);
    RON_CHECK(std::abs(cam5.target_dist - 1500.0f) < 1e-3f);

    // Pan bounds
    cam.target_x = 1.0f;
    cam.target_yaw = 0.0f;
    CameraInput in_left{};
    in_left.pan_x = -1.0f;
    for (int k = 0; k < 120; ++k) {
        camera_update(cam, in_left, 1.0f / 60.0f, &flat_sampler);
    }
    RON_CHECK(cam.target_x == 0.0f);

    CameraInput in_right{};
    in_right.pan_x = 1.0f;
    for (int k = 0; k < 6000; ++k) {
        camera_update(cam, in_right, 1.0f / 60.0f, &flat_sampler);
    }
    RON_CHECK(cam.target_x <= 4095.99f && cam.target_x >= 4095.0f);

    // Determinism
    CameraRts cA, cB;
    camera_init(cA, 1500.0f, 1500.0f, 0.5f, 200.0f);
    camera_snap(cA, &flat_sampler);
    camera_init(cB, 1500.0f, 1500.0f, 0.5f, 200.0f);
    camera_snap(cB, &flat_sampler);

    u32 state = 12345;
    auto lcg = [&]() -> float {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state & 0xFFFF) / 65535.0f * 2.0f - 1.0f;
    };
    for (int step = 0; step < 500; ++step) {
        CameraInput inp{ lcg(), lcg(), lcg(), lcg() };
        camera_update(cA, inp, 1.0f / 60.0f, &flat_sampler);
        camera_update(cB, inp, 1.0f / 60.0f, &flat_sampler);
    }
    RON_CHECK(std::memcmp(&cA, &cB, sizeof(CameraRts)) == 0);

    // Eye lift
    u16 c100 = height_m_to_code(100.0f);
    u16 c100_arr[64];
    std::fill(std::begin(c100_arr), std::end(c100_arr), c100);
    HeightSampler s100;
    s100.init(c100_arr, 8);

    CameraRts c_lift;
    camera_init(c_lift, 2048.0f, 2048.0f, 0.0f, 30.0f);
    camera_snap(c_lift, &s100);
    Vec3 eye = camera_eye(c_lift, &s100);
    RON_CHECK(eye.y >= 104.0f - 0.01f);
}

RON_TEST(Cam_PickFrustum) {
    u16 flat_codes[64];
    std::fill(std::begin(flat_codes), std::end(flat_codes), static_cast<u16>(13107));
    HeightSampler flat_sampler;
    flat_sampler.init(flat_codes, 8);

    CameraRts cam;
    camera_init(cam, 2048.0f, 2048.0f, 0.7f, 200.0f);
    camera_snap(cam, &flat_sampler);

    Mat4 view = camera_view(cam, &flat_sampler);
    Mat4 proj = camera_proj(16.0f / 9.0f);
    Mat4 view_proj = mat4_mul(proj, view);
    Mat4 inv_view_proj{};
    RON_CHECK(mat4_inverse(view_proj, inv_view_proj));

    float w = 1601.0f;
    float h = 901.0f;
    float ndc_x = (800.0f + 0.5f) / w * 2.0f - 1.0f;
    float ndc_y = 1.0f - (450.0f + 0.5f) / h * 2.0f;
    RON_CHECK(std::abs(ndc_x) < 1e-5f);
    RON_CHECK(std::abs(ndc_y) < 1e-5f);

    Vec3 ray_orig{}, ray_dir{};
    unproject_ray(inv_view_proj, 0.0f, 0.0f, ray_orig, ray_dir);

    // point-to-line distance from focus (2048, 0, 2048) to the ray < 0.01
    Vec3 focus(2048.0f, 0.0f, 2048.0f);
    Vec3 w_vec = focus - ray_orig;
    float proj_len = w_vec.dot(ray_dir);
    Vec3 closest = ray_orig + ray_dir * proj_len;
    float p2l_dist = (focus - closest).length();
    RON_CHECK(p2l_dist < 0.01f);

    // pick_terrain along that ray -> hit within 0.05 of the focus
    Vec3 hit{};
    bool pick_ok = pick_terrain(flat_sampler, ray_orig, ray_dir, 1000.0f, hit);
    RON_CHECK(pick_ok);
    RON_CHECK((hit - focus).length() < 0.05f);

    // Pixel (1200, 200)
    float px_ndc_x = (1200.0f + 0.5f) / w * 2.0f - 1.0f;
    float px_ndc_y = 1.0f - (200.0f + 0.5f) / h * 2.0f;
    Vec3 ray_orig2{}, ray_dir2{};
    unproject_ray(inv_view_proj, px_ndc_x, px_ndc_y, ray_orig2, ray_dir2);

    Vec3 hit2{};
    bool pick_ok2 = pick_terrain(flat_sampler, ray_orig2, ray_dir2, 2000.0f, hit2);
    RON_CHECK(pick_ok2);
    RON_CHECK(std::abs(hit2.y - 0.0f) < 0.01f);

    float hit_ndc[3];
    project(view_proj, hit2, hit_ndc);
    RON_CHECK(std::abs(hit_ndc[0] - px_ndc_x) < 1e-3f);
    RON_CHECK(std::abs(hit_ndc[1] - px_ndc_y) < 1e-3f);

    // Frustum checks
    Frustum frust = frustum_from_view_proj(view_proj);
    Vec3 eye = camera_eye(cam, &flat_sampler);
    Vec3 F = (focus - eye).normalize();
    Vec3 up(0.0f, 1.0f, 0.0f);
    Vec3 R = F.cross(up).normalize();

    auto point_in_frustum = [&](Vec3 pt) -> bool {
        for (int i = 0; i < 6; ++i) {
            float dist = frust.p[i].a * pt.x + frust.p[i].b * pt.y + frust.p[i].c * pt.z + frust.p[i].d;
            if (dist < 0.0f) return false;
        }
        return true;
    };

    RON_CHECK(point_in_frustum(focus));
    RON_CHECK(!point_in_frustum(eye - F * 10.0f));
    RON_CHECK(!point_in_frustum(eye + F * 17000.0f));
    RON_CHECK(!point_in_frustum(eye + F * 100.0f + R * 5000.0f));

    RON_CHECK(frustum_hits_aabb(frust, focus - Vec3(5.0f, 5.0f, 5.0f), focus + Vec3(5.0f, 5.0f, 5.0f)));
    Vec3 off_focus = focus + Vec3(20000.0f, 0.0f, 0.0f);
    RON_CHECK(!frustum_hits_aabb(frust, off_focus - Vec3(5.0f, 5.0f, 5.0f), off_focus + Vec3(5.0f, 5.0f, 5.0f)));
    RON_CHECK(frustum_hits_aabb(frust, focus - Vec3(2000.0f, 2000.0f, 2000.0f), focus + Vec3(2000.0f, 2000.0f, 2000.0f)));
    Vec3 behind_eye = eye - F * 200.0f;
    RON_CHECK(!frustum_hits_aabb(frust, behind_eye - Vec3(5.0f, 5.0f, 5.0f), behind_eye + Vec3(5.0f, 5.0f, 5.0f)));
}

} // namespace ron::render
