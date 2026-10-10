#include "LiquidSim.hpp"
#include "Globals.hpp"
#include "Shaders.hpp"

#include <array>
#include <cmath>
#include <format>
#include <initializer_list>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/OpenGL.hpp>

// ── Programs ────────────────────────────────────────────────────────────────

static bool compile(CLiquidPrograms::SProgram& program, const char* file, std::initializer_list<const char*> uniforms) {
    const auto source = SHADERS.find(file);
    if (source == SHADERS.end() || !program.shader->createProgram(g_pHyprOpenGL->m_shaders->TEXVERTSRC, source->second, true))
        return false;
    const auto id = program.shader->program();
    for (const char* name : uniforms)
        program.loc[name] = glGetUniformLocation(id, name);
    return true;
}

bool CLiquidPrograms::ensure() {
    if (m_tried)
        return m_ok;
    m_tried = true;

    m_ok = compile(splat, "liquid_splat.frag", {"tex", "texel", "seg", "push", "shape"}) &&
        compile(advect, "liquid_advect.frag", {"tex", "uVel", "texel", "dt", "dissipation", "velScale"}) &&
        compile(displace, "liquid_displace.frag", {"tex", "uVel", "uDye", "texel", "dt", "decay", "drag"}) &&
        compile(divergence, "liquid_divergence.frag", {"tex", "texel"}) &&
        compile(curl, "liquid_curl.frag", {"tex", "texel"}) &&
        compile(vorticity, "liquid_vorticity.frag", {"tex", "uCurl", "texel", "curl", "dt"}) &&
        compile(scale, "liquid_scale.frag", {"tex", "texel", "value"}) &&
        compile(pressure, "liquid_pressure.frag", {"tex", "uDiv", "texel"}) &&
        compile(gradient, "liquid_gradient.frag", {"tex", "uVel", "texel"});

    if (!m_ok)
        HyprlandAPI::addNotification(PHANDLE, std::format("[{}] Liquid Touch: failed to compile its shaders, liquid is off", PLUGIN_NAME),
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
    return m_ok;
}

void CLiquidPrograms::destroy() noexcept {
    for (auto* program : {&splat, &advect, &displace, &divergence, &curl, &vorticity, &scale, &pressure, &gradient})
        program->shader->destroy();
    m_tried = false;
    m_ok    = false;
}

// ── Render targets ──────────────────────────────────────────────────────────

CLiquidSim::~CLiquidSim() {
    release();
}

void CLiquidSim::release() noexcept {
    for (STarget* target : {&m_velocity.read, &m_velocity.write, &m_dye.read, &m_dye.write, &m_displacement.read, &m_displacement.write,
                            &m_pressure.read, &m_pressure.write, &m_divergence, &m_curl}) {
        if (target->fbo)
            glDeleteFramebuffers(1, &target->fbo);
        if (target->tex)
            glDeleteTextures(1, &target->tex);
        *target = {};
    }
    m_w = m_h = 0;
}

bool CLiquidSim::allocate(int w, int h) {
    release();

    glActiveTexture(GL_TEXTURE0);
    bool ok = true;
    for (STarget* target : {&m_velocity.read, &m_velocity.write, &m_dye.read, &m_dye.write, &m_displacement.read, &m_displacement.write,
                            &m_pressure.read, &m_pressure.write, &m_divergence, &m_curl}) {
        glGenTextures(1, &target->tex);
        glBindTexture(GL_TEXTURE_2D, target->tex);
        // linear for advection; the other passes read exact texel centres, where linear == nearest
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

        glGenFramebuffers(1, &target->fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, target->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            ok = false;
            break;
        }
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!ok) {
        release();
        return false;
    }
    m_w = w;
    m_h = h;
    return true;
}

// ── Passes ──────────────────────────────────────────────────────────────────

namespace {

// maps the shader VAO's [0,1] positions to clip space, as in blurBackground()
constexpr std::array<float, 9> FULLSCREEN_PROJECTION = {
    2.0f, 0.0f, 0.0f,
    0.0f, 2.0f, 0.0f,
   -1.0f,-1.0f, 1.0f,
};

struct SInput {
    const char* name;
    GLuint      tex;
};

// Binds the program and its input textures (unit 0 up); the caller sets the
// remaining uniforms, then draw() fills the target.
WP<CShader> use(const CLiquidPrograms::SProgram& program, float texelX, float texelY, std::initializer_list<SInput> inputs) {
    auto shader = g_pHyprOpenGL->useShader(program.shader);
    shader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, FULLSCREEN_PROJECTION);
    glUniform2f(program["texel"], texelX, texelY);
    int unit = 0;
    for (const auto& input : inputs) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, input.tex);
        glUniform1i(program[input.name], unit);
        ++unit;
    }
    return shader;
}

void draw(const WP<CShader>& shader, GLuint targetFbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

} // namespace

void CLiquidSim::applySplat(const SLiquidSplat& s) {
    const auto& p  = g_pGlobalState->liquidPrograms.splat;
    const float tx = 1.0f / m_w, ty = 1.0f / m_h;

    // velocity: additive push, so slowing down never carves a hole
    auto shader = use(p, tx, ty, {{"tex", m_velocity.read.tex}});
    glUniform4f(p["seg"], s.ax, s.ay, s.bx, s.by);
    glUniform4f(p["push"], s.vx, s.vy, s.amount, 0.0f);
    glUniform2f(p["shape"], s.aspect, s.radius);
    draw(shader, m_velocity.write.fbo);
    m_velocity.swap();

    // liquid: additive per px travelled, capped at full thickness
    shader = use(p, tx, ty, {{"tex", m_dye.read.tex}});
    glUniform4f(p["push"], 0.0f, 0.0f, s.amount, 1.0f);
    draw(shader, m_dye.write.fbo);
    m_dye.swap();
}

void CLiquidSim::step(float dt, const SLiquidParams& params) {
    const auto& P  = g_pGlobalState->liquidPrograms;
    const float tx = 1.0f / m_w, ty = 1.0f / m_h;

    auto shader = use(P.curl, tx, ty, {{"tex", m_velocity.read.tex}});
    draw(shader, m_curl.fbo);

    shader = use(P.vorticity, tx, ty, {{"tex", m_velocity.read.tex}, {"uCurl", m_curl.tex}});
    glUniform1f(P.vorticity["curl"], params.swirl);
    glUniform1f(P.vorticity["dt"], dt);
    draw(shader, m_velocity.write.fbo);
    m_velocity.swap();

    shader = use(P.divergence, tx, ty, {{"tex", m_velocity.read.tex}});
    draw(shader, m_divergence.fbo);

    shader = use(P.scale, tx, ty, {{"tex", m_pressure.read.tex}});
    glUniform1f(P.scale["value"], LIQUID_PRESSURE);
    draw(shader, m_pressure.write.fbo);
    m_pressure.swap();

    for (int i = 0; i < params.steps; ++i) {
        shader = use(P.pressure, tx, ty, {{"tex", m_pressure.read.tex}, {"uDiv", m_divergence.tex}});
        draw(shader, m_pressure.write.fbo);
        m_pressure.swap();
    }

    shader = use(P.gradient, tx, ty, {{"tex", m_pressure.read.tex}, {"uVel", m_velocity.read.tex}});
    draw(shader, m_velocity.write.fbo);
    m_velocity.swap();

    // the view behind: dragged along where the glass is wet, then flows back
    shader = use(P.displace, tx, ty, {{"tex", m_displacement.read.tex}, {"uVel", m_velocity.read.tex}, {"uDye", m_dye.read.tex}});
    glUniform1f(P.displace["dt"], dt);
    glUniform1f(P.displace["decay"], std::exp(-params.back * dt));
    glUniform1f(P.displace["drag"], params.drag);
    draw(shader, m_displacement.write.fbo);
    m_displacement.swap();

    // the liquid lags behind the flow
    shader = use(P.advect, tx, ty, {{"tex", m_dye.read.tex}, {"uVel", m_velocity.read.tex}});
    glUniform1f(P.advect["dt"], dt);
    glUniform1f(P.advect["dissipation"], params.fade);
    glUniform1f(P.advect["velScale"], 0.35f);
    draw(shader, m_dye.write.fbo);
    m_dye.swap();

    shader = use(P.advect, tx, ty, {{"tex", m_velocity.read.tex}, {"uVel", m_velocity.read.tex}});
    glUniform1f(P.advect["dt"], dt);
    glUniform1f(P.advect["dissipation"], LIQUID_FLOW_FADE);
    glUniform1f(P.advect["velScale"], 1.0f);
    draw(shader, m_velocity.write.fbo);
    m_velocity.swap();
}

bool CLiquidSim::update(int gridW, int gridH, float dt, const SLiquidSplat* splat, const SLiquidParams& params,
                        GLuint restoreFbo, int restoreW, int restoreH) {
    if (!g_pGlobalState->liquidPrograms.ensure())
        return false;

    // Each pass overwrites its whole target: no blending, and no scissor or
    // stencil left over from the element that ran before us.
    g_pHyprRenderer->blend(false);
    g_pHyprOpenGL->scissor(nullptr);
    if (glIsEnabled(GL_SCISSOR_TEST))
        glDisable(GL_SCISSOR_TEST);
    if (glIsEnabled(GL_STENCIL_TEST))
        glDisable(GL_STENCIL_TEST);

    bool ok = true;
    if (gridW != m_w || gridH != m_h)
        ok = allocate(gridW, gridH);

    if (ok) {
        g_pHyprOpenGL->setViewport(0, 0, m_w, m_h);
        if (splat)
            applySplat(*splat);
        step(dt, params);
    }

    // Hyprland's state at every element boundary, the caller's framebuffer back
    g_pHyprRenderer->blend(true);
    glBindFramebuffer(GL_FRAMEBUFFER, restoreFbo);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    g_pHyprOpenGL->setViewport(0, 0, restoreW, restoreH);
    return ok;
}
