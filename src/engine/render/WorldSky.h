#pragma once
#include <array>

namespace engine::render {
// Inputs are scene-owned. Radiance is scene-linear; display exposure is separate.
struct WorldSkySettings {
    float turbidity=4, indirectTurbidity=1;
    std::array<float,3> solarDiskColor{},lunarDiskColor{};
    float celestialColorWeight=0;
    bool indirect=false;
    std::array<float,3> groundReflectance{.1f,.1f,.1f};
    float horizonMinimum=.05f, minimumSunElevation=0, horizonFalloff=.05f;
    std::array<float,3> domeColor{100,100,100}, skylightColor{100,100,100};
    std::array<float,3> darkeningColor{1,1,1};
    float darkeningStart=0, darkeningEnd=1, darkeningExponent=1;
    float sunFalloff=.01f, moonFalloff=.01f, moonGlow=.05f, moonGlowFalloff=10;
    float latitude=36, longitude=0, cirrusOpacity=0, cirrusScale=5, cirrusAltitude=8000;
    float dimStars=1,brightStars=.1f,constellationStars=.05f,starCornerFix=.2f,galaxyIntensity=.02f;
    float screenSize=1080;
    float cirrusSunScatter=.9f, cirrusMoonScatter=.85f, cirrusAmbient=1, cirrusShadowFalloff=1;
};
struct WorldSkyModel {
    std::array<std::array<float,9>,3> coefficients{};
    std::array<float,3> radiance{};
};
using WorldSkyUniforms=std::array<std::array<float,4>,24>;
WorldSkyModel prepareSkyModel(float turbidity,const std::array<float,3>& reflectance,float sunElevation);
std::array<float,3> evaluateSkyModel(const WorldSkyModel&,float zenithCosine,float sunCosine);
void validateSkySettings(const WorldSkySettings&);
WorldSkyUniforms packSkyModel(const WorldSkySettings&,const std::array<float,3>& sunDirection,
    float timeOfDay,bool indirect=false);
std::array<float,3> averageSkyRadiance(const WorldSkyModel&,const std::array<float,3>& sunDirection,
    float horizonMinimum);
}
