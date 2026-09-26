#pragma once

#include "GlassRenderer.hpp"
#include "PluginConfig.hpp"

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprutils/math/Region.hpp>

// Per-item glass state for "subsurface item glass": one instance per glassed
// wl_subsurface, mirroring CGlassLayerSurface's two-phase temp-FBO pipeline
// but simplified for the subsurface case:
//   - the mask is always the ext-background-effect-v1 protocol region (the
//     hook only ever creates this state for a surface that has one — see
//     main.cpp's CRenderPass::add hook), never alpha-threshold;
//   - the sample box always equals the full item box (no region-shrink
//     optimization), so the mask's sample UV mapping is always identity;
//   - geometry (the item's box, in every coordinate family) is handed in by
//     the caller every frame rather than recomputed from a long-lived
//     surface object: CSurfacePassElement is transient and owned by the
//     render pass once enqueued, so the box is captured once at
//     CRenderPass::add() hook time and threaded through the pre/post
//     elements' own data (see GlassSubsurfacePassElement/CompositeElement);
//   - the item's own rendered-surface redirect target is one of
//     g_pGlobalState->subsurfaceTempFramebuffers, keyed by the item's
//     monitor and shared/reused serially by every glassed item on that
//     monitor in the frame (not owned per-instance here) — see Globals.hpp
//     for why that's safe.
class CGlassSubsurfaceState {
  public:
    explicit CGlassSubsurfaceState(WP<CWLSurfaceResource> surface, PHLWINDOWREF window);
    ~CGlassSubsurfaceState();

    // Phase 1 (pre-surface): sample+blur background under transformBox, redirect
    // currentFB → temp FBO so the original CSurfacePassElement draw (called by
    // main.cpp's hook between this and compositeAndRestore) lands there instead.
    void sampleAndRedirect(PHLMONITOR monitor, const CBox& transformBox, float alpha);

    // Phase 2 (post-surface): restore currentFB, composite glass masked by the
    // protocol region with the temp FBO's content (the item's own foreground) on top.
    // transformedRegion is non-const: CRegion::getExtents() is a non-const method.
    void compositeAndRestore(PHLMONITOR monitor, const CBox& rawBox, const CBox& transformBox,
                              CRegion& transformedRegion, float alpha);

    [[nodiscard]] bool alive() const { return !m_surface.expired(); }

  private:
    WP<CWLSurfaceResource>   m_surface;
    PHLWINDOWREF             m_window;
    SP<Render::IFramebuffer> m_sampleFramebuffer; // box-sized, per-item (small)
    Vector2D                 m_samplePaddingRatio;
    bool                     m_hasCachedSample = false;

    // Last transformBox seen, to force a resample when the item itself moves
    // or resizes with no other invalidation trigger (mirrors
    // CGlassLayerSurface::damageIfMoved(), inlined here since we have no
    // standing surface object to hang a separate call off of).
    CBox m_lastTransformBox;

    uint64_t m_lastSceneGeneration = 0;

    // Set at the end of sampleAndRedirect() when currentFB was actually
    // redirected this frame; cleared by compositeAndRestore() after it reads
    // it. Same discard-safety net as CGlassLayerSurface::m_redirectedThisFrame.
    bool m_redirectedThisFrame = false;

    // Saved currentFB pointer, restored in compositeAndRestore()
    SP<Render::IFramebuffer> m_savedCurrentFB;

    [[nodiscard]] bool        resolveThemeIsDark() const;
    [[nodiscard]] std::string resolvePresetName() const;
};
