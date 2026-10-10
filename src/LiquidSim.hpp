#pragma once

// Liquid Touch: a small fluid simulation per glass window, stirred by the
// pointer. Velocity, liquid thickness ("dye") and a displacement field that
// drags the view behind the glass live in half-float textures laid out in the
// window's own box UV; the glass shader reads them (Shaders.hpp, LIQUID TOUCH).
// A window's textures exist only while it is being stirred and are freed once
// its liquid settles, so an idle desktop costs nothing.
//
// Adapted from "Viscous Liquid - Cursor FX" by Sabo Sugi
// (https://codepen.io/sabosugi/pen/01a125aa-40e8-70ca-b198-550dc149d263) and
// Pavel Dobryakov's WebGL Fluid Simulation, both MIT: see THIRD_PARTY_NOTICES.md.

#include <GLES3/gl32.h>
#include <hyprland/src/render/Shader.hpp>
#include <string>
#include <unordered_map>
#include <utility>

// Fixed parts of the look, tuned in the browser prototype.
// The pointer as the liquid sees it follows the real one on a critically damped
// spring (rad/s). Bluetooth mice report only 15-30 times a second on Linux; a plain
// ease toward each report made strokes pulse at that rate. 14 keeps the stroke's
// speed within ~2x frame to frame at 15 Hz and trails the pointer by ~0.14 s.
inline constexpr float LIQUID_SPRING    = 14.0f;
inline constexpr float LIQUID_FLOW_FADE = 1.2f;  // velocity dissipation (1/s)
inline constexpr float LIQUID_PRESSURE  = 0.8f;  // share of last frame's pressure kept as the solver's start

struct SLiquidParams {
    float swirl = 8.0f;  // vorticity confinement
    float fade  = 0.9f;  // liquid dissipation (1/s)
    float drag  = 1.0f;  // how far the flow carries the view behind
    float back  = 1.1f;  // how fast the dragged view returns (1/s)
    int   steps = 20;    // pressure solver iterations
};

// One stroke of the pointer, from a to b.
struct SLiquidSplat {
    float ax = 0, ay = 0, bx = 0, by = 0; // box UV
    float vx = 0, vy = 0;                 // push, sim cells per second
    float amount = 0;                     // 0..1: the same liquid per px travelled at any speed
    float aspect = 1;                     // box width / height
    float radius = 0.05f;                 // brush radius, share of the box height
};

class CLiquidPrograms {
  public:
    struct SProgram {
        SP<CShader>                            shader = makeShared<CShader>();
        std::unordered_map<std::string, GLint> loc;

        [[nodiscard]] GLint operator[](const char* name) const {
            const auto it = loc.find(name);
            return it == loc.end() ? -1 : it->second;
        }
    };

    // Compiles every program once, on first use; false (for good) if one fails.
    [[nodiscard]] bool ensure();
    void               destroy() noexcept;

    SProgram splat, advect, displace, divergence, curl, vorticity, scale, pressure, gradient;

  private:
    bool m_tried = false;
    bool m_ok    = false;
};

class CLiquidSim {
  public:
    CLiquidSim() = default;
    ~CLiquidSim();
    CLiquidSim(const CLiquidSim&)            = delete;
    CLiquidSim& operator=(const CLiquidSim&) = delete;

    // One frame: (re)allocate for the grid, apply the stroke, step the fluid.
    // Runs inside the render pass and leaves restoreFbo bound with a
    // restoreW x restoreH viewport and blending on, like blurBackground().
    // False when half-float render targets are unavailable.
    [[nodiscard]] bool update(int gridW, int gridH, float dt, const SLiquidSplat* splat, const SLiquidParams& params,
                              GLuint restoreFbo, int restoreW, int restoreH);

    [[nodiscard]] GLuint dye() const { return m_dye.read.tex; }
    [[nodiscard]] GLuint velocity() const { return m_velocity.read.tex; }
    [[nodiscard]] GLuint displacement() const { return m_displacement.read.tex; }

  private:
    struct STarget {
        GLuint tex = 0;
        GLuint fbo = 0;
    };
    struct SDouble {
        STarget read, write;
        void    swap() { std::swap(read, write); }
    };

    int     m_w = 0, m_h = 0;
    SDouble m_velocity, m_dye, m_displacement, m_pressure;
    STarget m_divergence, m_curl;

    [[nodiscard]] bool allocate(int w, int h);
    void               release() noexcept;
    void               applySplat(const SLiquidSplat& s);
    void               step(float dt, const SLiquidParams& p);
};
