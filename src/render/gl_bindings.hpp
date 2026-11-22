#pragma once

#include "core/types.hpp"

// Single X-macro list defining all shader/engine bindings.
// Format: X(NAME, C_VALUE, C_TYPE, GLSL_STRING_VALUE)
#define RON_GL_BINDINGS(X) \
    X(BIND_UBO_FRAME, 0, u32, "0") \
    X(BIND_SSBO_INSTANCES, 0, u32, "0") \
    X(BIND_SSBO_VISIBLE, 1, u32, "1") \
    X(BIND_SSBO_INDIRECT, 2, u32, "2") \
    X(BIND_SSBO_COUNTERS, 3, u32, "3") \
    X(BIND_SSBO_TEST_IN, 6, u32, "6") \
    X(BIND_SSBO_TEST_OUT, 7, u32, "7") \
    X(TEX_HDR, 0, u32, "0") \
    X(BIND_UBO_TERRAIN, 1, u32, "1") \
    X(BIND_SSBO_TERRAIN_NODES, 4, u32, "4") \
    X(TEX_TERRAIN_HEIGHT, 1, u32, "1") \
    X(TEX_TERRAIN_BIOME, 2, u32, "2") \
    X(TEX_TERRAIN_ALBEDO, 3, u32, "3") \
    X(TEX_TERRAIN_NORMAL, 4, u32, "4") \
    X(TEX_TERRAIN_RH, 5, u32, "5") \
    X(TEX_OCEAN_DEPTH, 6, u32, "6") \
    X(BIND_UBO_OVERLAY, 2, u32, "2") \
    X(BIND_UBO_MINIMAP, 3, u32, "3") \
    X(BIND_SSBO_SPLATS, 5, u32, "5") \
    X(BIND_SSBO_BLIPS, 8, u32, "8") \
    X(TEX_TERRITORY_SDF, 7, u32, "7") \
    X(TEX_TERRITORY_OWNER, 8, u32, "8") \
    X(TEX_CRATER, 9, u32, "9") \
    X(TEX_FOG, 10, u32, "10") \
    X(TEX_MINIMAP_BASE, 11, u32, "11") \
    X(TEX_FOG_TVIS, 12, u32, "12") \
    X(TEX_FOG_TEXP, 13, u32, "13") \
    X(IMG_JFA_IN, 0, u32, "0") \
    X(IMG_JFA_OUT, 1, u32, "1") \
    X(IMG_SDF, 2, u32, "2") \
    X(IMG_CRATER, 3, u32, "3") \
    X(IMG_FOG_SMOOTH, 4, u32, "4") \
    X(RON_HEIGHT_MIN, -128.0f, float, "-128.0") \
    X(RON_HEIGHT_RANGE, 640.0f, float, "640.0") \
    X(RON_WORLD_SIZE, 4096.0f, float, "4096.0") \
    X(RON_REVERSE_Z, 1, u32, "1") \
    X(BIND_UBO_ENTITY, 4, u32, "4") \
    X(BIND_SSBO_MODELS, 9, u32, "9") \
    X(BIND_SSBO_BUCKETS, 10, u32, "10") \
    X(TEX_VAT, 14, u32, "14") \
    X(TEX_TRIM_ALBEDO, 15, u32, "15") \
    X(TEX_TRIM_NORMAL, 16, u32, "16") \
    X(TEX_TRIM_ORM, 17, u32, "17") \
    X(TEX_TRIM_EMISSIVE, 18, u32, "18") \
    X(RON_MAX_BUCKETS, 384, u32, "384") \
    X(RON_VAT_FRAMES, 44, u32, "44") \
    X(RON_ANIM_FPS, 16.0f, float, "16.0") \
    X(RON_CULL_PX, 1.5f, float, "1.5") \
    X(RON_ENTITY_ALBEDO_DECODE, 0, u32, "0") \
    X(BIND_UBO_LIGHT, 5, u32, "5") \
    X(BIND_UBO_MARKS, 6, u32, "6") \
    X(BIND_SSBO_ICONS, 11, u32, "11") \
    X(BIND_SSBO_BARS, 12, u32, "12") \
    X(BIND_SSBO_MARK_CMDS, 13, u32, "13") \
    X(BIND_SSBO_MODEL_MARKS, 14, u32, "14") \
    X(TEX_SHADOW_MAP, 19, u32, "19") \
    X(TEX_HEIGHT_LINEAR, 20, u32, "20") \
    X(TEX_MSDF, 21, u32, "21") \
    X(RON_SHADOW_CASCADES, 4, u32, "4") \
    X(RON_TERRAIN_SHADOW_STEPS, 48, u32, "48") \
    X(RON_TERRAIN_SHADOW_REACH, 1100.0f, float, "1100.0") \
    X(RON_TERRAIN_SHADOW_BIAS, 0.5f, float, "0.5")

namespace ron::render {

// Generate constexpr C++ constants
#define RON_GL_BINDING_CONST(name, c_val, c_type, str_val) \
    constexpr c_type name = c_val;

RON_GL_BINDINGS(RON_GL_BINDING_CONST)
#undef RON_GL_BINDING_CONST

struct BindingDefine {
    const char* name;
    const char* value;
};

extern const BindingDefine BINDING_DEFINES[];
extern const u32 BINDING_DEFINE_COUNT;

} // namespace ron::render
