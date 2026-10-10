#pragma once

#include <unordered_map>
#include <string>

inline const std::unordered_map<std::string, const char*> SHADERS = {
    {"liquidglass.frag", R"GLSL(
#version 300 es
precision highp float;

/*
 * Apple-style Liquid Glass Fragment Shader — Thick-glass refraction model
 *
 * The window is modeled as a thick convex glass slab:
 *   - Center: flat surface → clean frosted blur, no distortion
 *   - Edges: curved surface → refraction pulls in content from beyond
 *     the window boundary, creating natural color bleeding
 *
 * Rendering layers:
 * 1. Edge refraction via smooth outward direction (optionally along the edges,
 *    optionally rim-only) + exponential proximity
 * 2. Chromatic aberration (per-channel refraction scale)
 * 3. Edge raw-texture blend for vivid color pickup
 * 4. Subtle center dome lens magnification
 * 5. Frosted tint (brightness boost + desaturation)
 * 6. Configurable color tint overlay
 * 7. Bevel (thin lit line at the edge)
 * 8. Fresnel edge glow (white or tinted by the background)
 * 9. Specular highlight (top)
 * 10. Inner shadow (bottom rim)
 */

uniform sampler2D tex;
uniform vec2 fullSize;
uniform vec2 invFullSize;      // = 1.0 / fullSize, hoisted out of the per-pixel divisions below
uniform vec4 radii;            // per-corner radius: top-left, top-right, bottom-right, bottom-left
uniform vec2 uvPadding;

uniform float refractionStrength;
uniform float chromaticAberration;
uniform float fresnelStrength;
uniform float specularStrength;
uniform float glassOpacity;
uniform float edgeThickness;
uniform float invBezelWidthPx; // = 1.0 / (edgeThickness * minDim), hoisted per-draw
uniform vec3 tintColor;
uniform float tintAlpha;
uniform float lensDistortion;
uniform float lensMaxPx;       // = lensDistortion * minDim * 0.006, hoisted per-draw
uniform float brightness;
uniform float contrast;
uniform float saturation;
uniform float vibrancy;
uniform float vibrancyDarkness;
uniform float adaptiveDim;
uniform float adaptiveBoost;
uniform float roundingPower;
uniform float invRoundingPower; // = 1.0 / roundingPower, hoisted per-draw
uniform float refractionFlow;
uniform float refractionSpread;
uniform float fresnelTint;
uniform float bevelStrength;
uniform float bevelSize;
uniform float monitorScale;
uniform vec3 fresnelColor;
uniform float fresnelColorAlpha;
uniform vec3 bevelColor;
uniform float bevelColorAlpha;
uniform float bevelTint;
uniform float bevelAngle;
uniform float bevelShadow;
uniform float specularAngle;

// Glass light (see GlassLight.hpp). Packed:
uniform vec2  lightPos;     // light source, monitor-local px
uniform vec4  lightA;       // light amount, sharpness, rim width px, far-side reflection
uniform vec4  lightB;       // bend, click glow progress (-1 = none), glow light amount, glow flex
uniform vec3  lightColor;
uniform vec2  glowPoint;    // click point, window-local px
uniform vec2  glowC;        // glow reach px, ring width px
uniform float materialize;  // 0..1: new windows ramp their bending up
uniform vec2  boxPos;       // window top-left, monitor-local px
uniform vec3  parallax;     // xy = view shift px (cursor tilt), z = extra toward the rim
uniform vec4  oilA;         // amount (0 = off), time (s, pre-scaled by speed), swirl size px, colourfulness
uniform vec4  oilB;         // warp px, unused x3

// Liquid touch (LiquidSim.hpp): this window's fluid simulation, in box UV.
uniform int       liqOn;      // 1 while this window's liquid is live
uniform sampler2D liqDye;     // x = liquid thickness 0..1
uniform sampler2D liqVel;     // xy = flow, sim cells per second
uniform sampler2D liqDisp;    // xy = how far the view behind is dragged, box UV
uniform vec2      liqTexel;   // one sim cell, box UV
uniform vec4      liqA;       // amount (scales the whole look), refraction, colour split, ripple
uniform float     liqGlints;

uniform sampler2D maskTex;
uniform int useMask;
uniform vec2 maskUVOffset;
uniform vec2 maskUVScale;
uniform float maskAlphaThreshold;
uniform int maskMode;          // 0 = alpha threshold, 1 = protocol region
uniform int regionRectCount;   // 0..16
uniform vec4 regionRects[16];  // box-local pixels: xy = offset from box top-left, zw = size

// Subsurface item glass only: the rounded-box SDF (getCornerSDF below) is
// evaluated over this sub-rect of the drawn box instead of the full box —
// the item's blur-region extents, which can be smaller than its own surface
// box (e.g. a capsule pill inside a wider hit-test area). Box-local pixels,
// same space as regionRects. Windows and alpha-mask layers pass offset (0,0)
// and size == fullSize, so the SDF is unchanged from the old whole-box math.
uniform vec2 glassBoxOffsetPx;
uniform vec2 glassBoxSizePx;

// Maps this fragment's own box UV into the sample texture's normalized space
// before uvPadding is applied. Identity (offset 0, scale 1) unless the sample
// texture covers a smaller area than this box — PROTOCOL_REGION layers only,
// where the background is only ever sampled/blurred inside the blur region's
// bounding box (see GlassRenderer::sampleBackground callers in GlassLayerSurface.cpp).
uniform vec2 sampleUVOffset;
uniform vec2 sampleUVScale;

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

// ============================================================================
// TEXTURE SAMPLING (window UV -> padded texture UV)
// ============================================================================

// Box UV -> sample-texture-local UV (undoes sampleUVOffset/uvScale's
// shrink before the padding remap below sees it).
vec2 toSampleBoxUV(vec2 wuv) {
    return (wuv - sampleUVOffset) / sampleUVScale;
}

vec2 toTexUV(vec2 wuv) {
    return wuv * (1.0 - 2.0 * uvPadding) + uvPadding;
}

vec4 sampleBlurred(vec2 wuv) {
    vec2 tuv = toTexUV(toSampleBoxUV(wuv));
    return texture(tex, clamp(tuv, 0.001, 0.999));
}

// ============================================================================
// SDF
// ============================================================================

float lpNorm(vec2 v, float p, float invP) {
    // Exact identity: pow(x^2+y^2, 0.5) == length(v) when p == 2.0 (the
    // Hyprland default). Native sqrt is a single correctly-rounded hardware
    // op vs. two pow()s (exp2/log2-based) + a third pow() for the outer root.
    if (p == 2.0) return length(v);
    return pow(pow(abs(v.x), p) + pow(abs(v.y), p), invP);
}

// Quadrant lookup for the per-corner radius. p is measured from the box
// center (as getRoundedBoxSDF/getBevelSDF compute it below): negative y is
// the top half (v_texcoord grows downward — see the specular highlight's
// 1.0 - uv.y), negative x is the left half. cornerRadii is (top-left,
// top-right, bottom-right, bottom-left).
float pickCornerRadius(vec2 p, vec4 cornerRadii) {
    float top    = p.x < 0.0 ? cornerRadii.x : cornerRadii.y;
    float bottom = p.x < 0.0 ? cornerRadii.w : cornerRadii.z;
    return p.y < 0.0 ? top : bottom;
}

// posPx/boxSizePx: pixel-space position relative to (and size of) the box the
// SDF is measured against — the glass box (see glassBoxOffsetPx/SizePx above),
// not necessarily the fragment's full drawn box.
float getRoundedBoxSDF(vec2 posPx, vec2 boxSizePx, vec4 cornerRadii) {
    vec2 p = posPx - boxSizePx * 0.5;
    vec2 halfSize = boxSizePx * 0.5;
    float r = pickCornerRadius(p, cornerRadii);
    float clampedR = min(r, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + clampedR;
    return min(max(q.x, q.y), 0.0) + lpNorm(max(q, 0.0), roundingPower, invRoundingPower) - clampedR;
}

float getCornerSDF(vec2 uv) {
    vec2 boxLocalPx = uv * fullSize - glassBoxOffsetPx;
    return getRoundedBoxSDF(boxLocalPx, glassBoxSizePx, radii);
}

// Edge distance for the bevel, crease-free. Inside the window, getCornerSDF is
// max(q.x, q.y) - r: the distance to whichever edge is nearer. That has a crease
// along each corner's 45-degree diagonal, so the edge refraction (and anything
// else driven by edgeProximity) shows a straight seam in all four corners.
//
// Here the contour lines are rounded rectangles that match the window outline
// exactly at the edge (depth 0) and get rounder going inward: at depth d the box
// is inset by d and its corner radius is r + d. The radius only grows, so the
// top and side bevels always blend round the corner. Per pixel the depth is a
// closed-form quadratic in the corner zone:
//   |a + 2d| = r + d,  a = |p| - halfSize + r   =>   7d^2 + (4(ax+ay) - 2r) d + |a|^2 - r^2 = 0
// and the plain straight-edge distance elsewhere. Past depth (h - r) / 2, with h
// the smaller half-size, the radius would outgrow the inset box: from there the
// contours are stadiums, i.e. the plain distance to the box rounded by h (so a
// capsule gets its exact distance everywhere).
float getBevelSDF(vec2 uv) {
    vec2  H = glassBoxSizePx * 0.5;
    float h = min(H.x, H.y);
    vec2  signedP = uv * fullSize - glassBoxOffsetPx - H;
    float r = min(pickCornerRadius(signedP, radii), h);
    vec2  p = abs(signedP);
    vec2  a = p - H + r;
    float d = min(r - a.x, r - a.y);                      // straight-edge depth
    float B = 4.0 * (a.x + a.y) - 2.0 * r;
    float C = dot(a, a) - r * r;
    float disc = B * B - 28.0 * C;
    if (disc >= 0.0) {
        float dc = (-B + sqrt(disc)) / 14.0;
        if (a.x + 2.0 * dc >= 0.0 && a.y + 2.0 * dc >= 0.0)
            d = dc;                                        // in a corner zone: the rounded contour
    }
    vec2 s = p - H + h;
    return -max(d, h - length(max(s, 0.0)) - min(max(s.x, s.y), 0.0));
}

// Normal the light reads. The window's own corners are tight, so their normal
// swings 90 degrees in a few px and the highlight pinches into a bright point
// there. Light uses a much rounder box (radius R) instead, so it turns the
// corner gradually and the lit band stays an even stroke.
vec2 lightNormal(vec2 uv, float R) {
    vec2  H = fullSize * 0.5;
    vec2  p = (uv - 0.5) * fullSize;
    R = min(R, min(H.x, H.y));
    vec2  q = abs(p) - H + R;
    vec2  n = (q.x > 0.0 && q.y > 0.0) ? normalize(q)
            : (q.x > q.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
    return n * sign(p);
}

// ============================================================================
// OIL FILM helpers: smooth value noise, fbm, domain warping
// ============================================================================

float oilHash(vec2 p) { return fract(sin(dot(p, vec2(41.3, 289.1))) * 45758.5453); }

float oilNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(oilHash(i), oilHash(i + vec2(1.0, 0.0)), u.x),
               mix(oilHash(i + vec2(0.0, 1.0)), oilHash(i + vec2(1.0, 1.0)), u.x), u.y);
}

float oilFbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 3; i++) { v += a * oilNoise(p); p = p * 2.03 + vec2(17.0, 9.0); a *= 0.5; }
    return v;
}

// Liquid surface height, from the simulation only: thick liquid forms a smooth
// lens and fast flow raises ripples on it.
float liquidHeight(vec2 uv) {
    float dye   = texture(liqDye, uv).x;
    float speed = length(texture(liqVel, uv).xy);
    return dye * 0.6 + speed * liqA.w * 0.004 * smoothstep(0.0, 0.3, dye);
}

// ============================================================================
// REFRACTION DIRECTION
// Pixel-space direction toward window center — perfectly smooth everywhere,
// no SDF gradient needed (optional edge-following blend below). On straight edges the perpendicular pixel distance
// dominates, giving approximately edge-normal direction. At corners it
// naturally follows the diagonal.
// ============================================================================

vec2 refractionDir(vec2 uv) {
    vec2 toCenterPx = (vec2(0.5) - uv) * fullSize;
    float len = length(toCenterPx);
    return len > 0.1 ? toCenterPx / len : vec2(0.0);
}

// Smooth edge-following field: points into the box, hugging each edge's normal
// away from the corners and blending crease-free through the diagonals.
vec2 edgeDir(vec2 posPx) {
    vec2 halfSize = fullSize * 0.5;
    vec2 n = abs(posPx) / halfSize;
    vec2 g = sign(posPx) * pow(n, vec2(7.0)) / halfSize;
    float len = length(g);
    return len > 0.0 ? -g / len : vec2(0.0);   // points INTO the box
}

// light direction for a clockwise angle in degrees, 0 = from the top (screen y grows downward)
vec2 lightDir(float angleDeg) {
    float a = radians(angleDeg);
    return vec2(sin(a), -cos(a));
}

// exact outward normal from the SDF gradient; only meaningful right at the edge
vec2 sdfOutwardNormal(vec2 uv) {
    vec2 h = vec2(1.0) / fullSize;
    vec2 grad = vec2(
        getCornerSDF(uv + vec2(h.x, 0.0)) - getCornerSDF(uv - vec2(h.x, 0.0)),
        getCornerSDF(uv + vec2(0.0, h.y)) - getCornerSDF(uv - vec2(0.0, h.y))
    );
    float len = length(grad);
    return len > 0.0 ? grad / len : vec2(0.0, -1.0);
}

// ============================================================================
// MAIN — Thick-glass refraction model
// ============================================================================

void main() {
    vec2 uv = v_texcoord;

    // Layers only: sample the temp FBO to get the rendered surface pixel.
    // Discard fully transparent fragments so glass only covers visible content.
    // For windows, hasMask is false and this block is skipped entirely.
    vec4 surfacePixel = vec4(0.0);
    bool hasMask = (useMask == 1);
    if (hasMask) {
        vec2 maskUV = uv * maskUVScale + maskUVOffset;
        surfacePixel = texture(maskTex, clamp(maskUV, 0.001, 0.999));

        if (maskMode == 1) {
            vec2 pixelPos = uv * fullSize;
            bool insideRegion = false;
            for (int i = 0; i < regionRectCount; i++) {
                vec4 r = regionRects[i];
                if (pixelPos.x >= r.x && pixelPos.y >= r.y &&
                    pixelPos.x <= r.x + r.z && pixelPos.y <= r.y + r.w) {
                    insideRegion = true;
                    break;
                }
            }
            if (!insideRegion) { fragColor = surfacePixel; return; } // premultiplied, output as-is
        } else if (surfacePixel.a < maskAlphaThreshold) {
            discard;
        }
    }

    float cornerSdf    = getCornerSDF(uv);
    float cornerAlpha  = 1.0 - smoothstep(-1.5, 0.5, cornerSdf);

    if (maskMode == 1) {
        // Subsurface items: the glass box (glassBoxOffsetPx/SizePx) can be
        // smaller than the item's own drawn box/region — e.g. a capsule glass
        // shape inside a rectangular blur region. Outside it, fall back to the
        // plain surface pixel instead of dropping it (a hard discard here would
        // silently delete client content, like an icon, that simply isn't under
        // the glass). cornerAlpha fades smoothly across the boundary (the same
        // curve already used for glassA below), so this is an antialiased
        // "glass * coverage, surface over" blend, not a hard cutover — the
        // actual blend happens in the hasMask composite at the bottom of main().
        if (cornerAlpha < 0.001) {
            fragColor = surfacePixel;
            return;
        }
    } else {
        // Windows, and alpha-mask layers: no surface pixel to fall back to
        // outside the glass shape, so this is exactly the old hard-edged cutoff.
        if (cornerSdf > 0.0) discard;
        if (cornerAlpha < 0.001) discard;
    }

    float minDim = min(fullSize.x, fullSize.y);
    float bezelWidthPx = edgeThickness * minDim;

    // ========================================
    // EDGE PROXIMITY + DIRECTION
    // edgeProximity: 1.0 at boundary, exponential decay inward
    // inwardDir: pixel-space direction toward center (smooth everywhere)
    // Clamped to 1.0: cornerSdf can be positive here (maskMode==1's glass box
    // can be smaller than fullSize, so fragments just outside it still reach
    // this code with cornerAlpha > 0.001), and exp() of a positive value would
    // otherwise overshoot every effect that scales off edgeProximity.
    // ========================================
    // crease-free bevel distance, so the edge has no seam along the corner diagonals;
    // it assumes circular corners, so a superellipse outline keeps the exact SDF
    float bevelSdf = roundingPower == 2.0 ? getBevelSDF(uv) : cornerSdf;
    float edgeProximity = min(exp(bevelSdf * invBezelWidthPx), 1.0);
    vec2 inwardDir = refractionDir(uv);
    vec2 posPx = (uv - 0.5) * fullSize; // pixel-space position for the edge-flow direction below

    // ========================================
    // EDGE REFRACTION
    // Offset sampling UV inward (toward center) at edges — like looking
    // through the curved thick edge of a glass slab. This compresses
    // and distorts what's already behind the window, without reaching
    // beyond the window boundary.
    // ========================================
    float refractionPx = refractionStrength * 50.0;
    float lensFalloff = edgeProximity;
    if (refractionSpread < 0.999) {
        // rim-only lens: window the exponential tail so the centre stays flat
        float depth = -bevelSdf;
        float tailWindow = 1.0 - smoothstep(1.5 * bezelWidthPx, 3.0 * bezelWidthPx, depth);
        lensFalloff = mix(edgeProximity * tailWindow, edgeProximity, refractionSpread);
    }
    float refractionMag = lensFalloff * refractionPx;
    vec2 dir = inwardDir;
    if (refractionFlow > 0.001) {
        // pull along the edges instead of toward the centre
        vec2 mixedDir = mix(inwardDir, edgeDir(posPx), refractionFlow);
        float mixedLen = length(mixedDir);
        dir = mixedLen > 0.0001 ? mixedDir / mixedLen : inwardDir;
    }
    vec2 baseOffset = dir * refractionMag * invFullSize * materialize;

    // ========================================
    // GLASS LIGHT — the rim catches a light source where it faces it (moving
    // the window or the cursor moves the highlight), plus a softer reflection
    // on the far side. Where it's lit the glass concentrates light (bends more).
    // ========================================
    float lightAmt = 0.0;
    // light amount (lightA.x) and bend (lightB.x) are independent: with no
    // added light the rim still bends toward the light, and vice versa.
    if (lightA.x > 0.001 || abs(lightB.x) > 0.001) {
        // one direction per window (from its centre), like sunlight: a per-pixel
        // direction fans out into a pointed cone when the light sits near an edge
        vec2  toL = lightPos - (boxPos + 0.5 * fullSize);
        vec2  n   = lightNormal(uv, max(pickCornerRadius((uv - 0.5) * fullSize, radii), lightA.z * 6.0));
        vec2  L   = length(toL) > 1.0 ? normalize(toL) : vec2(0.0, -1.0);
        float ndl = dot(n, L);
        float rim = exp(bevelSdf / max(lightA.z, 1.0));       // 1 at the edge, fading inward
        float lit = pow(max(ndl, 0.0), lightA.y);
        float far = pow(max(-ndl, 0.0), lightA.y) * lightA.w;
        lightAmt  = (lit + far) * rim * lightA.x;
        baseOffset += inwardDir * (lit * rim * lightB.x * 20.0) / fullSize * materialize;
    }

    // ========================================
    // CLICK GLOW — energizes from the click point: a ring of light spreads
    // through the glass and fades, flexing the glass outward as it passes.
    // ========================================
    float glowAmt = 0.0;
    if (lightB.y >= 0.0) {   // glow light (lightB.z) and flex (lightB.w) independent
        float gp   = lightB.y;
        vec2  px   = uv * fullSize;
        float d    = distance(px, glowPoint);
        float R    = glowC.x * (1.0 - pow(1.0 - gp, 3.0));    // ease out
        float fade = pow(1.0 - gp, 2.0);
        float ring = exp(-pow((d - R) / max(glowC.y, 1.0), 2.0));
        float fill = exp(-pow(d / max(R * 0.6 + 1.0, 1.0), 2.0)) * 0.5;
        glowAmt    = (ring + fill) * fade * lightB.z;
        vec2 away  = d > 0.5 ? (px - glowPoint) / d : vec2(0.0);
        baseOffset += away * (ring * fade * lightB.w * 14.0) / fullSize;
    }

    // ========================================
    // CHROMATIC ABERRATION — per-channel refraction scale
    // Blue refracts more than red → natural spectral fringing at edges.
    // ========================================
    float chromaSpread = chromaticAberration * 0.35;
    vec2 offsetR = baseOffset * (1.0 - chromaSpread);
    vec2 offsetG = baseOffset;
    vec2 offsetB = baseOffset * (1.0 + chromaSpread);

    // ========================================
    // CENTER DOME LENS (subtle magnification in the flat interior)
    // Fades near edges so it doesn't interfere with edge refraction.
    // ========================================
    vec2 domeUV = vec2(0.0);
    if (lensDistortion > 0.001) {
        vec2 c = (uv - 0.5) * 2.0;
        vec2 dGrad = vec2(
            -4.0 * c.x * (1.0 - c.y * c.y),
            -4.0 * c.y * (1.0 - c.x * c.x)
        );
        float lensFade = 1.0 - edgeProximity;
        domeUV = dGrad * lensMaxPx * lensFade * invFullSize * materialize;
    }

    // ========================================
    // BACKGROUND SAMPLING (frosted blur only)
    // Nearby color influence comes naturally from the Gaussian blur
    // kernel crossing the window boundary — no explicit raw sampling.
    // ========================================
    // ========================================
    // OIL FILM — a thin film on the glass: slow domain-warped swirls whose
    // thickness sets an iridescent colour (like oil on water), in patches,
    // gently rippling the view behind it. Attached to the window.
    // ========================================
    float oilSlick = 0.0;
    vec3  oilTint  = vec3(0.0);
    vec2  oilUV    = vec2(0.0);
    if (oilA.x > 0.001) {
        vec2  p  = uv * fullSize / max(oilA.z, 1.0);
        float t  = oilA.y;
        vec2  q  = vec2(oilFbm(p + vec2(0.0, t * 0.11)), oilFbm(p + vec2(5.2, 1.3) - t * 0.09));
        float th = oilFbm(p + 3.0 * q + vec2(t * 0.05, -t * 0.04));
        oilSlick = smoothstep(0.42, 0.72, th);
        vec3 film = 0.5 + 0.5 * cos(6.2831853 * (th * 2.2 + vec3(0.0, 0.33, 0.67)));
        oilTint  = mix(vec3(1.0), film, clamp(oilA.w, 0.0, 1.0));
        oilUV    = (q - 0.5) * oilB.x / fullSize * materialize;
    }

    // PARALLAX — the view behind the glass shifts against the pane as the
    // cursor moves (like tilting thick glass), a bit more toward the rim.
    // Same shift for every channel, so no colour fringing across the pane.
    vec2 parallaxUV = parallax.xy * (1.0 + parallax.z * edgeProximity) / fullSize * materialize;

    // ========================================
    // LIQUID TOUCH — the pointer stirs a thin clear liquid on the glass: where
    // it's thick it bulges into a lens, the flow drags the view behind along
    // (and it flows back), and its curved parts split colour and catch glints.
    // ========================================
    vec3  liqN      = vec3(0.0, 0.0, 1.0);
    vec2  liqUV     = vec2(0.0);
    vec2  liqSpread = vec2(0.0);
    float liqWet    = 0.0;
    if (liqOn == 1) {
        vec2 tx   = vec2(liqTexel.x, 0.0);
        vec2 ty   = vec2(0.0, liqTexel.y);
        vec2 grad = vec2(liquidHeight(uv + tx) - liquidHeight(uv - tx),
                         liquidHeight(uv + ty) - liquidHeight(uv - ty)) * 0.5;
        liqN      = normalize(vec3(-grad * 6.0, 1.0));
        liqWet    = smoothstep(0.0, 0.35, texture(liqDye, uv).x);
        vec2 disp = texture(liqDisp, uv).xy;
        vec2 lens = liqN.xy * minDim * invFullSize;   // same strength in px on any window shape
        liqUV     = (disp - lens * liqA.y * 0.05) * liqA.x;
        liqSpread = (lens * 0.035 + disp * 0.04) * liqA.z * liqA.x;
    }

    vec3 color;
    vec2 uvR = uv + offsetR + domeUV + parallaxUV + oilUV + liqUV - liqSpread * 0.5;
    vec2 uvG = uv + offsetG + domeUV + parallaxUV + oilUV + liqUV;
    vec2 uvB = uv + offsetB + domeUV + parallaxUV + oilUV + liqUV + liqSpread * 0.5;

    if ((chromaticAberration > 0.001 && edgeProximity > 0.01) || liqOn == 1) {
        color.r = sampleBlurred(uvR).r;
        color.g = sampleBlurred(uvG).g;
        color.b = sampleBlurred(uvB).b;
    } else {
        color = sampleBlurred(uvG).rgb;
    }

    // ========================================
    // FROSTED TINT (per-theme tone mapping)
    // ========================================
    float blurredLum = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Frosted desaturation
    color = mix(vec3(blurredLum), color, saturation);

    // Tight smoothstep range maps the blur-compressed luminance (~0.3-0.7)
    // to the full [0,1] adaptive range, creating visible per-region differentiation
    float lumCurve = smoothstep(0.25, 0.55, blurredLum);

    // Dim: multiplicative — effective at darkening bright areas
    color *= brightness * (1.0 - adaptiveDim * lumCurve);

    // Boost: additive lift — multiplicative can't brighten near-black content
    color += vec3(adaptiveBoost * (1.0 - lumCurve) * 0.5);

    // Contrast (pivot around midpoint)
    color = mix(vec3(0.5), color, contrast);

    // Vibrancy (selective saturation boost scaled by existing saturation)
    float currentLum = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float sat = max(color.r, max(color.g, color.b)) - min(color.r, min(color.g, color.b));
    float darkFactor = 1.0 - vibrancyDarkness * (1.0 - blurredLum);
    color = mix(vec3(currentLum), color, 1.0 + vibrancy * sat * darkFactor);

    // ========================================
    // COLOR TINT OVERLAY
    // ========================================
    color = mix(color, tintColor, tintAlpha);

    // ========================================
    // BEVEL — thin lit line hugging the edge, brightest on the side facing the light
    // ========================================
    if (bevelStrength > 0.001) {
        float sizePx = max(bevelSize * monitorScale, 1.0);   // logical px, uniform across monitor scales
        float core = 0.25 * sizePx;
        float tail = sizePx;
        float ring = (1.0 - smoothstep(-core, 0.0, cornerSdf)) * smoothstep(-tail, -core, cornerSdf);

        // lit side faces the light, the far side fades out and can be shadowed
        float facing = clamp(dot(sdfOutwardNormal(uv), lightDir(bevelAngle)) * 0.5 + 0.5, 0.0, 1.0);

        vec3 bevelLight = vec3(1.0);
        if (bevelColorAlpha > 0.001) bevelLight = mix(vec3(1.0), bevelColor, bevelColorAlpha);   // a dark colour gives a dark line
        if (bevelTint > 0.001) {
            float maxC = max(max(color.r, color.g), color.b);
            bevelLight = mix(bevelLight, maxC > 0.001 ? color / maxC : vec3(1.0), bevelTint);
        }

        color = mix(color, bevelLight, ring * facing * bevelStrength);
        if (bevelShadow > 0.001)
            color = mix(color, vec3(0.0), ring * (1.0 - facing) * bevelShadow);
    }

    // liquid light: faint shading toward the glass light, sharp glints only
    if (liqOn == 1) {
        vec2  toL = lightPos - (boxPos + 0.5 * fullSize);
        vec2  L2  = length(toL) > 1.0 ? normalize(toL) : vec2(0.0, -1.0);
        color    *= max(1.0 + dot(liqN.xy, L2) * 0.15 * liqWet * liqA.x, 0.0);
        vec3  H   = normalize(normalize(vec3(L2, 1.2)) + vec3(0.0, 0.0, 1.0));
        color    += vec3(pow(max(dot(liqN, H), 0.0), 120.0) * liqGlints * liqA.x);
    }

    // ========================================
    // FRESNEL RIM GLOW (edge zone)
    // ========================================
    if (fresnelStrength > 0.001) {
        float fresnel = edgeProximity * edgeProximity * fresnelStrength * 0.15;
        vec3 fresnelLight = vec3(1.0);
        if (fresnelColorAlpha > 0.001) fresnelLight = mix(vec3(1.0), fresnelColor, fresnelColorAlpha);   // chosen colour, then the tint below
        if (fresnelTint > 0.001) {
            // rim light in the background's own hue, at full brightness so the gain matches white
            float maxC = max(max(color.r, color.g), color.b);
            fresnelLight = mix(fresnelLight, maxC > 0.001 ? color / maxC : vec3(1.0), fresnelTint);
        }
        color += fresnelLight * fresnel;
    }

    // ========================================
    // SPECULAR — subtle top highlight (edge zone)
    // ========================================
    if (specularStrength > 0.001) {
        float specT = max(1.0 - uv.y, 0.0);
        if (abs(specularAngle) > 0.001) {
            // rotate the highlight gradient toward the light: at angle 0, lightDir gives
            // (0,-1) and dot(uv-0.5, L) = 0.5-uv.y, so 0.5+dot(...) reduces to 1-uv.y exactly
            vec2 L = lightDir(specularAngle);
            specT = clamp(0.5 + dot(uv - 0.5, L), 0.0, 1.0);
        }
        float topBias = pow(specT, 2.0);
        float spec = topBias * edgeProximity * edgeProximity * specularStrength * 0.08;
        color += vec3(1.0, 0.99, 0.97) * spec;
    }

    color += lightColor * lightAmt * 0.55 + vec3(1.0, 0.99, 0.97) * glowAmt * 0.35;

    // oil sheen: soft-light the film colour into the glass where the film is thick
    if (oilSlick > 0.0) {
        float k = oilA.x * oilSlick;
        color = mix(color, color * (0.7 + 0.6 * oilTint) + oilTint * 0.06, k);
    }

    // ========================================
    // INNER SHADOW (bottom rim)
    // ========================================
    {
        float bottomBias = pow(uv.y, 2.0);
        float shadow = bottomBias * edgeProximity * edgeProximity * 0.06;
        color *= 1.0 - shadow;
    }

    // float framebuffers (FP16 under wide-gamut cm) store unbounded values and
    // the glass re-samples its own output: unclamped color diverges over frames
    color = clamp(color, 0.0, 1.0);
    float glassA = clamp(glassOpacity * cornerAlpha, 0.0, 1.0);

    if (hasMask) {
        // Layers only: composite the rendered surface over the glass effect
        // in a single pass. surfacePixel is premultiplied alpha from Hyprland's
        // surface rendering, so we unpremultiply before the 'over' blend.
        float surfA = surfacePixel.a;
        vec3 surfRGB = surfA > 0.001 ? surfacePixel.rgb / surfA : vec3(0.0);

        float compA = surfA + glassA * (1.0 - surfA);
        vec3 compRGB = compA > 0.001
            ? (surfRGB * surfA + color * glassA * (1.0 - surfA)) / compA
            : vec3(0.0);

        // Hyprland's compositor expects premultiplied alpha (blend GL_ONE, GL_ONE_MINUS_SRC_ALPHA).
        fragColor = vec4(compRGB * compA, compA);
    } else {
        // Windows: output the glass effect alone, surface is rendered separately by Hyprland.
        // Premultiplied: without this, a fading window's glass keeps full RGB contribution
        // because the GL_ONE source factor adds raw color regardless of alpha.
        fragColor = vec4(color * glassA, glassA);
    }
}
)GLSL"},

    // ---- Liquid touch: fluid simulation passes (LiquidSim.cpp) ----
    {"liquid_splat.frag", R"GLSL(
#version 300 es
precision highp float;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 texel;   // one sim cell
uniform vec4 seg;     // stroke from xy to zw, box UV
uniform vec4 push;    // xy = velocity (cells/s), z = amount, w = 0 velocity / 1 liquid
uniform vec2 shape;   // x = box aspect, y = brush radius (share of box height)
void main() {
    vec2 p = v_texcoord, a = seg.xy, b = seg.zw;
    p.x *= shape.x; a.x *= shape.x; b.x *= shape.x;
    vec2  pa = p - a, ba = b - a;
    float h  = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-8), 0.0, 1.0);
    float d  = length(pa - ba * h);
    float f  = exp(-d * d / (shape.y * shape.y));
    vec4 base = texture(tex, v_texcoord);
    if (push.w < 0.5)
        fragColor = vec4(base.xy + push.xy * f * push.z, 0.0, 1.0);
    else
        fragColor = vec4(min(base.x + f * push.z, 1.0), 0.0, 0.0, 1.0);
}
)GLSL"},

    {"liquid_advect.frag", R"GLSL(
#version 300 es
precision highp float;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 texel;   // one sim cell
uniform sampler2D uVel;
uniform float dt;
uniform float dissipation;
uniform float velScale;
void main() {
    vec2 coord = v_texcoord - dt * velScale * texture(uVel, v_texcoord).xy * texel;
    fragColor = texture(tex, coord) / (1.0 + dissipation * dt);
}
)GLSL"},

    {"liquid_displace.frag", R"GLSL(
#version 300 es
precision highp float;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 texel;   // one sim cell
// tex = displacement: the view behind is sampled at uv + D
uniform sampler2D uVel;
uniform sampler2D uDye;
uniform float dt;
uniform float decay;
uniform float drag;
void main() {
    float wet  = smoothstep(0.0, 0.2, texture(uDye, v_texcoord).x);
    vec2  step = dt * drag * wet * texture(uVel, v_texcoord).xy * texel;
    vec2  d    = texture(tex, v_texcoord - step).xy - step;
    d *= mix(decay * decay * decay, decay, wet);   // dry glass lets go faster
    fragColor = vec4(d, 0.0, 1.0);
}
)GLSL"},

    {"liquid_divergence.frag", R"GLSL(
#version 300 es
precision highp float;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 texel;   // one sim cell
void main() {
    vec2 vL = v_texcoord - vec2(texel.x, 0.0), vR = v_texcoord + vec2(texel.x, 0.0);
    vec2 vT = v_texcoord + vec2(0.0, texel.y), vB = v_texcoord - vec2(0.0, texel.y);
    float L = texture(tex, vL).x;
    float R = texture(tex, vR).x;
    float T = texture(tex, vT).y;
    float B = texture(tex, vB).y;
    vec2  C = texture(tex, v_texcoord).xy;
    if (vL.x < 0.0) L = -C.x;
    if (vR.x > 1.0) R = -C.x;
    if (vT.y > 1.0) T = -C.y;
    if (vB.y < 0.0) B = -C.y;
    fragColor = vec4(0.5 * (R - L + T - B), 0.0, 0.0, 1.0);
}
)GLSL"},

    {"liquid_curl.frag", R"GLSL(
#version 300 es
precision highp float;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 texel;   // one sim cell
void main() {
    float L = texture(tex, v_texcoord - vec2(texel.x, 0.0)).y;
    float R = texture(tex, v_texcoord + vec2(texel.x, 0.0)).y;
    float T = texture(tex, v_texcoord + vec2(0.0, texel.y)).x;
    float B = texture(tex, v_texcoord - vec2(0.0, texel.y)).x;
    fragColor = vec4(0.5 * (R - L - T + B), 0.0, 0.0, 1.0);
}
)GLSL"},

    {"liquid_vorticity.frag", R"GLSL(
#version 300 es
precision highp float;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 texel;   // one sim cell
uniform sampler2D uCurl;
uniform float curl;
uniform float dt;
void main() {
    float L = texture(uCurl, v_texcoord - vec2(texel.x, 0.0)).x;
    float R = texture(uCurl, v_texcoord + vec2(texel.x, 0.0)).x;
    float T = texture(uCurl, v_texcoord + vec2(0.0, texel.y)).x;
    float B = texture(uCurl, v_texcoord - vec2(0.0, texel.y)).x;
    float C = texture(uCurl, v_texcoord).x;
    vec2 force = 0.5 * vec2(abs(T) - abs(B), abs(R) - abs(L));
    force /= length(force) + 0.0001;
    force *= curl * C;
    force.y *= -1.0;
    vec2 vel = texture(tex, v_texcoord).xy + force * dt;
    fragColor = vec4(clamp(vel, -1000.0, 1000.0), 0.0, 1.0);
}
)GLSL"},

    {"liquid_scale.frag", R"GLSL(
#version 300 es
precision highp float;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 texel;   // one sim cell
uniform float value;
void main() {
    fragColor = value * texture(tex, v_texcoord);
}
)GLSL"},

    {"liquid_pressure.frag", R"GLSL(
#version 300 es
precision highp float;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 texel;   // one sim cell
uniform sampler2D uDiv;
void main() {
    float L = texture(tex, v_texcoord - vec2(texel.x, 0.0)).x;
    float R = texture(tex, v_texcoord + vec2(texel.x, 0.0)).x;
    float T = texture(tex, v_texcoord + vec2(0.0, texel.y)).x;
    float B = texture(tex, v_texcoord - vec2(0.0, texel.y)).x;
    float div = texture(uDiv, v_texcoord).x;
    fragColor = vec4((L + R + B + T - div) * 0.25, 0.0, 0.0, 1.0);
}
)GLSL"},

    {"liquid_gradient.frag", R"GLSL(
#version 300 es
precision highp float;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
uniform sampler2D tex;
uniform vec2 texel;   // one sim cell
uniform sampler2D uVel;
void main() {
    float L = texture(tex, v_texcoord - vec2(texel.x, 0.0)).x;
    float R = texture(tex, v_texcoord + vec2(texel.x, 0.0)).x;
    float T = texture(tex, v_texcoord + vec2(0.0, texel.y)).x;
    float B = texture(tex, v_texcoord - vec2(0.0, texel.y)).x;
    fragColor = vec4(texture(uVel, v_texcoord).xy - vec2(R - L, T - B), 0.0, 1.0);
}
)GLSL"},

    {"gaussianblur.frag", R"GLSL(
#version 300 es
precision highp float;

uniform sampler2D tex;
uniform vec2 direction; // (1.0/width, 0.0) for horizontal, (0.0, 1.0/height) for vertical
uniform float blurRadius; // kernel radius in pixels

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

void main() {
    // Compute sigma from radius (covers ~3 sigma)
    float sigma = max(blurRadius / 3.0, 0.001);
    float invSigma2 = -0.5 / (sigma * sigma);

    int samples = min(int(ceil(blurRadius)), 8);

    // Center tap, clamped: an out-of-range texel from a float framebuffer would otherwise dominate the kernel
    float w0 = 1.0;
    vec4 result = clamp(texture(tex, v_texcoord), 0.0, 1.0) * w0;
    float totalWeight = w0;

    // Linear sampling: pair adjacent taps (i, i+1) into a single bilinear fetch.
    // The interpolated offset between two texels yields their weighted average
    // in one texture() call, halving the total tap count.
    for (int i = 1; i <= samples; i += 2) {
        float x1 = float(i);
        float x2 = float(i + 1);
        float w1 = exp(x1 * x1 * invSigma2);
        float w2 = (i + 1 <= samples) ? exp(x2 * x2 * invSigma2) : 0.0;
        float wSum = w1 + w2;
        if (wSum < 0.0001) continue;

        // Offset biased toward the heavier weight
        float offset = (x1 * w1 + x2 * w2) / wSum;

        result += clamp(texture(tex, v_texcoord + direction * offset), 0.0, 1.0) * wSum;
        result += clamp(texture(tex, v_texcoord - direction * offset), 0.0, 1.0) * wSum;
        totalWeight += 2.0 * wSum;
    }

    fragColor = result / totalWeight;
}
)GLSL"},
};
