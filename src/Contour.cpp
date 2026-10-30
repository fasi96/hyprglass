#include "Contour.hpp"
#include "Diagnostics.hpp"
#include "Globals.hpp"

#include <array>
#include <format>
#include <string_view>
#include <GLES3/gl32.h>
#include <drm_fourcc.h>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

namespace Contour {

namespace {

GLuint framebufferId(const SP<Render::IFramebuffer>& framebuffer) {
    return dynamic_cast<Render::GL::CGLFramebuffer*>(framebuffer.get())->getFBID();
}

// The only format the field can be: a signed distance needs a float, and DRM has no
// single-channel float format.
constexpr DRMFormat FIELD_FORMAT = DRM_FORMAT_ABGR16161616F;

// Maps the VAO's [0,1] positions to clip space, as blurBackground() does.
constexpr std::array<float, 9> FULLSCREEN_PROJECTION = {
    2.0f, 0.0f, 0.0f,
    0.0f, 2.0f, 0.0f,
   -1.0f,-1.0f, 1.0f,
};

// Scratch targets only ever grow: a render target a few texels bigger than the field is
// fine (the passes only touch the field's own corner), and realloc on every other layer's
// build would not be. Growing each side on its own would turn a wide bar and a tall dock
// into a screen-sized target, so growth stops at the field budget.
bool ensureFramebuffer(SP<Render::IFramebuffer>& framebuffer, const char* name, int width, int height, bool growOnly) {
    if (!framebuffer)
        framebuffer = g_pHyprRenderer->createFB(name);

    if (growOnly) {
        const int currentWidth  = static_cast<int>(framebuffer->m_size.x);
        const int currentHeight = static_cast<int>(framebuffer->m_size.y);
        const int grownWidth    = std::max(width, currentWidth);
        const int grownHeight   = std::max(height, currentHeight);

        if (static_cast<long>(grownWidth) * grownHeight <= MAX_TEXELS) {
            width  = grownWidth;
            height = grownHeight;
        } else if (currentWidth >= width && currentHeight >= height) {
            width  = currentWidth;
            height = currentHeight;
        }
    }

    framebuffer->alloc(width, height, FIELD_FORMAT);

    // An allocation that failed (out of GPU memory) leaves no texture to render into.
    if (!framebuffer->isAllocated() || !framebuffer->getTexture()) {
        framebuffer.reset();
        return false;
    }
    return true;
}

} // namespace

bool gpuSupportsField() {
    auto& supported = g_pGlobalState->contourGpuSupported;
    if (supported)
        return *supported;

    // Half-float render targets are core from GLES 3.2; before that, an extension.
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    bool ok = major > 3 || (major == 3 && minor >= 2);
    if (!ok) {
        GLint count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &count);
        for (GLint i = 0; i < count && !ok; i++) {
            const auto* name = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
            ok               = name && std::string_view(name) == "GL_EXT_color_buffer_float";
        }
    }

    supported = ok;
    return ok;
}

CField::~CField() {
    release();
}

CField::EInvalidation CField::invalidate(std::chrono::steady_clock::time_point now) noexcept {
    if (!m_requested)
        return EInvalidation::None;

    m_uncoveredFrames = 0;

    if (m_dirty)
        return EInvalidation::None;

    if (!m_field) {
        m_dirty = true;
        return EInvalidation::None;
    }

    if (now - m_lastBuild < MIN_REBUILD_INTERVAL) {
        m_deferredAt = m_lastBuild + MIN_REBUILD_INTERVAL;
        return EInvalidation::Deferred;
    }

    m_deferredAt.reset();
    m_dirty = true;
    return EInvalidation::Now;
}

CField::EInvalidation CField::flushDeferred(std::chrono::steady_clock::time_point now) noexcept {
    if (!m_deferredAt)
        return EInvalidation::None;

    if (now < *m_deferredAt)
        return EInvalidation::Deferred;

    m_deferredAt.reset();
    m_dirty           = true;
    m_uncoveredFrames = 0;
    return EInvalidation::Now;
}

bool CField::build(const SBuildKey& key, const SP<Render::IFramebuffer>& layerRender, const CBox& box, float fadeAlpha,
                   const SP<Render::IFramebuffer>& callerFramebuffer) {
    if (!g_pGlobalState || !layerRender || !layerRender->getTexture() || !callerFramebuffer)
        return false;

    auto& state = *g_pGlobalState;
    if (state.contourUnavailable)
        return false;

    if (!gpuSupportsField()) {
        state.contourUnavailable = true;
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] This GPU cannot render the contour field, layers fall back to mask_mode = alpha", PLUGIN_NAME),
            CHyprColor{1.0, 0.6, 0.2, 1.0}, 8000);
        return false;
    }

    // Before any GL binding: the first use compiles the programs, which rebinds the VAO and array buffer.
    const auto* programs = state.shaderManager.contourPrograms();
    if (!programs) {
        state.contourUnavailable = true;
        return false;
    }

    const SPlan& plan = key.plan;

    // Allocation re-binds the renderer's own framebuffer and viewport; everything below binds its own.
    const bool allocated = ensureFramebuffer(m_field, "hyprglass-contour", plan.fieldWidth, plan.fieldHeight, false) &&
                           ensureFramebuffer(state.contourScratch.first, "hyprglass-contour-a", plan.fieldWidth, plan.fieldHeight, true) &&
                           ensureFramebuffer(state.contourScratch.second, "hyprglass-contour-b", plan.fieldWidth, plan.fieldHeight, true);
    if (m_field && !m_counted) {
        m_counted = true;
        state.contourScratch.users++;
    }
    if (!allocated) {
        release();
        return false;
    }

    const auto& first  = state.contourScratch.first;
    const auto& second = state.contourScratch.second;

    // Each pass overwrites its whole target: a leaked scissor or stencil would leave texels
    // unwritten, blending would mix in what the shared scratch held before.
    g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false);
    if (glIsEnabled(GL_SCISSOR_TEST)) {
        glDisable(GL_SCISSOR_TEST);
        Diagnostics::recordStateDesync("scissor on before the contour passes");
    }
    if (glIsEnabled(GL_STENCIL_TEST)) {
        glDisable(GL_STENCIL_TEST);
        Diagnostics::recordStateDesync("stencil on before the contour passes");
    }
    g_pHyprRenderer->blend(false);
    g_pHyprOpenGL->setViewport(0, 0, plan.fieldWidth, plan.fieldHeight);
    glActiveTexture(GL_TEXTURE0);

    const float fieldWidth  = static_cast<float>(plan.fieldWidth);
    const float fieldHeight = static_cast<float>(plan.fieldHeight);
    const float divisor     = static_cast<float>(plan.divisor);

    const auto  pass = [&](const SP<CShader>& program, const SP<Render::IFramebuffer>& target, const SP<Render::IFramebuffer>& source, auto&& setUniforms) {
        auto shader = g_pHyprOpenGL->useShader(program);
        shader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, FULLSCREEN_PROJECTION);
        setUniforms();
        glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));
        glBindFramebuffer(GL_FRAMEBUFFER, framebufferId(target));
        source->getTexture()->bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };

    // 1. which texels are inside the outline: the layer's alpha against the mask threshold,
    //    scaled by its fade so a layer part way through a fade keeps its shape
    {
        const auto& uniforms = programs->seedUniforms;
        pass(programs->seed, first, layerRender, [&] {
            glUniform1i(uniforms.tex, 0);
            glUniform2f(uniforms.boxOffsetPx, static_cast<float>(box.x), static_cast<float>(box.y));
            glUniform2f(uniforms.boxSizePx, static_cast<float>(plan.boxWidth), static_cast<float>(plan.boxHeight));
            glUniform2f(uniforms.sourceSizePx, static_cast<float>(layerRender->m_size.x), static_cast<float>(layerRender->m_size.y));
            glUniform1f(uniforms.divisor, divisor);
            glUniform1f(uniforms.threshold, key.threshold * std::clamp(fadeAlpha, 0.0f, 1.0f));
        });
    }

    // 2-3. exact distance transform, one axis then the other; the last one resolves the signed distance
    {
        const auto& uniforms = programs->edtUniforms;
        const auto  common = [&] {
            glUniform1i(uniforms.tex, 0);
            glUniform2f(uniforms.fieldSize, fieldWidth, fieldHeight);
            glUniform1i(uniforms.reachTexels, plan.reachTexels);
            glUniform1f(uniforms.texelPx, divisor);
            glUniform1f(uniforms.reachPx, plan.reachPx);
        };
        pass(programs->edt, second, first, [&] {
            common();
            glUniform2f(uniforms.direction, 0.0f, 1.0f);
            glUniform1i(uniforms.resolveSigned, 0);
        });
        pass(programs->edt, first, second, [&] {
            common();
            glUniform2f(uniforms.direction, 1.0f, 0.0f);
            glUniform1i(uniforms.resolveSigned, 1);
        });
    }

    // 4-5. round the creases where distances from two edges meet
    {
        const auto& uniforms = programs->smoothUniforms;
        const auto  common = [&] {
            glUniform1i(uniforms.tex, 0);
            glUniform2f(uniforms.fieldSize, fieldWidth, fieldHeight);
            glUniform1i(uniforms.radius, plan.smoothRadius);
            glUniform1f(uniforms.negHalfInvSigmaSq, plan.smoothNegHalfInvSigmaSq);
        };
        pass(programs->smooth, second, first, [&] {
            common();
            glUniform2f(uniforms.direction, 1.0f, 0.0f);
        });
        pass(programs->smooth, first, second, [&] {
            common();
            glUniform2f(uniforms.direction, 0.0f, 1.0f);
        });
    }

    // 6. the field the glass shader reads: distance and gradient
    {
        const auto& uniforms = programs->packUniforms;
        pass(programs->pack, m_field, first, [&] {
            glUniform1i(uniforms.tex, 0);
            glUniform2f(uniforms.fieldSize, fieldWidth, fieldHeight);
            glUniform1f(uniforms.texelPx, divisor);
        });
    }

    // Hand back the state blurBackground() hands back: Hyprland's blending at every element
    // boundary, the caller's framebuffer and the viewport of its own size.
    g_pHyprRenderer->blend(true);
    glBindFramebuffer(GL_FRAMEBUFFER, framebufferId(callerFramebuffer));
    glBindVertexArray(0);
    g_pHyprOpenGL->setViewport(0, 0, static_cast<int>(callerFramebuffer->m_size.x), static_cast<int>(callerFramebuffer->m_size.y));

    if (const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock())
        Diagnostics::recordContourRebuild(monitor->m_id);

    m_builtKey        = key;
    m_dirty           = false;
    m_uncoveredFrames = 0;
    m_deferredAt.reset();
    m_lastBuild       = std::chrono::steady_clock::now();
    return true;
}

void CField::release() noexcept {
    // every glassed layer calls this each frame while it does not use contour: nothing held is nothing to do
    if (!m_field && !m_requested && !m_counted)
        return;

    m_field.reset();
    m_builtKey        = {};
    m_requested       = false;
    m_dirty           = false;
    m_uncoveredFrames = 0;
    m_deferredAt.reset();

    if (!m_counted)
        return;
    m_counted = false;

    if (!g_pGlobalState)
        return;

    auto& scratch = g_pGlobalState->contourScratch;
    if (--scratch.users <= 0) {
        scratch.users = 0;
        scratch.first.reset();
        scratch.second.reset();
    }
}

} // namespace Contour
