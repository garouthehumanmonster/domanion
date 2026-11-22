#include "render/pbr_ref.hpp"
#include <cmath>
#include <algorithm>

namespace ron::render {

float ggx_d(float NH, float a2) noexcept {
    constexpr float PI = 3.14159265358979323846f;
    float denom = NH * NH * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * denom * denom);
}

float smith_vis(float NL, float NV, float a2) noexcept {
    float termL = NL * std::sqrt(NV * NV * (1.0f - a2) + a2);
    float termV = NV * std::sqrt(NL * NL * (1.0f - a2) + a2);
    return 0.5f / std::max(termL + termV, 1e-5f);
}

Vec3 fresnel_schlick(const Vec3& F0, float VH) noexcept {
    float p = std::pow(std::clamp(1.0f - VH, 0.0f, 1.0f), 5.0f);
    return F0 + (Vec3(1.0f, 1.0f, 1.0f) - F0) * p;
}

inline Vec3 mul3(const Vec3& a, const Vec3& b) noexcept {
    return Vec3(a.x * b.x, a.y * b.y, a.z * b.z);
}

Vec3 pbr_shade(const Vec3& albedo, float rough, float metal, float ao,
               const Vec3& N, const Vec3& V, const Vec3& L,
               const Vec3& sun_rgb, float vis,
               const Vec3& sky, const Vec3& ground, float sky_i) noexcept
{
    constexpr float PI = 3.14159265358979323846f;

    float rc = std::clamp(rough, 0.04f, 1.0f);
    float a = rc * rc;
    float a2 = a * a;

    float NL = std::clamp(N.dot(L), 0.0f, 1.0f);
    float NV = std::max(N.dot(V), 1e-4f);
    Vec3 H = (L + V).normalize();
    float NH = std::clamp(N.dot(H), 0.0f, 1.0f);
    float VH = std::clamp(V.dot(H), 0.0f, 1.0f);

    float D = ggx_d(NH, a2);
    float Vis = smith_vis(NL, NV, a2);
    Vec3 F0 = Vec3(0.04f, 0.04f, 0.04f) * (1.0f - metal) + albedo * metal;
    Vec3 F = fresnel_schlick(F0, VH);

    Vec3 diffuse = mul3((Vec3(1.0f, 1.0f, 1.0f) - F) * ((1.0f - metal) * NL), albedo);
    Vec3 spec = F * (D * Vis * NL * PI);
    Vec3 direct = mul3(diffuse + spec, sun_rgb) * vis;

    auto hemi = [&](const Vec3& n) -> Vec3 {
        float factor = n.y * 0.5f + 0.5f;
        return (ground * (1.0f - factor) + sky * factor) * sky_i;
    };

    Vec3 amb_diff = mul3(albedo * ((1.0f - metal) * ao), hemi(N));

    // Karis envBRDF approximation
    // c0=(-1, -0.0275, -0.572, 0.022), c1=(1, 0.0425, 1.04, -0.04)
    float rx = rc * (-1.0f) + 1.0f;
    float ry = rc * (-0.0275f) + 0.0425f;
    float rz = rc * (-0.572f) + 1.04f;
    float rw = rc * 0.022f - 0.04f;

    float a004 = std::min(rx * rx, std::exp2(-9.28f * NV)) * rx + ry;
    float AB_x = -1.04f * a004 + rz;
    float AB_y =  1.04f * a004 + rw;

    Vec3 R_refl = N * (2.0f * N.dot(V)) - V;
    Vec3 amb_spec = mul3(F0 * AB_x + Vec3(AB_y, AB_y, AB_y), hemi(R_refl) * ao);

    return direct + amb_diff + amb_spec;
}

} // namespace ron::render
