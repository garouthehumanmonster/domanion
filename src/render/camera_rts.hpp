#pragma once

#include "core/types.hpp"
#include "render/rmath.hpp"
#include "render/height_sampler.hpp"

namespace ron::render {

inline constexpr float CAM_DIST_MIN = 30.0f;
inline constexpr float CAM_DIST_MAX = 1500.0f;
inline constexpr float CAM_FOV_Y_DEG = 45.0f;
inline constexpr float CAM_NEAR = 1.0f;
inline constexpr float CAM_FAR = 16000.0f;
inline constexpr float CAM_PITCH_NEAR_DEG = 28.0f;
inline constexpr float CAM_PITCH_FAR_DEG = 62.0f;

struct CameraRts {
    float target_x{2048.0f};
    float target_z{2048.0f};
    float target_yaw{0.0f};
    float target_dist{300.0f};
    float x{2048.0f};
    float z{2048.0f};
    float yaw{0.0f};
    float dist{300.0f};
    float y{0.0f};
};

struct CameraInput {
    float pan_x{0.0f};      // +right
    float pan_z{0.0f};      // +forward
    float rotate{0.0f};     // +yaw
    float zoom_steps{0.0f}; // + = zoom in
};

void camera_init(CameraRts& cam, float x, float z, float yaw, float dist);
void camera_snap(CameraRts& cam, const HeightSampler* sampler);
void camera_update(CameraRts& cam, const CameraInput& in, float dt, const HeightSampler* sampler);
float camera_pitch(float dist); // radians
Vec3 camera_eye(const CameraRts& cam, const HeightSampler* sampler);
Mat4 camera_view(const CameraRts& cam, const HeightSampler* sampler);
Mat4 camera_proj(float aspect);

} // namespace ron::render
