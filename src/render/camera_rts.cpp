#include "render/camera_rts.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace ron::render {

void camera_init(CameraRts& cam, float x, float z, float yaw, float dist) {
    cam.target_x = cam.x = x;
    cam.target_z = cam.z = z;
    cam.target_yaw = cam.yaw = yaw;
    cam.target_dist = cam.dist = dist;
    cam.y = 0.0f;
}

void camera_snap(CameraRts& cam, const HeightSampler* sampler) {
    cam.x = cam.target_x;
    cam.z = cam.target_z;
    cam.yaw = cam.target_yaw;
    cam.dist = cam.target_dist;
    cam.y = (sampler != nullptr) ? std::max(0.0f, sampler->height_m(cam.x, cam.z)) : 0.0f;
}

float camera_pitch(float dist) {
    float clamped_dist = std::clamp(dist, CAM_DIST_MIN, CAM_DIST_MAX);
    float t = std::log(clamped_dist / CAM_DIST_MIN) / std::log(CAM_DIST_MAX / CAM_DIST_MIN);
    t = std::clamp(t, 0.0f, 1.0f);
    float deg = CAM_PITCH_NEAR_DEG + (CAM_PITCH_FAR_DEG - CAM_PITCH_NEAR_DEG) * t;
    return deg * (std::numbers::pi_v<float> / 180.0f);
}

void camera_update(CameraRts& cam, const CameraInput& in, float dt, const HeightSampler* sampler) {
    float speed = 0.9f * cam.target_dist;
    float sin_yaw = std::sin(cam.target_yaw);
    float cos_yaw = std::cos(cam.target_yaw);
    Vec3 f_h(sin_yaw, 0.0f, -cos_yaw);
    Vec3 r_h(cos_yaw, 0.0f, sin_yaw);

    Vec3 delta = (r_h * in.pan_x + f_h * in.pan_z) * (speed * dt);
    cam.target_x = std::clamp(cam.target_x + delta.x, 0.0f, 4095.99f);
    cam.target_z = std::clamp(cam.target_z + delta.z, 0.0f, 4095.99f);
    cam.target_yaw += in.rotate * 1.6f * dt;
    cam.target_dist = std::clamp(cam.target_dist * std::pow(0.85f, in.zoom_steps), CAM_DIST_MIN, CAM_DIST_MAX);

    float alpha_horiz = 1.0f - std::exp(-12.0f * dt);
    cam.x += (cam.target_x - cam.x) * alpha_horiz;
    cam.z += (cam.target_z - cam.z) * alpha_horiz;
    cam.yaw += (cam.target_yaw - cam.yaw) * alpha_horiz;
    cam.dist += (cam.target_dist - cam.dist) * alpha_horiz;

    float target_y = (sampler != nullptr) ? std::max(0.0f, sampler->height_m(cam.x, cam.z)) : 0.0f;
    float alpha_y = 1.0f - std::exp(-8.0f * dt);
    cam.y += (target_y - cam.y) * alpha_y;
}

Vec3 camera_eye(const CameraRts& cam, const HeightSampler* sampler) {
    float p = camera_pitch(cam.dist);
    float cos_p = std::cos(p);
    float sin_p = std::sin(p);
    float sin_yaw = std::sin(cam.yaw);
    float cos_yaw = std::cos(cam.yaw);
    Vec3 f_h(sin_yaw, 0.0f, -cos_yaw);
    Vec3 focus(cam.x, cam.y, cam.z);

    Vec3 eye = focus - f_h * (cam.dist * cos_p) + Vec3(0.0f, cam.dist * sin_p, 0.0f);
    if (sampler != nullptr) {
        float ground = std::max(0.0f, sampler->height_m(eye.x, eye.z));
        eye.y = std::max(eye.y, ground + 4.0f);
    }
    return eye;
}

Mat4 camera_view(const CameraRts& cam, const HeightSampler* sampler) {
    Vec3 eye = camera_eye(cam, sampler);
    Vec3 target(cam.x, cam.y, cam.z);
    Vec3 up(0.0f, 1.0f, 0.0f);
    return mat4_look_at(eye, target, up);
}

Mat4 camera_proj(float aspect) {
    float fovy = CAM_FOV_Y_DEG * (std::numbers::pi_v<float> / 180.0f);
    return mat4_perspective_reverse_z(fovy, aspect, CAM_NEAR, CAM_FAR);
}

} // namespace ron::render
