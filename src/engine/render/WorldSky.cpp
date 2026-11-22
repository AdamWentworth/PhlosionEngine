#include "engine/render/WorldSky.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace engine::render {
namespace {
#include "../../../third_party/hosek/ArHosekSkyModelData_RGB.h"
constexpr double pi=3.14159265358979323846;
double evaluateFit(const double* values,unsigned stride,unsigned coefficient,double elevation) {
    const double x=std::cbrt(std::clamp(elevation/(pi*.5),0.,1.));
    const double y=1-x;
    const std::array<double,6> basis{y*y*y*y*y,5*y*y*y*y*x,10*y*y*y*x*x,
        10*y*y*x*x*x,5*y*x*x*x*x,x*x*x*x*x};
    double value=0;
    for(unsigned i=0;i<6;++i)value+=basis[i]*values[i*stride+coefficient];
    return value;
}
double cook(const double* table,unsigned stride,unsigned coefficient,double turbidity,double albedo,double elevation) {
    const double t=std::clamp(turbidity,1.,10.);
    const unsigned lower=unsigned(std::floor(t))-1,upper=std::min(lower+1,9u);
    const double w=t-std::floor(t);
    const auto at=[&](unsigned a,unsigned b){return evaluateFit(table+(a*10+b)*6*stride,stride,coefficient,elevation);};
    return (1-albedo)*((1-w)*at(0,lower)+w*at(0,upper))+albedo*((1-w)*at(1,lower)+w*at(1,upper));
}
}
WorldSkyModel prepareSkyModel(float turbidity,const std::array<float,3>& reflectance,float elevation) {
    if(!std::isfinite(turbidity)||!std::isfinite(elevation))throw std::invalid_argument("Nonfinite sky input");
    struct Cached {float turbidity=0,elevation=0;std::array<float,3> reflectance{};WorldSkyModel model;bool valid=false;};
    static thread_local Cached last;
    if(last.valid&&last.turbidity==turbidity&&last.elevation==elevation&&last.reflectance==reflectance)return last.model;
    WorldSkyModel model;
    for(unsigned c=0;c<3;++c) {
        if(!std::isfinite(reflectance[c])||reflectance[c]<0||reflectance[c]>1)throw std::invalid_argument("Sky ground reflectance");
        for(unsigned i=0;i<9;++i)model.coefficients[c][i]=float(cook(datasetsRGB[c],9,i,turbidity,reflectance[c],elevation));
        // Source scene-linear RGB convention; display/exposure is applied later.
        model.radiance[c]=float(cook(datasetsRGBRad[c],1,0,turbidity,reflectance[c],elevation)/256.);
    }
    last={turbidity,elevation,reflectance,model,true};return model;
}
std::array<float,3> evaluateSkyModel(const WorldSkyModel& model,float theta,float gamma) {
    if(!std::isfinite(theta)||!std::isfinite(gamma))throw std::invalid_argument("Nonfinite sky ray");
    const double t=std::clamp(double(theta),0.,1.),g=std::clamp(double(gamma),-1.,1.);
    std::array<float,3> result;
    for(unsigned c=0;c<3;++c) {
        const auto& a=model.coefficients[c];
        const double mie=(1+g*g)/std::pow(std::abs(1+double(a[8])*a[8]-2*a[8]*g),1.5);
        result[c]=float((1+a[0]*std::exp(a[1]/(t+.01)))*
            (a[2]+a[3]*std::exp(a[4]*std::acos(g))+a[5]*g*g+a[6]*mie+a[7]*std::sqrt(t))*model.radiance[c]);
    }
    return result;
}
void validateSkySettings(const WorldSkySettings& s) {
    for(auto v:{s.turbidity,s.indirectTurbidity,s.horizonMinimum,s.minimumSunElevation,s.horizonFalloff,
        s.darkeningStart,s.darkeningEnd,s.darkeningExponent,s.sunFalloff,s.moonFalloff,s.moonGlow,
        s.moonGlowFalloff,s.latitude,s.longitude,s.cirrusOpacity,s.cirrusScale,s.cirrusAltitude,
        s.celestialColorWeight,s.dimStars,s.brightStars,s.constellationStars,s.starCornerFix,s.galaxyIntensity,s.screenSize,s.cirrusSunScatter,s.cirrusMoonScatter,s.cirrusAmbient,s.cirrusShadowFalloff})
        if(!std::isfinite(v))throw std::invalid_argument("Nonfinite sky settings");
    for(const auto& a:{s.groundReflectance,s.domeColor,s.skylightColor,s.darkeningColor,s.solarDiskColor,s.lunarDiskColor})
        for(float v:a)if(!std::isfinite(v)||v<0||v>65504)throw std::invalid_argument("Invalid sky color");
    for(float v:s.groundReflectance)if(v>1)throw std::invalid_argument("Sky albedo exceeds one");
    if(s.celestialColorWeight<0||s.celestialColorWeight>1||s.horizonMinimum<0||s.horizonMinimum>1||s.horizonFalloff<=0||
       s.minimumSunElevation<0||s.minimumSunElevation>float(pi*.5)||
       s.sunFalloff<=0||s.moonFalloff<=0||s.moonGlow<0||s.moonGlowFalloff<=0||
       s.dimStars<=0||s.brightStars<=0||s.constellationStars<=0||s.starCornerFix<0||s.starCornerFix>1||s.galaxyIntensity<0||s.screenSize<1||s.cirrusOpacity<0||s.cirrusScale<=0||s.cirrusAltitude<=0||s.cirrusAmbient<0||
       std::abs(s.cirrusSunScatter)>=1||std::abs(s.cirrusMoonScatter)>=1||s.cirrusShadowFalloff<0)
        throw std::invalid_argument("Invalid sky settings");
}
WorldSkyUniforms packSkyModel(const WorldSkySettings& s,const std::array<float,3>& sun,float time,bool indirect) {
    validateSkySettings(s);
    const float length=std::hypot(sun[0],sun[1],sun[2]);
    if(!std::isfinite(length)||length<1e-5f||!std::isfinite(time))throw std::invalid_argument("Invalid sky direction/time");
    const auto m=prepareSkyModel(indirect?s.indirectTurbidity:s.turbidity,s.groundReflectance,
        std::max(std::asin(std::clamp(sun[1]/length,-1.f,1.f)),s.minimumSunElevation));
    WorldSkyUniforms out{};
    for(unsigned c=0;c<3;++c)for(unsigned i=0;i<9;++i)out[c*3+i/4][i%4]=m.coefficients[c][i];
    out[9]={m.radiance[0],m.radiance[1],m.radiance[2],s.horizonMinimum};
    const auto& color=indirect?s.skylightColor:s.domeColor;
    out[10]={color[0],color[1],color[2],1};
    out[11]={s.skylightColor[0],s.skylightColor[1],s.skylightColor[2],s.horizonFalloff};
    out[12]={s.darkeningColor[0],s.darkeningColor[1],s.darkeningColor[2],s.darkeningExponent};
    out[13]={s.darkeningStart,s.darkeningEnd,time/24.f,s.latitude};
    out[14]={s.sunFalloff,s.moonFalloff,s.moonGlow,s.moonGlowFalloff};
    out[15]={s.longitude,s.cirrusOpacity,s.cirrusScale,s.cirrusAltitude};
    out[16]={s.cirrusSunScatter,s.cirrusMoonScatter,s.cirrusAmbient,s.cirrusShadowFalloff};
    out[17]={s.dimStars,s.brightStars,s.constellationStars,s.starCornerFix};
    out[18]={s.galaxyIntensity,s.screenSize,indirect?1.f:0.f,0};
    out[19]={s.solarDiskColor[0],s.solarDiskColor[1],s.solarDiskColor[2],s.celestialColorWeight};
    out[20]={s.lunarDiskColor[0],s.lunarDiskColor[1],s.lunarDiskColor[2],s.celestialColorWeight};
    return out;
}
std::array<float,3> averageSkyRadiance(const WorldSkyModel& model,const std::array<float,3>& sun,float horizon) {
    const float length=std::hypot(sun[0],sun[1],sun[2]);
    if(!std::isfinite(length)||length<1e-5f||!std::isfinite(horizon)||horizon<0||horizon>1)
        throw std::invalid_argument("Invalid sky integration");
    struct Cached {WorldSkyModel model;std::array<float,3> sun{},result{};float horizon=0;bool valid=false;};
    static thread_local Cached last;
    if(last.valid&&last.model.coefficients==model.coefficients&&last.model.radiance==model.radiance&&last.sun==sun&&last.horizon==horizon)return last.result;
    std::array<double,3> sum{};double weights=0;
    // Paraboloid samples and cosine weighting match the CPU skylight producer.
    constexpr unsigned size=16;
    for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x) {
        const double u=2.*x/(size-1)-1,v=2.*y/(size-1)-1,r2=u*u+v*v;
        if(r2>std::pow(1+1.5/size,2))continue;
        double dx=2*u/(1+r2),dy=std::max((1-r2)/(1+r2),.01),dz=2*v/(1+r2);
        const double n=std::sqrt(dx*dx+dy*dy+dz*dz);dx/=n;dy/=n;dz/=n;
        const auto c=evaluateSkyModel(model,std::max(float(dy),horizon),float((dx*sun[0]+dy*sun[1]+dz*sun[2])/length));
        for(unsigned i=0;i<3;++i)sum[i]+=c[i]*dy;
        weights+=dy;
    }
    const std::array<float,3> result{float(sum[0]/weights),float(sum[1]/weights),float(sum[2]/weights)};
    last={model,sun,result,horizon,true};return result;
}
}
