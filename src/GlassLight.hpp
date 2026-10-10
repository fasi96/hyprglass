#pragma once

// Glass light, following Apple's Liquid Glass logic: nothing animates on its
// own; light and motion come from what you do.
//   - A light source sits at a fixed point over the whole desktop; each
//     window's rim is lit where it faces that light, so moving a window slides
//     its highlight round the rim.
//   - The light leans toward the cursor (light_cursor), standing in for
//     tilting a phone: move the pointer and highlights glide.
//   - Clicking a glass window "energizes" it: a glow starts under the pointer
//     and spreads through the glass, flexing it as it goes.
//   - New windows materialize by ramping up their bending, not by fading.
// A single timer redraws only while something is moving (cursor, glow,
// materialize) and idles otherwise.

#include "Globals.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <hyprland/src/state/MonitorState.hpp>

namespace GlassLight {

[[nodiscard]] inline double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

[[nodiscard]] inline float f(Hyprlang::FLOAT* const* p) {
    return p ? static_cast<float>(**p) : 0.0f;
}

// An effect is on if it adds light or moves the glass; either can be 0 alone.
[[nodiscard]] inline bool lightOn() {
    const auto& c = g_pGlobalState->config;
    return f(c.lightStrength) > 0.001f || std::fabs(f(c.lightBend)) > 0.001f;
}
[[nodiscard]] inline bool glowOn() {
    const auto& c = g_pGlobalState->config;
    return f(c.glowStrength) > 0.001f || std::fabs(f(c.glowFlex)) > 0.001f;
}
[[nodiscard]] inline float glowDuration() {
    return std::clamp(f(g_pGlobalState->config.glowDuration), 0.1f, 5.0f);
}
[[nodiscard]] inline float materializeDuration() {
    return std::clamp(f(g_pGlobalState->config.materializeDuration), 0.0f, 3.0f);
}

// The light's position in global (logical) coordinates: a fixed point over
// the bounding box of all monitors, pulled toward the cursor by light_cursor.
[[nodiscard]] inline Vector2D lightPositionGlobal(const Vector2D& cursor) {
    const auto& cfg = g_pGlobalState->config;
    double minX = 0, minY = 0, maxX = 1, maxY = 1;
    bool first = true;
    for (const auto& mon : State::monitorState()->monitors()) {
        if (!mon)
            continue;
        const auto p = mon->m_position, s = mon->m_size;
        if (first) { minX = p.x; minY = p.y; maxX = p.x + s.x; maxY = p.y + s.y; first = false; }
        minX = std::min(minX, p.x); minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x + s.x); maxY = std::max(maxY, p.y + s.y);
    }
    Vector2D fixed = {minX + f(cfg.lightX) * (maxX - minX), minY + f(cfg.lightY) * (maxY - minY)};

    // slow wander: a lazy figure of eight, so highlights creep round the rims
    if (const float drift = f(cfg.lightDrift); drift > 0.001f) {
        const double period = std::max(2.0f, f(cfg.lightDriftPeriod));
        const double a      = 2.0 * M_PI * std::fmod(nowSeconds(), period * 1000.0) / period;
        fixed.x += std::sin(a) * drift * (maxX - minX);
        fixed.y += std::sin(a * 2.0 + 1.3) * drift * 0.5 * (maxY - minY);
    }
    const double pull = std::clamp(f(cfg.lightCursor), 0.0f, 1.0f);
    return fixed + (cursor - fixed) * pull;
}

[[nodiscard]] inline bool parallaxOn() { return std::fabs(f(g_pGlobalState->config.parallaxStrength)) > 0.01f; }

[[nodiscard]] inline bool oilOn() { return f(g_pGlobalState->config.oilAmount) > 0.001f; }

[[nodiscard]] inline bool liquidOn() {
    return f(g_pGlobalState->config.liquidAmount) > 0.001f && !g_pGlobalState->liquidUnsupported;
}

// Seconds until stirred liquid has settled: about five time constants of its
// slowest fade. Past this a window's simulation is dropped and redraws stop.
[[nodiscard]] inline double liquidSettle() {
    const auto& c       = g_pGlobalState->config;
    const float slowest = std::min({std::max(f(c.liquidFade), 0.05f), std::max(f(c.liquidReturn), 0.05f), LIQUID_FLOW_FADE});
    return std::min(12.0, 5.0 / slowest);
}

[[nodiscard]] inline bool driftOn() { return f(g_pGlobalState->config.lightDrift) > 0.001f && lightOn(); }

// The cursor position the light follows: lagged by light_lag so highlights
// glide after the pointer and settle, instead of snapping.
[[nodiscard]] inline Vector2D lightCursor(const Vector2D& raw) {
    auto& st = *g_pGlobalState;
    if (f(st.config.lightLag) <= 0.01f || !st.smoothCursorInit)   // smoothing shared by light + parallax
        return raw;
    return st.smoothCursor;
}

// Smooth 0..1 ramp (smoothstep).
[[nodiscard]] inline float ease(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

}
