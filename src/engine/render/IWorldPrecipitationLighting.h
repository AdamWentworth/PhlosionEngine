#pragma once
#include "engine/render/WorldEnvironmentLighting.h"
#include "engine/render/WorldFogMedium.h"
#include "engine/render/WorldDistantFog.h"
#include "engine/render/WorldDiffuseProbes.h"
#include "engine/render/WorldTemporalScene.h"
#include "engine/render/IWorldResourceScope.h"
#include "engine/render/IWorldDirectionalShadow.h"
#include <string>
namespace engine::render {
inline constexpr std::uint64_t worldPrecipitationLightingByteLimit=256ull*1024*1024;
struct WorldPrecipitationLightingView {
    WorldTemporalView view;
    WorldEnvironmentLighting environment;
    WorldResourceScopeId owner=0;
    unsigned quality=1;
    float range=128,depthExponent=2,absorption=1;
    bool distant=false;
    // The primary surface producer already owns opaque depth for this exact view.
    bool usePreparedSurfaceDepth=false;
    WorldFogMedium medium;
    WorldDistantFog distantFog;
};
struct WorldPrecipitationLightingResult {
    std::string texture;
    std::array<float,4> dimensions{}; // froxel width, height, depth, tiles across
    std::array<float,4> depth{}; // range, inverse exponent, source time of day, enabled
};
struct WorldPrecipitationLightingDiagnostic {
    unsigned width=0,height=0,depth=0;
    std::uint64_t bytes=0,identity=0,frame=0;
    bool history=false;
    std::vector<std::array<float,4>> scattering,lighting;
};
using WorldFogUniforms=std::array<std::array<float,4>,13>;
class IWorldPrecipitationLighting {
public:
    virtual ~IWorldPrecipitationLighting()=default;
    virtual WorldPrecipitationLightingResult updateWorldPrecipitationLighting(const WorldPrecipitationLightingView&)=0;
    virtual void bindWorldFog(const WorldPrecipitationLightingView*,const WorldPrecipitationLightingResult* = nullptr,const WorldPrecipitationLightingResult* = nullptr)=0;
    virtual bool captureWorldPrecipitationLighting()=0;
    virtual WorldPrecipitationLightingDiagnostic readWorldPrecipitationLighting()=0;
};
using PrecipitationRows=std::array<std::array<float,4>,672>;
inline std::array<unsigned,3> precipitationDimensions(int width,int height,unsigned quality) {
    if(width<=0||height<=0||width>8192||height>8192||quality>3)throw std::invalid_argument("Precipitation view dimensions");
    constexpr unsigned tiles[]{24,16,12,8},slices[]{32,64,80,96};
    const auto aligned=[&](unsigned n){return ((n+tiles[quality]-1)/tiles[quality]+3)&~3u;};
    return {aligned(width),aligned(height),slices[quality]};
}
inline std::array<unsigned,3> precipitationDimensions(const WorldPrecipitationLightingView& u) {
    auto d=precipitationDimensions(u.view.width,u.view.height,u.quality);
    if(u.distant)for(auto& v:d)v/=2;
    return d;
}
inline std::array<float,3> precipitationJitter(std::uint64_t frame) {
    constexpr double g=1.22074408460575947536;
    const auto phase=[&](double a){return float(std::fmod(.5+a*double(frame%1048576),1.));};
    return {phase(1/g)-.5f,phase(1/(g*g))-.5f,phase(1/(g*g*g))};
}
inline PrecipitationRows packPrecipitationLighting(const WorldPrecipitationLightingView& u,
    const WorldPrecipitationLightingView& previous,bool history,
    const WorldDiffuseProbeUniforms& indirect,const WorldDirectionalShadowView& shadow,
    const std::array<float,4>& shadowAtlas) {
    const auto dims=precipitationDimensions(u);
    if(!u.owner||!u.view.identity||!u.view.cameraValid||!std::isfinite(u.range)||u.range<32||u.range>256||
       !std::isfinite(u.depthExponent)||u.depthExponent<1||u.depthExponent>8||
       !std::isfinite(u.absorption)||u.absorption<=0||u.absorption>32)throw std::invalid_argument("Precipitation lighting inputs");
    validateFogMedium(u.medium);
    const auto e=packWorldEnvironment(u.environment);PrecipitationRows p{};
    p[0]={float(dims[0]),float(dims[1]),float(dims[2]),8};
    p[1]={float(u.view.width),float(u.view.height),u.range*(u.distant?20.f:1.f),u.depthExponent};
    p[26]={u.distant?u.range:0.f,previous.distant?previous.range:0.f,u.distant?1.f:0.f,0};
    const auto jitter=precipitationJitter(u.view.frame);p[2]={jitter[0],jitter[1],jitter[2],history?1.f:0.f};
    for(auto x:u.view.viewProjection)if(!std::isfinite(x))throw std::invalid_argument("Precipitation camera matrix");
    const auto matrix=glm::make_mat4(u.view.viewProjection.data());
    if(std::abs(glm::determinant(matrix))<1e-12f)throw std::invalid_argument("Precipitation singular camera");
    const auto inverse=glm::inverse(matrix);
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)p[4+r][c]=inverse[c][r];
    const auto old=glm::make_mat4(previous.view.viewProjection.data());
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)p[8+r][c]=old[c][r];
    const auto forward=[&](const auto& inv,const auto& camera) {
        const auto h=inv*glm::vec4(0,0,1,1);return glm::normalize(glm::vec3(h)/h.w-glm::make_vec3(camera.data()));
    };
    auto f=forward(inverse,u.view.cameraPosition);
    p[3]={u.view.cameraPosition[0],u.view.cameraPosition[1],u.view.cameraPosition[2],0};
    p[12]={f.x,f.y,f.z,1/(u.range*(u.distant?20.f:1.f))};
    p[13]={previous.view.cameraPosition[0],previous.view.cameraPosition[1],previous.view.cameraPosition[2],1/(previous.range*(previous.distant?20.f:1.f))};
    f=forward(glm::inverse(old),previous.view.cameraPosition);p[14]={f.x,f.y,f.z,1/previous.depthExponent};
    p[15]={std::max(u.environment.hazeDensity,0.f),u.environment.hazeBaseHeight,
        std::max(u.environment.hazeHeightFalloff,0.f),u.absorption};
    p[16]={std::clamp(u.environment.localHazeAlbedo,0.f,1.f),std::clamp(u.environment.localHazeAnisotropy,-.95f,.95f),1,float(u.environment.lightChannels)};
    const bool moon=std::max({e[7][0],e[7][1],e[7][2]})>std::max({e[1][0],e[1][1],e[1][2]});
    p[17]=e[moon?6:0];p[17][3]=u.environment.enabled?1.f:0.f;p[18]=e[moon?7:1];p[19]=e[2];
    std::copy_n(indirect.begin(),5,p.begin()+20);
    unsigned n=0;
    const auto add=[&](const auto& pos,float range,const auto& color,const WorldLocalLightSource& source,
                       std::array<float,3> normal,bool hemisphere,unsigned channels) {
        if(range<=0)return;
        if(!validWorldLocalLightSource(source))throw std::invalid_argument("Precipitation light source");
        const auto rows=packWorldLocalLightSource(source);const unsigned at=32+n++*6;
        p[at]={pos[0],pos[1],pos[2],range};
        p[at+1]={color[0],color[1],color[2],float(channels)};
        p[at+2]=rows[0];p[at+3]=rows[1];p[at+4]=rows[2];
        p[at+5]={normal[0],normal[1],normal[2],hemisphere?1.f:0.f};
    };
    const auto admitted=[&](auto& l){return !u.distant&&(l.participation&WorldLightFog);};
    for(const auto& l:u.environment.pointLights)if(admitted(l)) {
        add(l.position,l.radius,l.intensity,l.source,{},false,l.channels);
    }
    for(const auto& l:u.environment.rectLights)if(admitted(l)) {
        const auto cross=glm::cross(glm::make_vec3(l.halfU.data()),glm::make_vec3(l.halfV.data()));
        if(glm::length(cross)<1e-8f)continue;
        const auto normal=glm::normalize(cross);auto c=l.radiance;
        if(l.response!=WorldRectLightResponse::FiniteSource)for(auto& x:c)x*=4*glm::length(cross);
        // The source excludes tubes (and point/sphere sources) from the area
        // emitter's one-sided plane test. FiniteSource is that native source.
        add(l.position,l.range,c,l.source,{normal.x,normal.y,normal.z},
            l.response!=WorldRectLightResponse::FiniteSource,l.channels);
    }
    p[25]={float(n),float(std::array<unsigned,4>{2,4,6,8}[u.quality]),0,0};
    const auto sh=packWorldDirectionalShadow(shadow);
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)p[160+r][c]=sh.viewProjection[c*4+r];
    p[164]=sh.parameters;p[165]=shadowAtlas;
    const auto& m=u.medium;
    p[166]={m.globalLightScale,m.localLightScale,m.ambientScale,m.localAmbientScale};
    p[167]={m.phaseScale,m.phaseBase,m.heightMaxCut,m.distantGlobalLightScale};
    p[168]={m.albedoTint[0],m.albedoTint[1],m.albedoTint[2],m.shadowAmbientDarkening};
    p[169]={m.distantAlbedoTint[0],m.distantAlbedoTint[1],m.distantAlbedoTint[2],m.distantSkyScale};
    p[170]={m.distantGroundIrradiance[0],m.distantGroundIrradiance[1],m.distantGroundIrradiance[2],m.distantGroundScale};
    p[171]={m.groundElevation,m.groundHeightVariation,m.distantGroundSaturation,0};
    if(!u.distant) {
        const auto camera=glm::make_vec3(u.view.cameraPosition.data());
        const auto distance=[&](const auto* value){return glm::dot(glm::make_vec3(value->position.data())-camera,glm::make_vec3(value->position.data())-camera);};
        std::vector<const WorldFogBox*> boxes;
        for(const auto& b:m.boxes) {
            const auto extent=glm::abs(glm::make_vec3(b.halfRight.data()))+glm::abs(glm::make_vec3(b.halfUp.data()))+glm::abs(glm::make_vec3(b.halfForward.data()));
            if(fogBoundsVisible(glm::make_vec3(b.position.data()),extent,matrix))boxes.push_back(&b);
        }
        std::stable_sort(boxes.begin(),boxes.end(),[&](const auto* a,const auto* b){return a->priority!=b->priority?a->priority>b->priority:distance(a)<distance(b);});
        if(boxes.size()>32)boxes.resize(32);
        p[172][0]=float(boxes.size());
        for(unsigned i=0;i<boxes.size();++i) {
            const auto& b=*boxes[i];auto at=176+i*7;
            p[at]={b.position[0],b.position[1],b.position[2],b.density};
            p[at+1]={b.halfRight[0],b.halfRight[1],b.halfRight[2],b.absorption};
            p[at+2]={b.halfUp[0],b.halfUp[1],b.halfUp[2],b.blendRange};
            p[at+3]={b.halfForward[0],b.halfForward[1],b.halfForward[2],b.densityFalloff};
            p[at+4]={b.albedo[0],b.albedo[1],b.albedo[2],b.ambientScale};
            p[at+5][0]=float(b.channels);
        }
        std::vector<const WorldFogParticle*> particles;
        for(const auto& v:m.particles)
            if(fogBoundsVisible(glm::make_vec3(v.position.data()),glm::vec3(v.radius),matrix))particles.push_back(&v);
        std::stable_sort(particles.begin(),particles.end(),[&](const auto* a,const auto* b){return distance(a)<distance(b);});
        if(particles.size()>64)particles.resize(64);
        p[172][1]=float(particles.size());
        for(unsigned i=0;i<particles.size();++i) {
            const auto& v=*particles[i];auto at=400+i*3;
            p[at]={v.position[0],v.position[1],v.position[2],v.radius};
            p[at+1]={v.albedo[0],v.albedo[1],v.albedo[2],v.density};
            p[at+2][0]=v.falloff;
        }
    }
    for(const auto& row:p)for(float x:row)if(!std::isfinite(x))throw std::invalid_argument("Nonfinite precipitation parameters");
    return p;
}
inline bool fogBindingViewMatches(const WorldPrecipitationLightingView& a,const WorldPrecipitationLightingView& b) {
    return a.owner==b.owner && a.view.identity==b.view.identity && a.view.generation==b.view.generation &&
        a.view.frame==b.view.frame && a.view.width==b.view.width && a.view.height==b.view.height &&
        a.view.viewProjection==b.view.viewProjection && a.view.cameraPosition==b.view.cameraPosition &&
        a.quality==b.quality && a.range==b.range && a.depthExponent==b.depthExponent;
}
inline WorldFogUniforms packWorldFog(const WorldPrecipitationLightingView& u,
    const WorldPrecipitationLightingResult& nearVolume,const WorldPrecipitationLightingResult& distantVolume) {
    WorldFogUniforms p{};p[0]=nearVolume.dimensions;p[1]=distantVolume.dimensions;p[2]={u.range,1/u.depthExponent,1,0};
    p[3]={u.view.cameraPosition[0],u.view.cameraPosition[1],u.view.cameraPosition[2],0};
    const auto matrix=glm::make_mat4(u.view.viewProjection.data()),inverse=glm::inverse(matrix);
    auto h=inverse*glm::vec4(0,0,1,1);auto f=glm::normalize(glm::vec3(h)/h.w-glm::make_vec3(u.view.cameraPosition.data()));
    p[4]={f.x,f.y,f.z,0};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)p[5+r][c]=matrix[c][r];
    const auto luma=[](const auto& v){return v[0]*.2126f+v[1]*.7152f+v[2]*.0722f;};
    const auto& sky=u.environment.sky;
    const float scale=std::clamp(luma(sky.domeColor)/std::max(luma(sky.skylightColor),1e-4f),.1f,4.f);
    const auto distant=packDistantFog(u.distantFog,scale);
    std::copy(distant.begin(),distant.end(),p.begin()+9);
    return p;
}
inline bool precipitationHistoryValid(const WorldPrecipitationLightingView& u,const WorldPrecipitationLightingView& p) {
    if(u.view.reset||u.owner!=p.owner||u.view.identity!=p.view.identity||u.view.generation!=p.view.generation||
       u.view.frame!=p.view.frame+1||u.view.width!=p.view.width||u.view.height!=p.view.height||
       u.distant!=p.distant||u.medium.revision!=p.medium.revision||u.absorption!=p.absorption||u.quality!=p.quality||u.range!=p.range||u.depthExponent!=p.depthExponent)return false;

    const auto jump=[](float a,float b){return std::abs(a-b)>std::max(.0001f,std::max(std::abs(a),std::abs(b))*.25f);};
    if(u.environment.enabled!=p.environment.enabled ||
       jump(u.environment.hazeDensity,p.environment.hazeDensity) ||
       std::abs(u.environment.localHazeAlbedo-p.environment.localHazeAlbedo)>.1f ||
       std::abs(u.environment.localHazeAnisotropy-p.environment.localHazeAnisotropy)>.1f)return false;
    float day=std::abs(u.environment.timeOfDayHours-p.environment.timeOfDayHours);
    day=std::min(day,24-day);
    if(day>.25f||glm::distance(glm::make_vec3(u.view.cameraPosition.data()),glm::make_vec3(p.view.cameraPosition.data()))>3.f)return false;
    const auto direction=[](const WorldTemporalView& v){const auto inv=glm::inverse(glm::make_mat4(v.viewProjection.data()));const auto h=inv*glm::vec4(0,0,1,1);return glm::normalize(glm::vec3(h)/h.w-glm::make_vec3(v.cameraPosition.data()));};
    return glm::dot(direction(u.view),direction(p.view))>.5f;
}
}
