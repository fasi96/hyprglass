#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

// Optional glass effects. Each one is compiled into its own variant of the glass
// program, behind a GLSL #define, so a draw that leaves every effect off runs the
// default program, unchanged, and pays nothing for effects it does not use: a
// runtime branch in one big program costs every draw registers and occupancy.
using GlassFeatureMask = uint32_t;

inline constexpr GlassFeatureMask GLASS_FEATURE_NONE = 0;
// edge_height > 0: the rim is a lens that refracts (and can mirror) what is around it.
inline constexpr GlassFeatureMask GLASS_FEATURE_LENS_EDGE = 1u << 0;
// focus_light > 0 on the active window: a light along its glass edge.
inline constexpr GlassFeatureMask GLASS_FEATURE_FOCUS_LIGHT = 1u << 1;
// frost > 0 and windows behind the glass: the backdrop is blurred further.
inline constexpr GlassFeatureMask GLASS_FEATURE_FROST = 1u << 2;
// A layer with mask_mode = contour: its edge follows a traced outline (Contour.hpp).
inline constexpr GlassFeatureMask GLASS_FEATURE_CONTOUR = 1u << 3;

struct SGlassFeatureDefine {
    GlassFeatureMask bit;
    std::string_view name; // defined to 1 in the variant's source
};

// One entry per feature bit.
inline constexpr std::array GLASS_FEATURE_DEFINES{
    SGlassFeatureDefine{GLASS_FEATURE_LENS_EDGE, "HYPRGLASS_FEATURE_LENS_EDGE"},
    SGlassFeatureDefine{GLASS_FEATURE_FOCUS_LIGHT, "HYPRGLASS_FEATURE_FOCUS_LIGHT"},
    SGlassFeatureDefine{GLASS_FEATURE_FROST, "HYPRGLASS_FEATURE_FROST"},
    SGlassFeatureDefine{GLASS_FEATURE_CONTOUR, "HYPRGLASS_FEATURE_CONTOUR"},
};

[[nodiscard]] consteval GlassFeatureMask allGlassFeatures() {
    GlassFeatureMask all = GLASS_FEATURE_NONE;
    for (const auto& feature : GLASS_FEATURE_DEFINES) {
        if (feature.bit == 0 || (feature.bit & (feature.bit - 1)) != 0 || (all & feature.bit) != 0)
            throw "each glass feature needs its own single bit";
        all |= feature.bit;
    }
    return all;
}

inline constexpr GlassFeatureMask GLASS_FEATURE_ALL = allGlassFeatures();

// The glass program's source for `features`. With no feature it is `base` itself,
// byte for byte; otherwise the features' #defines follow the #version line, then a
// #line directive keeps compile errors at the default source's line numbers.
[[nodiscard]] inline std::string glassVariantSource(std::string_view base, GlassFeatureMask features) {
    std::string source{base};
    features &= GLASS_FEATURE_ALL;
    if (features == GLASS_FEATURE_NONE)
        return source;

    const auto version = source.find("#version");
    const auto lineEnd = version == std::string::npos ? std::string::npos : source.find('\n', version);
    if (lineEnd == std::string::npos)
        return source;

    std::string defines;
    for (const auto& feature : GLASS_FEATURE_DEFINES)
        if (features & feature.bit)
            defines += std::format("#define {} 1\n", feature.name);

    const auto nextLine = 2 + std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(lineEnd), '\n');
    defines += std::format("#line {}\n", nextLine);

    source.insert(lineEnd + 1, defines);
    return source;
}
