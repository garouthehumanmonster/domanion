#include "render/lighting_system.hpp"
#include "render/entity_renderer.hpp"
#include "core/assert_log.hpp"
#include <glad/gl.h>

namespace ron::render {

bool LightingSystem::init(VramLedger& ledger, ShaderLibrary& shaders) noexcept {
    destroy();
    (void)shaders;

    // 1. Cascaded Shadow Map Texture Array (2048x2048x4, D32F = 64 MiB)
    if (!shadow_map_.create_2d_array(ledger, VramCat::ShadowMaps, "shadow_map_array",
                                    TexFmt::D32F, SHADOW_RES, SHADOW_RES, SHADOW_CASCADES, 1)) {
        RON_LOG_ERROR("LightingSystem::init: failed to create shadow map array");
        destroy();
        return false;
    }

    // Sampler state on texture: linear filtering, clamp to edge, depth comparison (Reverse-Z: GL_GREATER)
    glTextureParameteri(shadow_map_.id(), GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(shadow_map_.id(), GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(shadow_map_.id(), GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(shadow_map_.id(), GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(shadow_map_.id(), GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(shadow_map_.id(), GL_TEXTURE_COMPARE_FUNC, GL_GREATER);

    // 2. 4 FBOs (one per cascade layer)
    glCreateFramebuffers(4, shadow_fbo_);
    for (u32 c = 0; c < 4; ++c) {
        glNamedFramebufferTextureLayer(shadow_fbo_[c], GL_DEPTH_ATTACHMENT, shadow_map_.id(), 0, static_cast<GLint>(c));
        glNamedFramebufferDrawBuffer(shadow_fbo_[c], GL_NONE);
        glNamedFramebufferReadBuffer(shadow_fbo_[c], GL_NONE);

        GLenum status = glCheckNamedFramebufferStatus(shadow_fbo_[c], GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            RON_LOG_ERROR("LightingSystem::init: shadow FBO %u incomplete (status 0x%X)", c, status);
            destroy();
            return false;
        }
    }

    // 3. Linear Height Sampler
    glCreateSamplers(1, &height_linear_sampler_);
    glSamplerParameteri(height_linear_sampler_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(height_linear_sampler_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(height_linear_sampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(height_linear_sampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 4. Lighting UBO (368 B) in VramCat::ShadowMaps
    if (!lighting_ubo_.create(ledger, VramCat::ShadowMaps, "lighting_ubo",
                              sizeof(LightingUbo), nullptr, GL_DYNAMIC_STORAGE_BIT)) {
        RON_LOG_ERROR("LightingSystem::init: failed to create lighting UBO");
        destroy();
        return false;
    }

    return true;
}

void LightingSystem::update(const FrameUniforms& fu, const CameraRts& cam, u32 flags) noexcept {
    (void)cam;
    float L[3];
    light_to_sun(fu, L);
    cascade_fit(fu, L, static_cast<int>(SHADOW_RES), &cascades_);
    lighting_fill_ubo(cascades_, fu, flags, &cpu_ubo_);
    if (lighting_ubo_.id() != 0) {
        glNamedBufferSubData(lighting_ubo_.id(), 0, sizeof(LightingUbo), &cpu_ubo_);
    }
}

void LightingSystem::bind(u32 height_tex_id) const noexcept {
    if (shadow_map_.id() != 0) {
        glBindTextureUnit(TEX_SHADOW_MAP, shadow_map_.id());
    }
    if (height_tex_id != 0) {
        glBindTextureUnit(TEX_HEIGHT_LINEAR, height_tex_id);
        if (height_linear_sampler_ != 0) {
            glBindSampler(TEX_HEIGHT_LINEAR, height_linear_sampler_);
        }
    }
    if (lighting_ubo_.id() != 0) {
        glBindBufferBase(GL_UNIFORM_BUFFER, BIND_UBO_LIGHT, lighting_ubo_.id());
    }
}

void LightingSystem::bind(const TerrainSource& terrain_source) const noexcept {
    (void)terrain_source;
    bind(0);
}

void LightingSystem::bind(const TerrainRenderer& terrain) const noexcept {
    bind(terrain.height_tex().id());
}

void LightingSystem::render_shadows(EntityRenderer& entity_renderer, const ShaderLibrary& shaders) {
    u32 shadow_prog = shaders.program(ProgramId::ShadowEntity);
    if (shadow_prog == 0) return;

    for (u32 c = 0; c < 4; ++c) {
        glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_[c]);
        glViewport(0, 0, static_cast<GLsizei>(SHADOW_RES), static_cast<GLsizei>(SHADOW_RES));

        const float clear_d = 0.0f; // Reverse-Z: clear depth to 0.0
        glClearNamedFramebufferfv(shadow_fbo_[c], GL_DEPTH, 0, &clear_d);

        entity_renderer.draw_shadow_cascade(static_cast<int>(c), shadow_prog, lighting_ubo_);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void LightingSystem::destroy() noexcept {
    for (u32& fbo : shadow_fbo_) {
        if (fbo != 0) {
            glDeleteFramebuffers(1, &fbo);
            fbo = 0;
        }
    }
    if (height_linear_sampler_ != 0) {
        glDeleteSamplers(1, &height_linear_sampler_);
        height_linear_sampler_ = 0;
    }
    shadow_map_.destroy();
    lighting_ubo_.destroy();
}

} // namespace ron::render
