#ifndef PBR_LITE_GLSL
#define PBR_LITE_GLSL

#ifndef PBR_PI
#define PBR_PI 3.14159265358979323846
#endif

float ggx_d(float NH, float a2) {
    float denom = NH * NH * (a2 - 1.0) + 1.0;
    return a2 / (PBR_PI * denom * denom);
}

float smith_vis(float NL, float NV, float a2) {
    float termL = NL * sqrt(NV * NV * (1.0 - a2) + a2);
    float termV = NV * sqrt(NL * NL * (1.0 - a2) + a2);
    return 0.5 / max(termL + termV, 1e-5);
}

vec3 fresnel_schlick(vec3 F0, float VH) {
    float p = pow(clamp(1.0 - VH, 0.0, 1.0), 5.0);
    return F0 + (vec3(1.0) - F0) * p;
}

vec3 hemi_ambient(vec3 n, vec3 sky, vec3 ground, float sky_i) {
    float factor = n.y * 0.5 + 0.5;
    return mix(ground, sky, factor) * sky_i;
}

vec3 pbr_shade(vec3 albedo, float rough, float metal, float ao,
               vec3 N, vec3 V, vec3 L,
               vec3 sun_rgb, float vis,
               vec3 sky, vec3 ground, float sky_i)
{
    float rc = clamp(rough, 0.04, 1.0);
    float a = rc * rc;
    float a2 = a * a;

    float NL = clamp(dot(N, L), 0.0, 1.0);
    float NV = max(dot(N, V), 1e-4);
    vec3 H = normalize(L + V);
    float NH = clamp(dot(N, H), 0.0, 1.0);
    float VH = clamp(dot(V, H), 0.0, 1.0);

    float D = ggx_d(NH, a2);
    float Vis = smith_vis(NL, NV, a2);
    vec3 F0 = mix(vec3(0.04), albedo, metal);
    vec3 F = fresnel_schlick(F0, VH);

    vec3 diffuse = (vec3(1.0) - F) * ((1.0 - metal) * NL) * albedo;
    vec3 spec = F * (D * Vis * NL * PBR_PI);
    vec3 direct = (diffuse + spec) * sun_rgb * vis;

    vec3 amb_diff = albedo * ((1.0 - metal) * ao) * hemi_ambient(N, sky, ground, sky_i);

    // Karis envBRDF approximation
    float rx = rc * (-1.0) + 1.0;
    float ry = rc * (-0.0275) + 0.0425;
    float rz = rc * (-0.572) + 1.04;
    float rw = rc * 0.022 - 0.04;

    float a004 = min(rx * rx, exp2(-9.28 * NV)) * rx + ry;
    float AB_x = -1.04 * a004 + rz;
    float AB_y =  1.04 * a004 + rw;

    vec3 R_refl = N * (2.0 * dot(N, V)) - V;
    vec3 amb_spec = (F0 * AB_x + vec3(AB_y)) * (hemi_ambient(R_refl, sky, ground, sky_i) * ao);

    return direct + amb_diff + amb_spec;
}

#endif // PBR_LITE_GLSL
