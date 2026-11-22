#include "engine/render/WorldSky.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "data/sky_oracle.h"
using namespace engine::render;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main() {
    double worst=0;
    for(const auto& v:skyOracle) {
        auto m=prepareSkyModel(float(v[0]),{float(v[1]),float(v[1]),float(v[1])},float(v[2]));
        auto color=evaluateSkyModel(m,float(v[3]),float(v[4]));
        for(unsigned c=0;c<3;++c) {
            const double error=std::abs(color[c]-v[5+c])/std::max(std::abs(v[5+c]),1e-5);
            worst=std::max(worst,error);
            check(error<.001,"Public independent sky radiance mismatch");
        }
    }
    const auto m=prepareSkyModel(4,{.1f,.2f,.3f},.3f);
    for(unsigned c=0;c<3;++c) {
        const auto equal=prepareSkyModel(4,{.1f+.1f*c,.1f+.1f*c,.1f+.1f*c},.3f);
        check(m.coefficients[c]==equal.coefficients[c],"Ground reflectance channels crossed");
    }
    WorldSkySettings s;auto p=packSkyModel(s,{1,-1,0},23);
    auto frozen=packSkyModel(s,{1,0,0},23);check(p==frozen,"Night elevation was not clamped");
    auto q=packSkyModel(s,{1,1,0},12,true);check(q!=packSkyModel(s,{1,1,0},12),"Indirect turbidity ignored");
    auto avg=averageSkyRadiance(m,{1,1,0},.05f);
    for(auto c:avg)check(std::isfinite(c)&&c>0,"Invalid integrated skylight");
    s.solarDiskColor={7,3,1};s.lunarDiskColor={.2f,.3f,.5f};s.celestialColorWeight=.7f;
    const auto split=packSkyModel(s,{1,1,0},12);
    check(split[19]==std::array<float,4>{7,3,1,.7f}&&
          split[20]==std::array<float,4>{.2f,.3f,.5f,.7f},"Independent celestial colors lost");
    check(split[17]==std::array<float,4>{s.dimStars,s.brightStars,s.constellationStars,s.starCornerFix},
          "Celestial colors shifted star parameters");
    s.cirrusShadowFalloff=0;validateSkySettings(s);
    auto invalid=s;invalid.celestialColorWeight=1.01f;
    bool invalidWeight=false;try{validateSkySettings(invalid);}catch(const std::invalid_argument&){invalidWeight=true;}
    check(invalidWeight,"Invalid celestial blend accepted");
    s.turbidity=std::numeric_limits<float>::quiet_NaN();
    bool rejected=false;try{packSkyModel(s,{0,1,0},12);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Nonfinite sky accepted");
    std::cout<<"PASS 320 public sky references; maximum relative error="<<worst<<"\n";
}
