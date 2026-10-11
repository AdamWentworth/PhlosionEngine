#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace engine::render {

struct WorldPointLight {
    std::array<float, 3> position{};
    float radius = 0;
    std::array<float, 3> intensity{};
};

using WorldPointLightUniforms = std::array<std::array<float, 4>, 8>;

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
    // Angular radii/halo width are radians. Moon phase is illuminated fraction;
    // this authored phase does not imply an astronomical calendar.
    float sunAngularRadius = .00465f;
    float sunHaloStrength = 0;
    float sunHaloWidth = .025f;
    std::array<float, 3> moonDirection{0, 1, 0};
    std::array<float, 3> moonRadiance{0, 0, 0};
    float moonAngularRadius = 0;
    float moonPhase = 1;
    float starIntensity = 0;
    std::array<WorldPointLight, 4> pointLights{};
};

using WorldEnvironmentUniforms = std::array<std::array<float, 4>, 9>;

inline WorldEnvironmentUniforms packWorldEnvironment(const WorldEnvironmentLighting& e) {
    WorldEnvironmentUniforms out{};
    if (!e.enabled) return out;
    const std::array<float, 36> values{
        e.sunDirection[0], e.sunDirection[1], e.sunDirection[2], e.exposure,
        e.sunRadiance[0], e.sunRadiance[1], e.sunRadiance[2], e.hazeDensity,
        e.ambientRadiance[0], e.ambientRadiance[1], e.ambientRadiance[2], e.hazeBaseHeight,
        e.hazeRadiance[0], e.hazeRadiance[1], e.hazeRadiance[2], e.hazeHeightFalloff,
        e.nightSkyRadiance[0], e.nightSkyRadiance[1], e.nightSkyRadiance[2], e.skyIntensity,
        e.hazeMaxOpacity, 0, 0, 0,
        e.moonDirection[0], e.moonDirection[1], e.moonDirection[2], e.moonAngularRadius,
        e.moonRadiance[0], e.moonRadiance[1], e.moonRadiance[2], e.moonPhase,
        e.sunAngularRadius, e.sunHaloStrength, e.sunHaloWidth, e.starIntensity};
    for (float v : values) if (!std::isfinite(v)) return out;
    const float length = std::hypot(e.sunDirection[0], e.sunDirection[1], e.sunDirection[2]);
    const float moonLength = std::hypot(e.moonDirection[0], e.moonDirection[1], e.moonDirection[2]);
    if (!std::isfinite(length) || length < 1e-5f || !std::isfinite(moonLength) || moonLength < 1e-5f || e.exposure <= 0 || e.hazeDensity < 0 || e.hazeHeightFalloff < 0) return out;
    const auto nonnegative = [](const auto& c) { return std::array<float, 3>{
        std::clamp(c[0],0.f,65504.f), std::clamp(c[1],0.f,65504.f), std::clamp(c[2],0.f,65504.f)}; };
    const auto sun = nonnegative(e.sunRadiance), ambient = nonnegative(e.ambientRadiance);
    const auto haze = nonnegative(e.hazeRadiance), night = nonnegative(e.nightSkyRadiance);
    const auto moon = nonnegative(e.moonRadiance);
    out[0] = {e.sunDirection[0]/length,e.sunDirection[1]/length,e.sunDirection[2]/length,1};
    out[1] = {sun[0],sun[1],sun[2],e.skyDraw ? 1.f : 0.f};
    out[2] = {ambient[0],ambient[1],ambient[2],std::clamp(e.exposure,.01f,16.f)};
    out[3] = {haze[0],haze[1],haze[2],std::min(e.hazeDensity,1.f)};
    out[4] = {std::clamp(e.hazeBaseHeight,-100000.f,100000.f),std::min(e.hazeHeightFalloff,1.f),std::clamp(e.hazeMaxOpacity,0.f,1.f),std::clamp(e.skyIntensity,0.f,32.f)};
    out[5] = {night[0],night[1],night[2],0};
    out[6] = {e.moonDirection[0]/moonLength,e.moonDirection[1]/moonLength,e.moonDirection[2]/moonLength,std::clamp(e.moonAngularRadius,0.f,.04f)};
    out[7] = {moon[0],moon[1],moon[2],std::clamp(e.moonPhase,0.f,1.f)};
    out[8] = {std::clamp(e.sunAngularRadius,0.f,.04f),std::clamp(e.sunHaloStrength,0.f,1.f),std::clamp(e.sunHaloWidth,.001f,.15f),std::clamp(e.starIntensity,0.f,1.f)};
    return out;
}

inline WorldPointLightUniforms packWorldPointLights(const WorldEnvironmentLighting& e) {
    WorldPointLightUniforms out{};
    if (packWorldEnvironment(e)[0][3] == 0) return out;
    for (std::size_t i = 0; i < e.pointLights.size(); ++i) {
        const auto& light = e.pointLights[i];
        const std::array values{light.position[0], light.position[1], light.position[2],
            light.radius, light.intensity[0], light.intensity[1], light.intensity[2]};
        for (float value : values) if (!std::isfinite(value)) return {};
        if (light.radius < 0) return {};
        if (light.radius == 0) continue;
        out[i*2] = {light.position[0], light.position[1], light.position[2],
            std::min(light.radius, 1000.f)};
        out[i*2+1] = {std::clamp(light.intensity[0], 0.f, 65504.f),
            std::clamp(light.intensity[1], 0.f, 65504.f),
            std::clamp(light.intensity[2], 0.f, 65504.f), 0};
    }
    return out;
}

} // namespace engine::render
