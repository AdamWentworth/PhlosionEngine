#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace engine::render {

// Per-view, per-draw values. Directions point toward the sun; radiance is
// scene-linear, exposure is a multiplier, and distances/heights are in metres.
// Disabled inputs preserve the established material and inspection lighting.
struct WorldEnvironmentLighting {
    bool enabled = false;
    bool skyDraw = false;
    std::array<float, 3> sunDirection{0, 1, 0};
    std::array<float, 3> sunRadiance{0, 0, 0};
    std::array<float, 3> ambientRadiance{0, 0, 0};
    float exposure = 1.15f;
    std::array<float, 3> hazeRadiance{0, 0, 0};
    float hazeDensity = 0;
    float hazeBaseHeight = 0;
    float hazeHeightFalloff = 0;
    float hazeMaxOpacity = .85f;
    float skyIntensity = 1;
    std::array<float, 3> nightSkyRadiance{.002f, .003f, .008f};
};

using WorldEnvironmentUniforms = std::array<std::array<float, 4>, 6>;

inline WorldEnvironmentUniforms packWorldEnvironment(const WorldEnvironmentLighting& e) {
    WorldEnvironmentUniforms out{};
    if (!e.enabled) return out;
    const std::array<float, 24> values{
        e.sunDirection[0], e.sunDirection[1], e.sunDirection[2], e.exposure,
        e.sunRadiance[0], e.sunRadiance[1], e.sunRadiance[2], e.hazeDensity,
        e.ambientRadiance[0], e.ambientRadiance[1], e.ambientRadiance[2], e.hazeBaseHeight,
        e.hazeRadiance[0], e.hazeRadiance[1], e.hazeRadiance[2], e.hazeHeightFalloff,
        e.nightSkyRadiance[0], e.nightSkyRadiance[1], e.nightSkyRadiance[2], e.skyIntensity,
        e.hazeMaxOpacity, 0, 0, 0};
    for (float v : values) if (!std::isfinite(v)) return out;
    const float length = std::hypot(e.sunDirection[0], e.sunDirection[1], e.sunDirection[2]);
    if (!std::isfinite(length) || length < 1e-5f || e.exposure <= 0 || e.hazeDensity < 0 || e.hazeHeightFalloff < 0) return out;
    const auto nonnegative = [](const auto& c) { return std::array<float, 3>{
        std::clamp(c[0],0.f,65504.f), std::clamp(c[1],0.f,65504.f), std::clamp(c[2],0.f,65504.f)}; };
    const auto sun = nonnegative(e.sunRadiance), ambient = nonnegative(e.ambientRadiance);
    const auto haze = nonnegative(e.hazeRadiance), night = nonnegative(e.nightSkyRadiance);
    out[0] = {e.sunDirection[0]/length,e.sunDirection[1]/length,e.sunDirection[2]/length,1};
    out[1] = {sun[0],sun[1],sun[2],e.skyDraw ? 1.f : 0.f};
    out[2] = {ambient[0],ambient[1],ambient[2],std::clamp(e.exposure,.01f,16.f)};
    out[3] = {haze[0],haze[1],haze[2],std::min(e.hazeDensity,1.f)};
    out[4] = {std::clamp(e.hazeBaseHeight,-100000.f,100000.f),std::min(e.hazeHeightFalloff,1.f),std::clamp(e.hazeMaxOpacity,0.f,1.f),std::clamp(e.skyIntensity,0.f,32.f)};
    out[5] = {night[0],night[1],night[2],0};
    return out;
}

} // namespace engine::render
