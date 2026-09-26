#pragma once

#include <hyprland/src/config/shared/Types.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <typeinfo>
#include <unordered_map>

inline constexpr std::string_view CONFIG_PREFIX = "plugin:hyprglass:";

// Window tags for theme and preset selection
inline constexpr std::string_view TAG_THEME_PREFIX  = "hyprglass_theme_";
inline constexpr std::string_view TAG_PRESET_PREFIX = "hyprglass_preset_";

// Window tags for per-window enable/disable. Override the global `enabled` setting.
// `hyprglass_disabled` always wins if both are present.
inline constexpr std::string_view TAG_ENABLED  = "hyprglass_enabled";
inline constexpr std::string_view TAG_DISABLED = "hyprglass_disabled";

// Hyprland stores dynamic tags (`tagwindow` dispatcher, dynamic window rules)
// with a trailing '*'. CTagKeeper::isTagged() normalizes this for exact lookups,
// but code iterating getTags() or registering preset names must strip it itself
// so "firefox" and "firefox*" refer to the same preset.
inline std::string stripDynamicTagMarker(std::string_view tag) {
    if (tag.ends_with('*'))
        tag.remove_suffix(1);
    return std::string(tag);
}

// Sentinel: "not set by user, inherit from parent layer"
inline constexpr Hyprlang::FLOAT SENTINEL_FLOAT = -1.0;
inline constexpr Hyprlang::INT   SENTINEL_INT   = -1;

inline constexpr int MAX_PRESET_INHERITANCE_DEPTH = 8;

// Layer glass masking strategy: how hyprglass decides which pixels of a layer
// surface get the glass effect.
enum class ELayerMaskMode { AUTO, ALPHA, REGION };

// Parses "auto"/"alpha"/"region"; nullopt for anything else.
[[nodiscard]] std::optional<ELayerMaskMode> parseLayerMaskMode(std::string_view value);

// Instrumentation mode (plugin:hyprglass:debug:mode). Isolates the render-pass
// cost from the GL pipeline cost for A/B measurement: hints_only skips all GL
// work in the pass elements while keeping their boundingBox/needsLiveBlur/
// disableSimplification hints; gl_work_only runs the GL pipeline as normal but
// forces needsLiveBlur/disableSimplification off on both pass elements.
enum class EDebugMode { OFF, HINTS_ONLY, GL_WORK_ONLY };

// Parses "off"/"hints_only"/"gl_work_only"; nullopt for anything else.
[[nodiscard]] std::optional<EDebugMode> parseDebugMode(std::string_view value);

// Reads plugin:hyprglass:debug:mode, falling back to OFF for an unset global
// state or an unrecognized value (validateConfig() warns about the latter).
[[nodiscard]] EDebugMode currentDebugMode();

namespace ConfigKeys {

// Global-only
inline constexpr auto ENABLED             = "plugin:hyprglass:enabled";
inline constexpr auto DEFAULT_THEME       = "plugin:hyprglass:default_theme";
inline constexpr auto DEFAULT_PRESET      = "plugin:hyprglass:default_preset";
inline constexpr auto MANAGE_WINDOW_BLUR  = "plugin:hyprglass:manage_window_blur";
inline constexpr auto SKIP_OPAQUE_WINDOWS = "plugin:hyprglass:skip_opaque_windows";
inline constexpr auto BLUR_FOLD           = "plugin:hyprglass:blur_fold";

// Performance diagnostics
inline constexpr auto DEBUG_MODE   = "plugin:hyprglass:debug:mode";
inline constexpr auto DEBUG_TIMERS = "plugin:hyprglass:debug:timers";

// ---- Liquid Glass motion (global-only) --------------------------------------
// Light: a light source over the whole desktop lights each window's rim where
// it faces the light; the light leans toward the cursor, so moving a window or
// the pointer slides the highlight round the rim.
inline constexpr auto LIGHT_STRENGTH       = "plugin:hyprglass:light_strength";        // light added on the rim (0 = none; the glass can still bend)
inline constexpr auto LIGHT_BEND           = "plugin:hyprglass:light_bend";            // how much the rim bends where it faces the light (0 = none)
inline constexpr auto LIGHT_X              = "plugin:hyprglass:light_x";               // across all monitors: 0 = left edge, 1 = right edge
inline constexpr auto LIGHT_Y              = "plugin:hyprglass:light_y";               // 0 = top of the monitors, below 0 = above them
inline constexpr auto LIGHT_CURSOR         = "plugin:hyprglass:light_cursor";          // how far the light leans toward the cursor (0-1)
inline constexpr auto LIGHT_LAG            = "plugin:hyprglass:light_lag";             // seconds light + parallax take to catch the cursor (0 = instant)
inline constexpr auto LIGHT_SHARPNESS      = "plugin:hyprglass:light_sharpness";       // how tightly the highlight gathers
inline constexpr auto LIGHT_WIDTH          = "plugin:hyprglass:light_width";           // how far into the glass the rim light reaches, px
inline constexpr auto LIGHT_FAR            = "plugin:hyprglass:light_far";             // soft reflection on the side facing away
inline constexpr auto LIGHT_INACTIVE       = "plugin:hyprglass:light_inactive";        // unfocused windows' share of the rim light
inline constexpr auto LIGHT_COLOR          = "plugin:hyprglass:light_color";           // 0xRRGGBB
inline constexpr auto LIGHT_DRIFT          = "plugin:hyprglass:light_drift";           // the light slowly wanders this far (share of the desktop, 0 = still)
inline constexpr auto LIGHT_DRIFT_PERIOD   = "plugin:hyprglass:light_drift_period";    // seconds for one slow wander
// Parallax: the view behind the glass shifts against the pane as the cursor moves.
inline constexpr auto PARALLAX_STRENGTH    = "plugin:hyprglass:parallax_strength";     // px (0 = off)
inline constexpr auto PARALLAX_DEPTH       = "plugin:hyprglass:parallax_depth";        // extra shift toward the rim, so the glass reads as thick
// Oil film: slow iridescent swirls on the glass, like oil on water.
inline constexpr auto OIL_AMOUNT           = "plugin:hyprglass:oil_amount";            // 0 = off
inline constexpr auto OIL_SPEED            = "plugin:hyprglass:oil_speed";             // how fast the film flows
inline constexpr auto OIL_SCALE            = "plugin:hyprglass:oil_scale";             // swirl size, px
inline constexpr auto OIL_COLOR            = "plugin:hyprglass:oil_color";             // 0 = clear sheen, 1 = full iridescence
inline constexpr auto OIL_WARP             = "plugin:hyprglass:oil_warp";              // how much the film ripples the view behind
inline constexpr auto OIL_INACTIVE         = "plugin:hyprglass:oil_inactive";          // unfocused windows' share (0 = focused only)
inline constexpr auto OIL_FPS              = "plugin:hyprglass:oil_fps";               // redraws per second while it flows
// Click glow: clicking a glass window energizes it from the click point.
inline constexpr auto GLOW_STRENGTH        = "plugin:hyprglass:glow_strength";         // light added (0 = none; the glass can still flex)
inline constexpr auto GLOW_FLEX            = "plugin:hyprglass:glow_flex";             // how much the ripple flexes the glass (0 = none)
inline constexpr auto GLOW_DURATION        = "plugin:hyprglass:glow_duration";         // seconds to spread and fade
inline constexpr auto GLOW_SPREAD          = "plugin:hyprglass:glow_spread";           // how far it travels, share of the window size
inline constexpr auto GLOW_RING            = "plugin:hyprglass:glow_ring";             // ring width, px
// Materialize: new windows appear by ramping up their bending, not by fading.
inline constexpr auto MATERIALIZE_DURATION = "plugin:hyprglass:materialize_duration";  // seconds (0 = off)

// Preset keyword, registered as unscoped because Hyprlang does not dispatch
// scoped keyword handlers inside the plugin special category.
inline constexpr auto PRESET_KEYWORD = "preset";

// Overridable — global level
inline constexpr auto BLUR_STRENGTH        = "plugin:hyprglass:blur_strength";
inline constexpr auto BLUR_ITERATIONS      = "plugin:hyprglass:blur_iterations";
inline constexpr auto REFRACTION_STRENGTH  = "plugin:hyprglass:refraction_strength";
inline constexpr auto CHROMATIC_ABERRATION = "plugin:hyprglass:chromatic_aberration";
inline constexpr auto FRESNEL_STRENGTH     = "plugin:hyprglass:fresnel_strength";
inline constexpr auto SPECULAR_STRENGTH    = "plugin:hyprglass:specular_strength";
inline constexpr auto SPECULAR_ANGLE       = "plugin:hyprglass:specular_angle";
inline constexpr auto GLASS_OPACITY        = "plugin:hyprglass:glass_opacity";
inline constexpr auto EDGE_THICKNESS       = "plugin:hyprglass:edge_thickness";
inline constexpr auto TINT_COLOR           = "plugin:hyprglass:tint_color";
inline constexpr auto LENS_DISTORTION      = "plugin:hyprglass:lens_distortion";
inline constexpr auto BRIGHTNESS           = "plugin:hyprglass:brightness";
inline constexpr auto CONTRAST             = "plugin:hyprglass:contrast";
inline constexpr auto SATURATION           = "plugin:hyprglass:saturation";
inline constexpr auto VIBRANCY             = "plugin:hyprglass:vibrancy";
inline constexpr auto VIBRANCY_DARKNESS    = "plugin:hyprglass:vibrancy_darkness";
inline constexpr auto ADAPTIVE_DIM          = "plugin:hyprglass:adaptive_dim";
inline constexpr auto ADAPTIVE_BOOST        = "plugin:hyprglass:adaptive_boost";
inline constexpr auto REFRACTION_FLOW       = "plugin:hyprglass:refraction_flow";
inline constexpr auto REFRACTION_SPREAD     = "plugin:hyprglass:refraction_spread";
inline constexpr auto FRESNEL_TINT          = "plugin:hyprglass:fresnel_tint";
inline constexpr auto BEVEL_STRENGTH        = "plugin:hyprglass:bevel_strength";
inline constexpr auto BEVEL_SIZE            = "plugin:hyprglass:bevel_size";
inline constexpr auto FRESNEL_COLOR         = "plugin:hyprglass:fresnel_color";
inline constexpr auto BEVEL_COLOR           = "plugin:hyprglass:bevel_color";
inline constexpr auto BEVEL_TINT            = "plugin:hyprglass:bevel_tint";
inline constexpr auto BEVEL_ANGLE           = "plugin:hyprglass:bevel_angle";
inline constexpr auto BEVEL_SHADOW          = "plugin:hyprglass:bevel_shadow";
inline constexpr auto SELF_SAMPLE           = "plugin:hyprglass:self_sample";

// Layer surface support
inline constexpr auto LAYERS_ENABLED            = "plugin:hyprglass:layers:enabled";
inline constexpr auto LAYERS_NAMESPACES         = "plugin:hyprglass:layers:namespaces";
inline constexpr auto LAYERS_EXCLUDE_NAMESPACES = "plugin:hyprglass:layers:exclude_namespaces";
inline constexpr auto LAYERS_PRESET             = "plugin:hyprglass:layers:preset";
inline constexpr auto LAYERS_NAMESPACE_PRESETS          = "plugin:hyprglass:layers:namespace_presets";
inline constexpr auto LAYERS_NAMESPACE_MASK_THRESHOLDS  = "plugin:hyprglass:layers:namespace_mask_thresholds";
inline constexpr auto LAYERS_NAMESPACE_LIVE_RESAMPLE    = "plugin:hyprglass:layers:namespace_live_resample";
inline constexpr auto LAYERS_LIVE_RESAMPLE              = "plugin:hyprglass:layers:live_resample";
inline constexpr auto LAYERS_LIVE_RESAMPLE_FPS          = "plugin:hyprglass:layers:live_resample_fps";
inline constexpr auto LAYERS_FORCE_LIVE_RESAMPLE        = "plugin:hyprglass:layers:force_live_resample";
inline constexpr auto LAYERS_MASK_MODE                  = "plugin:hyprglass:layers:mask_mode";
inline constexpr auto LAYERS_NAMESPACE_MASK_MODES       = "plugin:hyprglass:layers:namespace_mask_modes";
inline constexpr auto LAYERS_MANAGE_BLUR                = "plugin:hyprglass:layers:manage_blur";

// Window background cache kill switch; commit-driven invalidation (single
// global bool, no per-namespace concept for windows) and its throttle —
// mirrors the layers:live_resample/live_resample_fps keys above.
inline constexpr auto WINDOWS_BACKGROUND_CACHE  = "plugin:hyprglass:windows:background_cache";
inline constexpr auto WINDOWS_LIVE_RESAMPLE     = "plugin:hyprglass:windows:live_resample";
inline constexpr auto WINDOWS_LIVE_RESAMPLE_FPS = "plugin:hyprglass:windows:live_resample_fps";

// Overridable — dark theme overrides
inline constexpr auto DARK_BLUR_STRENGTH        = "plugin:hyprglass:dark:blur_strength";
inline constexpr auto DARK_BLUR_ITERATIONS      = "plugin:hyprglass:dark:blur_iterations";
inline constexpr auto DARK_REFRACTION_STRENGTH  = "plugin:hyprglass:dark:refraction_strength";
inline constexpr auto DARK_CHROMATIC_ABERRATION = "plugin:hyprglass:dark:chromatic_aberration";
inline constexpr auto DARK_FRESNEL_STRENGTH     = "plugin:hyprglass:dark:fresnel_strength";
inline constexpr auto DARK_SPECULAR_STRENGTH    = "plugin:hyprglass:dark:specular_strength";
inline constexpr auto DARK_SPECULAR_ANGLE       = "plugin:hyprglass:dark:specular_angle";
inline constexpr auto DARK_GLASS_OPACITY        = "plugin:hyprglass:dark:glass_opacity";
inline constexpr auto DARK_EDGE_THICKNESS       = "plugin:hyprglass:dark:edge_thickness";
inline constexpr auto DARK_TINT_COLOR           = "plugin:hyprglass:dark:tint_color";
inline constexpr auto DARK_LENS_DISTORTION      = "plugin:hyprglass:dark:lens_distortion";
inline constexpr auto DARK_BRIGHTNESS           = "plugin:hyprglass:dark:brightness";
inline constexpr auto DARK_CONTRAST             = "plugin:hyprglass:dark:contrast";
inline constexpr auto DARK_SATURATION           = "plugin:hyprglass:dark:saturation";
inline constexpr auto DARK_VIBRANCY             = "plugin:hyprglass:dark:vibrancy";
inline constexpr auto DARK_VIBRANCY_DARKNESS    = "plugin:hyprglass:dark:vibrancy_darkness";
inline constexpr auto DARK_ADAPTIVE_DIM          = "plugin:hyprglass:dark:adaptive_dim";
inline constexpr auto DARK_ADAPTIVE_BOOST        = "plugin:hyprglass:dark:adaptive_boost";
inline constexpr auto DARK_REFRACTION_FLOW      = "plugin:hyprglass:dark:refraction_flow";
inline constexpr auto DARK_REFRACTION_SPREAD    = "plugin:hyprglass:dark:refraction_spread";
inline constexpr auto DARK_FRESNEL_TINT         = "plugin:hyprglass:dark:fresnel_tint";
inline constexpr auto DARK_BEVEL_STRENGTH       = "plugin:hyprglass:dark:bevel_strength";
inline constexpr auto DARK_BEVEL_SIZE           = "plugin:hyprglass:dark:bevel_size";
inline constexpr auto DARK_FRESNEL_COLOR        = "plugin:hyprglass:dark:fresnel_color";
inline constexpr auto DARK_BEVEL_COLOR          = "plugin:hyprglass:dark:bevel_color";
inline constexpr auto DARK_BEVEL_TINT           = "plugin:hyprglass:dark:bevel_tint";
inline constexpr auto DARK_BEVEL_ANGLE          = "plugin:hyprglass:dark:bevel_angle";
inline constexpr auto DARK_BEVEL_SHADOW         = "plugin:hyprglass:dark:bevel_shadow";
inline constexpr auto DARK_SELF_SAMPLE          = "plugin:hyprglass:dark:self_sample";

// Overridable — light theme overrides
inline constexpr auto LIGHT_BLUR_STRENGTH        = "plugin:hyprglass:light:blur_strength";
inline constexpr auto LIGHT_BLUR_ITERATIONS      = "plugin:hyprglass:light:blur_iterations";
inline constexpr auto LIGHT_REFRACTION_STRENGTH  = "plugin:hyprglass:light:refraction_strength";
inline constexpr auto LIGHT_CHROMATIC_ABERRATION = "plugin:hyprglass:light:chromatic_aberration";
inline constexpr auto LIGHT_FRESNEL_STRENGTH     = "plugin:hyprglass:light:fresnel_strength";
inline constexpr auto LIGHT_SPECULAR_STRENGTH    = "plugin:hyprglass:light:specular_strength";
inline constexpr auto LIGHT_SPECULAR_ANGLE       = "plugin:hyprglass:light:specular_angle";
inline constexpr auto LIGHT_GLASS_OPACITY        = "plugin:hyprglass:light:glass_opacity";
inline constexpr auto LIGHT_EDGE_THICKNESS       = "plugin:hyprglass:light:edge_thickness";
inline constexpr auto LIGHT_TINT_COLOR           = "plugin:hyprglass:light:tint_color";
inline constexpr auto LIGHT_LENS_DISTORTION      = "plugin:hyprglass:light:lens_distortion";
inline constexpr auto LIGHT_BRIGHTNESS           = "plugin:hyprglass:light:brightness";
inline constexpr auto LIGHT_CONTRAST             = "plugin:hyprglass:light:contrast";
inline constexpr auto LIGHT_SATURATION           = "plugin:hyprglass:light:saturation";
inline constexpr auto LIGHT_VIBRANCY             = "plugin:hyprglass:light:vibrancy";
inline constexpr auto LIGHT_VIBRANCY_DARKNESS    = "plugin:hyprglass:light:vibrancy_darkness";
inline constexpr auto LIGHT_ADAPTIVE_DIM          = "plugin:hyprglass:light:adaptive_dim";
inline constexpr auto LIGHT_ADAPTIVE_BOOST        = "plugin:hyprglass:light:adaptive_boost";
inline constexpr auto LIGHT_REFRACTION_FLOW      = "plugin:hyprglass:light:refraction_flow";
inline constexpr auto LIGHT_REFRACTION_SPREAD    = "plugin:hyprglass:light:refraction_spread";
inline constexpr auto LIGHT_FRESNEL_TINT         = "plugin:hyprglass:light:fresnel_tint";
inline constexpr auto LIGHT_BEVEL_STRENGTH       = "plugin:hyprglass:light:bevel_strength";
inline constexpr auto LIGHT_BEVEL_SIZE           = "plugin:hyprglass:light:bevel_size";
inline constexpr auto LIGHT_FRESNEL_COLOR        = "plugin:hyprglass:light:fresnel_color";
inline constexpr auto LIGHT_BEVEL_COLOR          = "plugin:hyprglass:light:bevel_color";
inline constexpr auto LIGHT_BEVEL_TINT           = "plugin:hyprglass:light:bevel_tint";
inline constexpr auto LIGHT_BEVEL_ANGLE          = "plugin:hyprglass:light:bevel_angle";
inline constexpr auto LIGHT_BEVEL_SHADOW         = "plugin:hyprglass:light:bevel_shadow";
inline constexpr auto LIGHT_SELF_SAMPLE          = "plugin:hyprglass:light:self_sample";

} // namespace ConfigKeys

// Cached pointers for a single config layer (built-in dark/light/global)
struct SOverridableConfig {
    Hyprlang::FLOAT* const* blurStrength        = nullptr;
    Hyprlang::INT* const*   blurIterations      = nullptr;
    Hyprlang::FLOAT* const* refractionStrength  = nullptr;
    Hyprlang::FLOAT* const* chromaticAberration = nullptr;
    Hyprlang::FLOAT* const* fresnelStrength     = nullptr;
    Hyprlang::FLOAT* const* specularStrength    = nullptr;
    Hyprlang::FLOAT* const* specularAngle       = nullptr;
    Hyprlang::FLOAT* const* glassOpacity        = nullptr;
    Hyprlang::FLOAT* const* edgeThickness       = nullptr;
    Hyprlang::INT* const*   tintColor           = nullptr;
    Hyprlang::FLOAT* const* lensDistortion      = nullptr;
    Hyprlang::FLOAT* const* brightness          = nullptr;
    Hyprlang::FLOAT* const* contrast            = nullptr;
    Hyprlang::FLOAT* const* saturation          = nullptr;
    Hyprlang::FLOAT* const* vibrancy            = nullptr;
    Hyprlang::FLOAT* const* vibrancyDarkness    = nullptr;
    Hyprlang::FLOAT* const* adaptiveDim         = nullptr;
    Hyprlang::FLOAT* const* adaptiveBoost       = nullptr;
    Hyprlang::FLOAT* const* refractionFlow      = nullptr;
    Hyprlang::FLOAT* const* refractionSpread    = nullptr;
    Hyprlang::FLOAT* const* fresnelTint         = nullptr;
    Hyprlang::FLOAT* const* bevelStrength       = nullptr;
    Hyprlang::FLOAT* const* bevelSize           = nullptr;
    Hyprlang::INT* const*   fresnelColor        = nullptr;
    Hyprlang::INT* const*   bevelColor          = nullptr;
    Hyprlang::FLOAT* const* bevelTint           = nullptr;
    Hyprlang::FLOAT* const* bevelAngle          = nullptr;
    Hyprlang::FLOAT* const* bevelShadow         = nullptr;
    Hyprlang::FLOAT* const* selfSample          = nullptr;
};

// Plain values for a user-defined preset layer (all sentinel = not set → inherit)
struct SPresetValues {
    float   blurStrength       = static_cast<float>(SENTINEL_FLOAT);
    int64_t blurIterations     = SENTINEL_INT;
    float   refractionStrength = static_cast<float>(SENTINEL_FLOAT);
    float   chromaticAberration = static_cast<float>(SENTINEL_FLOAT);
    float   fresnelStrength    = static_cast<float>(SENTINEL_FLOAT);
    float   specularStrength   = static_cast<float>(SENTINEL_FLOAT);
    float   specularAngle      = static_cast<float>(SENTINEL_FLOAT);
    float   glassOpacity       = static_cast<float>(SENTINEL_FLOAT);
    float   edgeThickness      = static_cast<float>(SENTINEL_FLOAT);
    int64_t tintColor          = SENTINEL_INT;
    float   lensDistortion     = static_cast<float>(SENTINEL_FLOAT);
    float   brightness         = static_cast<float>(SENTINEL_FLOAT);
    float   contrast           = static_cast<float>(SENTINEL_FLOAT);
    float   saturation         = static_cast<float>(SENTINEL_FLOAT);
    float   vibrancy           = static_cast<float>(SENTINEL_FLOAT);
    float   vibrancyDarkness   = static_cast<float>(SENTINEL_FLOAT);
    float   adaptiveDim        = static_cast<float>(SENTINEL_FLOAT);
    float   adaptiveBoost      = static_cast<float>(SENTINEL_FLOAT);
    float   refractionFlow     = static_cast<float>(SENTINEL_FLOAT);
    float   refractionSpread   = static_cast<float>(SENTINEL_FLOAT);
    float   fresnelTint        = static_cast<float>(SENTINEL_FLOAT);
    float   bevelStrength      = static_cast<float>(SENTINEL_FLOAT);
    float   bevelSize          = static_cast<float>(SENTINEL_FLOAT);
    int64_t fresnelColor       = SENTINEL_INT;
    int64_t bevelColor         = SENTINEL_INT;
    float   bevelTint          = static_cast<float>(SENTINEL_FLOAT);
    float   bevelAngle         = static_cast<float>(SENTINEL_FLOAT);
    float   bevelShadow        = static_cast<float>(SENTINEL_FLOAT);
    float   selfSample         = static_cast<float>(SENTINEL_FLOAT);
};

struct SCustomPreset {
    std::string   name;
    std::string   inherits;
    SPresetValues shared;
    SPresetValues dark;
    SPresetValues light;
};

struct StringConfigPtr {
    void* const*          dataptr = nullptr;
    const std::type_info* type    = nullptr;
};

inline std::string_view readStringConfig(const StringConfigPtr& ptr) {
    if (!ptr.dataptr || !ptr.type)
        return {};

    if (*ptr.type == typeid(Config::STRING)) {
        const auto* value = *reinterpret_cast<Config::STRING* const*>(ptr.dataptr);
        return value ? std::string_view(*value) : std::string_view{};
    }

    if (*ptr.type == typeid(Hyprlang::STRING)) {
        const auto value = *reinterpret_cast<Hyprlang::STRING const*>(ptr.dataptr);
        return value ? std::string_view(value) : std::string_view{};
    }

    return {};
}

struct SPluginConfig {
    Hyprlang::INT* const* enabled           = nullptr;
    // Glass replaces Hyprland's blur for glassed windows: when set, the plugin
    // marks them with the noblur window property so Hyprland composites them
    // against the live framebuffer (which contains the glass) instead of its
    // pre-frame cached blur.
    Hyprlang::INT* const* manageWindowBlur  = nullptr;
    // Skip glass for windows CWindow::opaque() reports as opaque: nothing behind
    // them is visible, so sampling and blurring their background is wasted work.
    Hyprlang::INT* const* skipOpaqueWindows = nullptr;
    // Derives a smaller blur pass count from the requested radius (GlassRenderer::
    // foldBlurPasses) instead of always running blur_iterations passes at full radius.
    Hyprlang::INT* const* blurFold = nullptr;

    // glass light
    Hyprlang::FLOAT* const* lightStrength = nullptr;
    Hyprlang::FLOAT* const* lightX = nullptr;
    Hyprlang::FLOAT* const* lightY = nullptr;
    Hyprlang::FLOAT* const* lightSharpness = nullptr;
    Hyprlang::FLOAT* const* lightWidth = nullptr;
    Hyprlang::FLOAT* const* lightFar = nullptr;
    Hyprlang::FLOAT* const* lightBend = nullptr;
    Hyprlang::FLOAT* const* lightCursor = nullptr;
    Hyprlang::FLOAT* const* lightInactive = nullptr;
    Hyprlang::FLOAT* const* glowStrength = nullptr;
    Hyprlang::FLOAT* const* glowDuration = nullptr;
    Hyprlang::FLOAT* const* glowSpread = nullptr;
    Hyprlang::FLOAT* const* glowRing = nullptr;
    Hyprlang::FLOAT* const* glowFlex = nullptr;
    Hyprlang::FLOAT* const* materializeDuration = nullptr;
    Hyprlang::INT* const*   lightColor = nullptr;
    Hyprlang::FLOAT* const* lightDrift = nullptr;
    Hyprlang::FLOAT* const* lightDriftPeriod = nullptr;
    Hyprlang::FLOAT* const* lightLag = nullptr;
    Hyprlang::FLOAT* const* parallaxStrength = nullptr;
    Hyprlang::FLOAT* const* parallaxDepth = nullptr;
    Hyprlang::FLOAT* const* oilAmount = nullptr;
    Hyprlang::FLOAT* const* oilSpeed = nullptr;
    Hyprlang::FLOAT* const* oilScale = nullptr;
    Hyprlang::FLOAT* const* oilColor = nullptr;
    Hyprlang::FLOAT* const* oilWarp = nullptr;
    Hyprlang::FLOAT* const* oilInactive = nullptr;
    Hyprlang::FLOAT* const* oilFps = nullptr;
    StringConfigPtr      defaultTheme;
    StringConfigPtr      defaultPreset;

    // Performance diagnostics (see Diagnostics.hpp for the hyprctl side)
    StringConfigPtr       debugMode;
    Hyprlang::INT* const* debugTimers = nullptr;

    Hyprlang::INT* const* layersEnabled                  = nullptr;
    StringConfigPtr       layersNamespaces;
    StringConfigPtr       layersExcludeNamespaces;
    StringConfigPtr       layersPreset;
    StringConfigPtr       layersNamespacePresets;
    StringConfigPtr       layersNamespaceMaskThresholds;
    StringConfigPtr       layersNamespaceLiveResample;
    Hyprlang::INT* const* layersLiveResample             = nullptr;
    Hyprlang::INT* const* layersLiveResampleFps          = nullptr;
    Hyprlang::INT* const* layersForceLiveResample        = nullptr;
    StringConfigPtr       layersMaskMode;
    StringConfigPtr       layersNamespaceMaskModes;
    Hyprlang::INT* const* layersManageBlur               = nullptr;

    Hyprlang::INT* const* windowsBackgroundCache  = nullptr;
    Hyprlang::INT* const* windowsLiveResample     = nullptr;
    Hyprlang::INT* const* windowsLiveResampleFps  = nullptr;

    SOverridableConfig global;
    SOverridableConfig dark;
    SOverridableConfig light;
};

// Context for preset-aware value resolution
struct SResolveContext {
    const std::string&                                    presetName;
    bool                                                  isDark;
    const SPluginConfig&                                  config;
    const std::unordered_map<std::string, SCustomPreset>& customPresets;
};

// Preset-aware resolution: preset chain → built-in theme → global → hardcoded
[[nodiscard]] float resolvePresetFloat(
    const SResolveContext& context,
    float SPresetValues::* presetField,
    Hyprlang::FLOAT* const* SOverridableConfig::* configField,
    float hardcodedDefault = static_cast<float>(SENTINEL_FLOAT));

[[nodiscard]] int64_t resolvePresetInt(
    const SResolveContext& context,
    int64_t SPresetValues::* presetField,
    Hyprlang::INT* const* SOverridableConfig::* configField,
    int64_t hardcodedDefault = SENTINEL_INT);

// True when any tier can resolve self_sample above 0. Answers "could a window
// sample itself" without a window at hand, so the surface observer can stay
// unarmed on the default config.
[[nodiscard]] bool anySelfSampleConfigured(const SPluginConfig&                                  config,
                                           const std::unordered_map<std::string, SCustomPreset>& customPresets);

void registerConfig(HANDLE handle);
void initConfigPointers(HANDLE handle, SPluginConfig& config);

// Preset keyword handler (registered via addConfigKeyword)
Hyprlang::CParseResult handlePresetKeyword(const char* command, const char* value);

// Clear pending presets/layers before config re-parse (called from preConfigReload callback)
void clearPendingPresets();
void clearPendingLayers();

// Swap pending data into active maps (called from configReloaded callback)
void commitPendingPresets();
void commitPendingLayers();

// Validate config values and notify user of misconfigurations
void validateConfig();
