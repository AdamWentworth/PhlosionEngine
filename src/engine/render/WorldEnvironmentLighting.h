#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace engine::render {

struct WorldPointLight {
    std::array<float, 3> position{};
    float radius = 0;
    std::array<float, 3> intensity{};
};

using WorldPointLightUniforms = std::array<std::array<float, 4>, 8>;

enum class WorldRectLightResponse : std::uint32_t { Ltc = 0, FiniteLine = 1 };

// One-sided rectangular emitter. Cross(halfU, halfV) points out of its luminous
// face; axes are perpendicular world-space half extents, radiance is linear.
struct WorldRectLight {
    std::array<float, 3> position{};
    float range = 0;
    std::array<float, 3> halfU{}, halfV{}, radiance{};
    // FiniteLine uses an inscribed line/sphere for direct surface response.
    // Normal intensity is radiance * rectangle area; other transport retains
    // this same physical luminous rectangle, radiance and emitting hemisphere.
    WorldRectLightResponse response = WorldRectLightResponse::Ltc;
};
inline constexpr std::size_t worldRectLightCapacity = 16;
using WorldRectLightUniforms = std::array<std::array<float, 4>, worldRectLightCapacity * 4>;

struct WorldWeatherShelter {
    // X/Z footprint; only surfaces below the roof remain sheltered.
    std::array<float, 4> bounds{};
    float roofHeight = 0;
};

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
    // Single scattering from rectangular sources in the existing height haze.
    // Zero preserves legacy views. Albedo is scattering/extinction, g is the
    // Henyey-Greenstein mean cosine (positive scatters forward).
    float localHazeAlbedo = 0;
    float localHazeAnisotropy = 0;
    bool localHazeDeferred = false;
    // Optional additional height haze, with a clear camera-near interval.
    float distantHazeStart = 0, distantHazeDensity = 0, distantHazeMaxOpacity = 0;
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
    // Two periodic sky layers. Offsets are UV displacement; density is optical
    // thickness and tint is scene-linear. Zero coverage preserves clear sky.
    float cloudCoverage = 0;
    float cloudDensity = 1;
    std::array<float, 4> cloudOffsets{};
    std::array<float, 3> cloudTint{1, 1, 1};
    float cloudLightResponse = 1;
    std::array<WorldPointLight, 4> pointLights{};
    // CPU precipitation inputs share the world snapshot without changing GPU uniforms.
    float rainIntensity = 0;
    double precipitationSeconds = 0;
    // Shared active-play clock for CPU environment animation; independent of
    // precipitation's bounded texture/particle phase and unchanged GPU uniforms.
    double animationSeconds = 0;
    std::array<float, 2> precipitationWind{};
    std::uint32_t precipitationSeed = 0;
    float surfaceWetness = 0;
    float puddleAmount = 0;
    // Explicit material opt-in prevents weather from changing character/UI rigs.
    bool surfaceWeatherEnabled = false;
    bool waterSurface = false;
    float surfaceAbsorption = 1;
    std::array<WorldWeatherShelter, 4> weatherShelters{};
    std::array<WorldRectLight, worldRectLightCapacity> rectLights{};
};

using WorldEnvironmentUniforms = std::array<std::array<float, 4>, 19>;

inline WorldEnvironmentUniforms packWorldEnvironment(const WorldEnvironmentLighting& e) {
    WorldEnvironmentUniforms out{};
    if (!e.enabled) return out;
    const std::array<float, 36> values{
        e.sunDirection[0], e.sunDirection[1], e.sunDirection[2], e.exposure,
        e.sunRadiance[0], e.sunRadiance[1], e.sunRadiance[2], e.hazeDensity,
        e.ambientRadiance[0], e.ambientRadiance[1], e.ambientRadiance[2], e.hazeBaseHeight,
        e.hazeRadiance[0], e.hazeRadiance[1], e.hazeRadiance[2], e.hazeHeightFalloff,
        e.nightSkyRadiance[0], e.nightSkyRadiance[1], e.nightSkyRadiance[2], e.skyIntensity,
        e.hazeMaxOpacity, e.distantHazeStart, e.distantHazeDensity, e.distantHazeMaxOpacity,
        e.moonDirection[0], e.moonDirection[1], e.moonDirection[2], e.moonAngularRadius,
        e.moonRadiance[0], e.moonRadiance[1], e.moonRadiance[2], e.moonPhase,
        e.sunAngularRadius, e.sunHaloStrength, e.sunHaloWidth, e.starIntensity};
    for (float v : values) if (!std::isfinite(v)) return out;
    const std::array cloudValues{e.cloudCoverage, e.cloudDensity,
        e.cloudOffsets[0], e.cloudOffsets[1], e.cloudOffsets[2], e.cloudOffsets[3],
        e.cloudTint[0], e.cloudTint[1], e.cloudTint[2], e.cloudLightResponse};
    for (float v : cloudValues) if (!std::isfinite(v)) return out;
    if (e.cloudDensity < 0 || e.cloudLightResponse < 0) return out;
    if(e.distantHazeStart<0 || e.distantHazeDensity<0 || e.distantHazeMaxOpacity<0) return out;
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
    out[5] = {night[0],night[1],night[2],std::min(e.distantHazeDensity,1.f)};
    out[6] = {e.moonDirection[0]/moonLength,e.moonDirection[1]/moonLength,e.moonDirection[2]/moonLength,std::clamp(e.moonAngularRadius,0.f,.04f)};
    out[7] = {moon[0],moon[1],moon[2],std::clamp(e.moonPhase,0.f,1.f)};
    out[8] = {std::clamp(e.sunAngularRadius,0.f,.04f),std::clamp(e.sunHaloStrength,0.f,1.f),std::clamp(e.sunHaloWidth,.001f,.15f),std::clamp(e.starIntensity,0.f,1.f)};
    if (e.cloudCoverage > 0 && e.cloudDensity > 0) {
        out[9] = {std::clamp(e.cloudCoverage,0.f,1.f),std::min(e.cloudDensity,4.f),0,0};
        for (std::size_t i = 0; i < 4; ++i)
            out[10][i] = e.cloudOffsets[i] - std::floor(e.cloudOffsets[i]);
        const auto tint = nonnegative(e.cloudTint);
        out[11] = {tint[0],tint[1],tint[2],std::min(e.cloudLightResponse,2.f)};
    }
    out[9][2]=std::min(e.distantHazeStart,100000.f);
    out[9][3]=std::clamp(e.distantHazeMaxOpacity,0.f,1.f);
    if (e.surfaceWeatherEnabled && std::isfinite(e.surfaceWetness) &&
        std::isfinite(e.puddleAmount) && std::isfinite(e.surfaceAbsorption) &&
        std::isfinite(e.rainIntensity) && std::isfinite(e.precipitationSeconds) &&
        e.precipitationSeconds >= 0 && e.precipitationSeconds <= 8192) {
        out[12] = {std::clamp(e.surfaceWetness,0.f,1.f),std::clamp(e.puddleAmount,0.f,1.f),
            std::clamp(e.rainIntensity,0.f,1.f),static_cast<float>(e.precipitationSeconds)};
        for (std::size_t i=0;i<e.weatherShelters.size();++i) {
            const auto& shelter=e.weatherShelters[i];
            bool valid=std::isfinite(shelter.roofHeight);
            for (float v : shelter.bounds) valid &= std::isfinite(v) && std::abs(v)<=1000000.f;
            if (valid && shelter.bounds[0]<shelter.bounds[2] && shelter.bounds[1]<shelter.bounds[3]) {
                out[13+i]=shelter.bounds;out[17][i]=shelter.roofHeight;
            }
        }
        out[18] = {std::clamp(e.surfaceAbsorption,0.f,1.f),e.waterSurface?1.f:0.f,0,0};
    }
    // Previously reserved channels; independent of surface-weather opt-in.
    if (std::isfinite(e.localHazeAlbedo) && std::isfinite(e.localHazeAnisotropy)) {
        out[18][2]=e.localHazeDeferred ? 0.f : std::clamp(e.localHazeAlbedo,0.f,1.f);
        out[18][3]=std::clamp(e.localHazeAnisotropy,-.8f,.8f);
    }
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

inline bool validWorldRectLight(const WorldRectLight& light) {
    if (light.response != WorldRectLightResponse::Ltc &&
        light.response != WorldRectLightResponse::FiniteLine) return false;
    for (const auto& v : {light.position, light.halfU, light.halfV, light.radiance})
        for (float x : v) if (!std::isfinite(x) || std::abs(x) > 1000000.f) return false;
    if (!std::isfinite(light.range) || light.range < 0 || light.range > 1000) return false;
    if (light.range == 0) return true;
    float u2 = 0, v2 = 0, uv = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        u2 += light.halfU[i] * light.halfU[i];
        v2 += light.halfV[i] * light.halfV[i];
        uv += light.halfU[i] * light.halfV[i];
        if (light.radiance[i] < 0 || light.radiance[i] > 65504) return false;
    }
    return u2 >= 1e-6f && v2 >= 1e-6f && u2 <= 10000 && v2 <= 10000 &&
        std::abs(uv) <= .001f * std::sqrt(u2 * v2);
}

inline WorldRectLightUniforms packWorldRectLights(const WorldEnvironmentLighting& e) {
    WorldRectLightUniforms out{};
    if (packWorldEnvironment(e)[0][3] == 0) return out;
    for (std::size_t i = 0; i < e.rectLights.size(); ++i) {
        const auto& l = e.rectLights[i];
        if (!validWorldRectLight(l)) return {};
        if (l.range == 0) continue;
        out[4*i] = {l.position[0], l.position[1], l.position[2], l.range};
        out[4*i+1] = {l.halfU[0], l.halfU[1], l.halfU[2], static_cast<float>(l.response)};
        out[4*i+2] = {l.halfV[0], l.halfV[1], l.halfV[2], 0};
        out[4*i+3] = {l.radiance[0], l.radiance[1], l.radiance[2], 0};
    }
    return out;
}

} // namespace engine::render
