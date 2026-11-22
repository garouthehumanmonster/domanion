#pragma once

#include "core/types.hpp"
#include "render/rmath.hpp"

namespace ron::render {

float ggx_d(float NH, float a2) noexcept;
float smith_vis(float NL, float NV, float a2) noexcept;
Vec3 fresnel_schlick(const Vec3& F0, float VH) noexcept;
Vec3 pbr_shade(const Vec3& albedo, float rough, float metal, float ao,
               const Vec3& N, const Vec3& V, const Vec3& L,
               const Vec3& sun_rgb, float vis,
               const Vec3& sky, const Vec3& ground, float sky_i) noexcept;

} // namespace ron::render
