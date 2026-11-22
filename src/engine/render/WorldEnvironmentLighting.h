#pragma once
#include "engine/render/WorldSky.h"
#include "engine/render/WorldWindController.h"
#include "engine/render/WorldClouds.h"
#include "engine/render/WorldPuddle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace engine::render {

// Independent authored transport participation. Receiver channels do not replace
// diffuse/specular or consumer flags; the shader tests both.
enum WorldLightParticipation : std::uint32_t {
    WorldLightDiffuse=1,WorldLightSpecular=2,WorldLightSurfaces=4,
    WorldLightProbes=8,WorldLightFog=16,WorldLightParticles=32,
    WorldLightShadows=64,WorldLightTransparent=128,WorldLightAll=255
};
// Native local sources use candela after producer-side flux/colour conversion.
// Axis is the spot direction, or the capsule's unit longitudinal axis.
// The rectangular luminous face remains separate for visibility and transport.
// Capsule fog uses this axis for its length and the luminous face for its cone.
struct WorldLocalLightSource {
    std::array<float,3> axis{1,0,0};
    float radius=0, length=0;
    float roughnessBias=1, attenuationCap=0;
    float innerAngle=0, outerAngle=180, softness=1;
    float intensityScale=1;
    bool native=true, inverseSquare=true, cone=false, directional=false;
};
inline bool validWorldLocalLightSource(const WorldLocalLightSource& s) {
    for(float v:{s.axis[0],s.axis[1],s.axis[2],s.radius,s.length,s.roughnessBias,
                 s.attenuationCap,s.innerAngle,s.outerAngle,s.softness,s.intensityScale})
        if(!std::isfinite(v))return false;
    const float norm=std::hypot(s.axis[0],s.axis[1],s.axis[2]);
    return std::abs(norm-1.f)<.001f && s.radius>=0 && s.radius<=100 &&
        s.length>=0 && s.length<=200 && s.roughnessBias>=0 && s.roughnessBias<=4 &&
        s.attenuationCap>=0 && s.attenuationCap<=10000 &&
        s.innerAngle>=0 && s.outerAngle>s.innerAngle && s.outerAngle<=360 &&
        s.softness>0 && s.softness<=32 && s.intensityScale>=0 && s.intensityScale<=65504;
}
inline std::array<std::array<float,4>,3> packWorldLocalLightSource(const WorldLocalLightSource& s) {
    if(!validWorldLocalLightSource(s))throw std::invalid_argument("Invalid finite light source");
    const unsigned flags=(s.native?1u:0u)|(s.inverseSquare?2u:0u)|(s.cone?4u:0u)|(s.directional?8u:0u);
    constexpr float halfRadians=3.14159265358979323846f/360.f;
    return {{{s.axis[0],s.axis[1],s.axis[2],float(flags)},
             {s.radius,s.length,s.roughnessBias,s.attenuationCap},
             {std::cos(s.innerAngle*halfRadians),s.outerAngle==180.f?0.f:std::cos(s.outerAngle*halfRadians),s.softness,s.intensityScale}}};
}
enum class WorldShadowSoftness : std::uint32_t { ExtraSharp, Sharp, Default, Soft, ExtraSoft };
struct WorldLocalShadowSettings {
    float angle=150.f, fadeDistance=60.f, fadeRange=10.f;
    float radius=0; // Zero uses the light range; this restricts caster queries, not receiver normalization.
    WorldShadowSoftness softness=WorldShadowSoftness::Default;
    bool staticOnly=false;
};
inline bool validWorldLocalShadowSettings(const WorldLocalShadowSettings& s) {
    return std::isfinite(s.angle)&&s.angle>0&&s.angle<173.f&&
        std::isfinite(s.fadeDistance)&&s.fadeDistance>=0&&s.fadeDistance<=100000&&
        std::isfinite(s.fadeRange)&&s.fadeRange>0&&s.fadeRange<=100000&&
        std::isfinite(s.radius)&&s.radius>=0&&s.radius<=1000&&
        unsigned(s.softness)<=unsigned(WorldShadowSoftness::ExtraSoft);
}
inline unsigned worldShadowKernel(WorldShadowSoftness softness) {
    constexpr std::array<unsigned,5> kernels{2,2,5,10,15};
    return kernels.at(unsigned(softness));
}
inline float worldShadowOpacity(const WorldLocalShadowSettings& s,
        const std::array<float,3>& source,const std::array<float,3>& camera) {
    if(!validWorldLocalShadowSettings(s))throw std::invalid_argument("Invalid local shadow settings");
    float distance2=0;
    for(unsigned i=0;i<3;++i){
        if(!std::isfinite(camera[i])||!std::isfinite(source[i]))throw std::invalid_argument("Invalid shadow camera");
        const float d=camera[i]-source[i];distance2+=d*d;
    }
    return std::clamp((s.fadeDistance+s.fadeRange-std::sqrt(distance2))/s.fadeRange,0.f,1.f);
}

struct WorldPointLight {
    std::array<float, 3> position{};
    float radius = 0;
    std::array<float, 3> intensity{};
    std::uint32_t channels=255,participation=WorldLightAll;
    WorldLocalLightSource source{};
    WorldLocalShadowSettings shadow{};
    std::uint64_t identity=0;
};

using WorldPointLightUniforms = std::array<std::array<float, 4>, 20>;

enum class WorldRectLightResponse : std::uint32_t { Ltc = 0, FiniteLine = 1, FiniteSource = 2 };
enum class WorldLocalShadowFilter : std::uint32_t { Pcf = 0, DistanceMoments = 1 };

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
    WorldLocalShadowFilter shadowFilter = WorldLocalShadowFilter::Pcf;
    std::uint32_t channels=255,participation=WorldLightAll;
    std::uint64_t identity=0; // zero retains legacy array-index identity
    WorldLocalLightSource source{};
    WorldLocalShadowSettings shadow{};
    bool affectsDiffuseProbes=true; // diffuse transport participation, independent of specular probe capture
};
inline constexpr std::size_t worldRectLightCapacity = 16;
inline constexpr std::size_t worldLocalShadowSourceCapacity = worldRectLightCapacity + 4;
using WorldRectLightUniforms = std::array<std::array<float, 4>, worldRectLightCapacity * 7>;

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
    std::uint32_t lightChannels=255;
    std::uint32_t localShadowMapResolution=1024; // 0 disables; 512/1024 supported
    bool skyDraw = false;
    std::array<float, 3> sunDirection{0, 1, 0};
    std::array<float, 3> sunRadiance{0, 0, 0};
    std::array<float, 3> globalSpecularTint{1, 1, 1}; // directional specular only
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
    bool volumetricFogDeferred = false; // scene fog replaces the analytic near-height term
    // Optional additional height haze, with a clear camera-near interval.
    float distantHazeStart = 0, distantHazeDensity = 0, distantHazeMaxOpacity = 0;
    float skyIntensity = 1;
    WorldSkySettings sky;
    WorldCloudSettings clouds;
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
    float blowingDustWeight = 0; // Simulation-owned dry-weather override; independent of rain.
    double dustEffectSeconds=0;
    std::uint64_t dustGeneration=0;
    bool dustEffectActive=false;
    double precipitationSeconds = 0;
    // Shared active-play clock for CPU environment animation; independent of
    // precipitation's bounded texture/particle phase and unchanged GPU uniforms.
    double animationSeconds = 0;
    WorldWindSnapshot wind{};
    bool sharedWind=false;
    std::array<float, 2> precipitationWind{};
    std::array<double, 2> precipitationWindOffset{};
    bool precipitationWindIntegrated=false;
    float timeOfDayHours=12;
    std::uint32_t precipitationSeed = 0;
    float surfaceWetness = 0;
    float puddleAmount = 0;
    // Explicit material opt-in prevents weather from changing character/UI rigs.
    bool surfaceWeatherEnabled = false;
    bool waterSurface = false;
    float surfaceAbsorption = 1;
    // Optional per-draw reflection shaping: normal/grazing scale, bias and
    // extra grazing response. Weight zero preserves the neutral BRDF exactly.
    std::array<float, 4> reflectionResponse{1, 1, 0, 0};
    float reflectionResponseWeight = 0;
    bool reflectionAlbedoF90 = false;
    std::array<WorldWeatherShelter, 4> weatherShelters{};
    std::array<WorldPuddleRegion,worldPuddleCapacity> puddles{};
    std::array<WorldRectLight, worldRectLightCapacity> rectLights{};
};

inline WorldPuddleUniforms packWorldPuddles(const WorldEnvironmentLighting& e) {
    WorldPuddleUniforms out{};
    if(!e.enabled||!e.surfaceWeatherEnabled||!std::isfinite(e.surfaceWetness)||
       e.surfaceWetness<=0||worldPuddleFill(e.puddleAmount)<=0)return out;
    for(std::size_t i=0;i<e.puddles.size();++i) {
        const auto& p=e.puddles[i];if(!validWorldPuddle(p))continue;
        out[2*i]={p.center[0],p.center[2],p.center[1],1};
        out[2*i+1]={1/p.extent[0],1/p.extent[1],p.rotation[0],p.rotation[1]};
    }
    return out;
}

inline float packWorldLightParticipation(std::uint32_t channels,std::uint32_t participation,std::uint32_t receiver) {
    if(channels>255 || participation>255 || receiver>255)throw std::invalid_argument("Invalid light channel/participation mask");
    return float(channels | (participation<<8) | (receiver<<16));
}
using WorldEnvironmentUniforms = std::array<std::array<float, 4>, 19>;
using WorldReflectionResponseUniforms = std::array<std::array<float, 4>, 2>;

inline WorldReflectionResponseUniforms packWorldReflectionResponse(const WorldEnvironmentLighting& e) {
    WorldReflectionResponseUniforms out{};
    if (!e.enabled || !std::isfinite(e.reflectionResponseWeight) || e.reflectionResponseWeight <= 0) return out;
    for (float value : e.reflectionResponse) if (!std::isfinite(value) || value < 0) return out;
    for (std::size_t i=0;i<4;++i) out[0][i]=std::min(e.reflectionResponse[i],4.f);
    out[1]={std::min(e.reflectionResponseWeight,1.f),e.reflectionAlbedoF90?1.f:0.f,0,0};
    return out;
}

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
    // Negative packed density marks scene volumetrics; retain the physical density in the CPU snapshot.
    out[3] = {haze[0],haze[1],haze[2],e.volumetricFogDeferred?-std::max(std::min(e.hazeDensity,1.f),1e-8f):std::min(e.hazeDensity,1.f)};
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

inline WorldPointLightUniforms packWorldPointLights(const WorldEnvironmentLighting& e,
    const WorldEnvironmentUniforms& environment) {
    WorldPointLightUniforms out{};
    if (environment[0][3] == 0) return out;
    for (std::size_t i = 0; i < e.pointLights.size(); ++i) {
        const auto& light = e.pointLights[i];
        const std::array values{light.position[0], light.position[1], light.position[2],
            light.radius, light.intensity[0], light.intensity[1], light.intensity[2]};
        for (float value : values) if (!std::isfinite(value)) return {};
        if (light.radius < 0 || !validWorldLocalLightSource(light.source) || !validWorldLocalShadowSettings(light.shadow)) return {};
        out[i*2+1][3]=packWorldLightParticipation(light.channels,light.participation,i==0?e.lightChannels:0);
        if (light.radius == 0) continue;
        const auto source=packWorldLocalLightSource(light.source);
        std::copy(source.begin(),source.end(),out.begin()+8+i*3);
        out[i*2] = {light.position[0], light.position[1], light.position[2],
            std::min(light.radius, 1000.f)};
        out[i*2+1] = {std::clamp(light.intensity[0], 0.f, 65504.f),
            std::clamp(light.intensity[1], 0.f, 65504.f),
            std::clamp(light.intensity[2], 0.f, 65504.f),out[i*2+1][3]};
    }
    return out;
}

inline WorldPointLightUniforms packWorldPointLights(const WorldEnvironmentLighting& e) {
    return packWorldPointLights(e, packWorldEnvironment(e));
}

inline bool validWorldRectLight(const WorldRectLight& light) {
    if(light.channels>255 || light.participation>255 || !validWorldLocalShadowSettings(light.shadow))return false;
    if (light.shadowFilter != WorldLocalShadowFilter::Pcf &&
        light.shadowFilter != WorldLocalShadowFilter::DistanceMoments) return false;
    if (light.response != WorldRectLightResponse::Ltc &&
        light.response != WorldRectLightResponse::FiniteLine &&
        light.response != WorldRectLightResponse::FiniteSource) return false;
    if(light.response==WorldRectLightResponse::FiniteSource && !validWorldLocalLightSource(light.source))return false;
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

inline WorldRectLightUniforms packWorldRectLights(const WorldEnvironmentLighting& e,
    const WorldEnvironmentUniforms& environment) {
    WorldRectLightUniforms out{};
    if (environment[0][3] == 0) return out;
    for (std::size_t i = 0; i < e.rectLights.size(); ++i) {
        const auto& l = e.rectLights[i];
        if (!validWorldRectLight(l)) return {};
        if (l.range == 0) continue;
        if(l.response==WorldRectLightResponse::FiniteSource) {
            const auto source=packWorldLocalLightSource(l.source);
            std::copy(source.begin(),source.end(),out.begin()+64+i*3);
        }
        out[4*i] = {l.position[0], l.position[1], l.position[2], l.range};
        out[4*i+1] = {l.halfU[0], l.halfU[1], l.halfU[2], static_cast<float>(l.response)};
        out[4*i+2] = {l.halfV[0], l.halfV[1], l.halfV[2], 0};
        out[4*i+3] = {l.radiance[0], l.radiance[1], l.radiance[2],packWorldLightParticipation(l.channels,l.participation,0)};
    }
    return out;
}

inline WorldRectLightUniforms packWorldRectLights(const WorldEnvironmentLighting& e) {
    return packWorldRectLights(e, packWorldEnvironment(e));
}

} // namespace engine::render
