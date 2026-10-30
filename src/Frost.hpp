#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprutils/math/Box.hpp>

// Frost: a window's glass blurs further where other windows are behind it, and stays
// clear over the wallpaper. The glass draws the sample (the backdrop, already blurred)
// mixed toward a frosted copy of it, by an amount that follows how much of the glass
// the windows below cover, eased over time.
namespace Frost {

// Coverage below the dead zone is a window grazing the glass: no frost. From the full
// mark up, the glass is frosted entirely.
inline constexpr float COVERAGE_DEAD_ZONE = 0.04f;
inline constexpr float COVERAGE_FULL      = 0.45f;

// Time constant of the ease toward the target amount.
inline constexpr float EASE_SECONDS = 0.09f;

// Within this of its target the amount snaps there, so the ease ends and frames stop.
inline constexpr float SETTLE_EPSILON = 0.002f;

// Below this the frosted copy is not drawn at all (and not kept).
inline constexpr float MINIMUM_AMOUNT = 0.001f;

// The frosted copy: the sample at half its size, blurred with this many passes.
inline constexpr int BLUR_ITERATIONS = 2;

// frost_blur, in blur_strength units. Far past any useful frost: it only keeps the blur
// radius finite.
inline constexpr float BLUR_MAX = 10.0f;

// Smoothstep between the dead zone and the full mark.
[[nodiscard]] constexpr float amountForCoverage(float coverage) noexcept {
    const float t = std::clamp((coverage - COVERAGE_DEAD_ZONE) / (COVERAGE_FULL - COVERAGE_DEAD_ZONE), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static_assert(amountForCoverage(0.0f) == 0.0f);
static_assert(amountForCoverage(COVERAGE_DEAD_ZONE) == 0.0f);
static_assert(amountForCoverage(COVERAGE_FULL) == 1.0f);
static_assert(amountForCoverage(1.0f) == 1.0f);
static_assert(amountForCoverage(0.5f * (COVERAGE_DEAD_ZONE + COVERAGE_FULL)) > 0.49f &&
              amountForCoverage(0.5f * (COVERAGE_DEAD_ZONE + COVERAGE_FULL)) < 0.51f);

// The fraction of `glassBox` that the windows drawn below `window` cover, 0..1: the
// union of their boxes, so windows behind each other count once. `glassBox` is
// monitor-local physical pixels, as WindowGeometry::computeWindowBox() gives. Only an
// estimate: boxes are rectangles, translucency is ignored, and the drawing order
// follows Hyprland's tiled, floating, pinned layering.
[[nodiscard]] float coveredFraction(const PHLWINDOW& window, const PHLMONITOR& monitor, const CBox& glassBox);

// One window's frost: the eased amount and the frosted copy of its sample.
class CFrostState {
  public:
    // Moves the shown amount toward `target` (0..1), eased unless `animated` is off or
    // this is the first call, which snaps. True while it is still moving: the caller
    // keeps frames coming until it is not.
    [[nodiscard]] bool step(float target, bool animated, std::chrono::steady_clock::time_point now) noexcept {
        target = target > 0.0f ? std::min(target, 1.0f) : 0.0f;

        if (!m_seen || !animated) {
            m_seen   = true;
            m_amount = target;
            m_moving = false;
            return false;
        }

        if (m_amount != target) {
            // the first step of a move has no previous frame to measure from
            const float seconds = m_moving ? std::chrono::duration<float>(now - m_lastStep).count() : 0.0f;
            m_amount += (target - m_amount) * (1.0f - std::exp(-std::max(seconds, 0.0f) / EASE_SECONDS));
            if (std::abs(target - m_amount) < SETTLE_EPSILON)
                m_amount = target;
        }

        m_moving   = m_amount != target;
        m_lastStep = now;
        return m_moving;
    }

    [[nodiscard]] float amount() const noexcept { return m_amount; }
    [[nodiscard]] bool  moving() const noexcept { return m_moving; }

    // A shown amount, or a copy still held.
    [[nodiscard]] bool active() const noexcept { return m_amount > 0.0f || m_copy; }

    // The sample was refreshed: the copy no longer matches it.
    void sampleChanged() noexcept { m_copyStale = true; }

    // True when the copy is missing, stale, built with another radius, or not half the
    // size of `sample`.
    [[nodiscard]] bool needsBuild(const SP<Render::IFramebuffer>& sample, float radius) const {
        if (!m_copy || m_copyStale || radius != m_builtRadius || !sample)
            return true;
        return m_copy->m_size.x != std::max(1, static_cast<int>(sample->m_size.x) / 2) ||
               m_copy->m_size.y != std::max(1, static_cast<int>(sample->m_size.y) / 2);
    }

    void built(float radius) noexcept {
        m_copyStale   = false;
        m_builtRadius = radius;
    }

    // Frees the copy and its blur target; the shown amount stays.
    void releaseCopy() noexcept {
        m_copy.reset();
        m_temporary.reset();
        m_copyStale = true;
    }

    // Frost is off for this window (glass disabled): nothing shown, nothing held.
    void reset() noexcept {
        releaseCopy();
        m_amount = 0.0f;
        m_moving = false;
    }

    // The framebuffers buildFrostCopy() fills.
    [[nodiscard]] SP<Render::IFramebuffer>& copy() noexcept { return m_copy; }
    [[nodiscard]] SP<Render::IFramebuffer>& temporary() noexcept { return m_temporary; }

  private:
    SP<Render::IFramebuffer>              m_copy;
    SP<Render::IFramebuffer>              m_temporary;
    bool                                  m_copyStale   = true;
    float                                 m_builtRadius = 0.0f;
    float                                 m_amount      = 0.0f;
    bool                                  m_seen        = false;
    bool                                  m_moving      = false;
    std::chrono::steady_clock::time_point m_lastStep{};
};

} // namespace Frost
