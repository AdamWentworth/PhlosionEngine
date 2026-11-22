#pragma once
#include "engine/render/RenderBackendTypes.h"
#include "engine/render/WorldTemporalScene.h"
#include "engine/render/WorldEnvironmentLighting.h"
#include "engine/render/WorldDiffuseProbes.h"
#include "engine/render/IWorldLocalShadow.h"
#include "engine/render/IWorldResourceScope.h"
#include <glm/gtc/type_ptr.hpp>
#include <span>
#include <string>
#include <vector>
namespace engine::render {
inline constexpr unsigned particleLightingExtent=1024,particleLightingCapacity=4096;
struct WorldParticleQuad {
    std::array<glm::vec3,4> corners{};
    float backLighting=.5f,wrap=.65f;
    unsigned lightingMipBias=0;
    bool billboard=true;
};
struct WorldParticleLightingView {
    WorldResourceScopeId owner=0;
    WorldTemporalView view;
    WorldEnvironmentLighting environment;
    unsigned quality=1;
    std::span<const WorldParticleQuad> quads;
};
struct WorldParticleLightingResult {
    std::string texture;
    std::vector<std::array<glm::vec2,4>> coordinates;
    unsigned tiles=0,texels=0;
    bool sharedDepth=false;
};
struct WorldParticleLightingDiagnostic {
    unsigned width=0,height=0,tiles=0,texels=0;
    std::uint64_t identity=0,frame=0,bytes=0;
    std::vector<std::array<float,4>> lighting;
};
class IWorldParticleLighting {
public:
    virtual ~IWorldParticleLighting()=default;
    virtual WorldParticleLightingResult updateWorldParticleLighting(const WorldParticleLightingView&)=0;
    virtual bool captureWorldParticleLighting()=0;
    virtual WorldParticleLightingDiagnostic readWorldParticleLighting()=0;
};
inline void appendParticleLightingQuads(std::vector<WorldParticleQuad>& quads,
        std::span<const backend::WorldMeshVertex> vertices,bool billboard) {
    if(vertices.size()%4)throw std::invalid_argument("Particle lighting quad topology");
    for(std::size_t i=0;i<vertices.size();i+=4) {
        WorldParticleQuad q;q.billboard=billboard;
        for(unsigned j=0;j<4;++j)q.corners[j]={vertices[i+j].x,vertices[i+j].y,vertices[i+j].z};
        quads.push_back(q);
    }
}
inline void applyParticleLightingCoordinates(std::span<backend::WorldMeshVertex> vertices,
        std::span<const std::array<glm::vec2,4>> coordinates) {
    if(vertices.size()!=coordinates.size()*4)throw std::invalid_argument("Particle lighting coordinate count");
    for(std::size_t i=0;i<vertices.size();++i) {
        vertices[i].sourceUv2U=coordinates[i/4][i%4].x;vertices[i].sourceUv2V=coordinates[i/4][i%4].y;
    }
}
using ParticleLightRows=std::array<std::array<float,4>,320>;
struct ParticleLightingLayout {
    std::vector<std::array<float,4>> data;
    WorldParticleLightingResult result;
};
inline ParticleLightingLayout particleLightingLayout(const WorldParticleLightingView& u) {
    if(!u.owner||!u.view.identity||!u.view.cameraValid||u.quality>3||
       u.view.width<1||u.view.height<1||u.view.width>8192||u.view.height>8192||u.quads.size()>particleLightingCapacity)
        throw std::invalid_argument("Particle lighting view");
    const auto vp=glm::make_mat4(u.view.viewProjection.data());
    for(int i=0;i<16;++i)if(!std::isfinite(u.view.viewProjection[i]))throw std::invalid_argument("Particle lighting matrix");
    if(std::abs(glm::determinant(vp))<1e-12f)throw std::invalid_argument("Particle lighting singular matrix");
    for(float v:u.view.cameraPosition)if(!std::isfinite(v))throw std::invalid_argument("Particle lighting camera");
    struct Tile {unsigned index,size;};
    std::vector<Tile> tiles;ParticleLightingLayout out;out.result.coordinates.resize(u.quads.size());
    for(unsigned i=0;i<u.quads.size();++i) {
        const auto& q=u.quads[i];
        if(!std::isfinite(q.backLighting)||!std::isfinite(q.wrap)||q.backLighting<0||q.backLighting>1||q.wrap<0||q.wrap>2||q.lightingMipBias>8)
            throw std::invalid_argument("Particle lighting response");
        glm::vec2 lo(1e20f),hi(-1e20f);bool crossesNear=false;unsigned behind=0;
        for(auto p:q.corners) {
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))throw std::invalid_argument("Particle lighting geometry");
            auto h=vp*glm::vec4(p,1);if(h.w<=1e-5f){crossesNear=true;++behind;continue;}
            auto pixel=(glm::vec2(h)/h.w*glm::vec2(.5f,-.5f)+.5f)*glm::vec2(u.view.width,u.view.height);
            lo=glm::min(lo,pixel);hi=glm::max(hi,pixel);
        }
        if(behind==4 || (!crossesNear&&(hi.x<0||hi.y<0||lo.x>u.view.width||lo.y>u.view.height)))continue;
        // Six source tile levels, 2..64, chosen from the projected extent / 16.
        const float extent=crossesNear?1024.f:std::max(hi.x-lo.x,hi.y-lo.y);
        int mip=std::clamp(int(std::log2(std::max(extent/16.f,1e-8f))),0,5);
        mip=std::max(0,mip-int(q.lightingMipBias));
        if(u.quality==0)mip=std::max(0,mip-1);
        tiles.push_back({i,1u<<(mip+1)});
    }
    std::stable_sort(tiles.begin(),tiles.end(),[](auto a,auto b){return a.size>b.size;});
    unsigned x=0,y=0,row=0;
    for(auto t:tiles) {
        if(x+t.size>particleLightingExtent){x=0;y+=row;row=0;}
        if(y+t.size>particleLightingExtent)throw std::runtime_error("Particle lighting atlas capacity");
        const auto& q=u.quads[t.index];
        for(auto p:q.corners)out.data.push_back({p.x,p.y,p.z,0});
        out.data.push_back({float(x),float(y),float(t.size),q.billboard?1.f:0.f});
        out.data.push_back({q.backLighting,q.wrap,0,0});
        for(unsigned c=0;c<4;++c)out.result.coordinates[t.index][c]=
            (glm::vec2(x,y)+.5f+glm::vec2((c==1||c==2)?float(t.size-1):0,c>=2?float(t.size-1):0))/float(particleLightingExtent);
        x+=t.size;row=std::max(row,t.size);out.result.texels+=t.size*t.size;
    }
    out.result.tiles=unsigned(tiles.size());return out;
}
inline ParticleLightRows packParticleLighting(const WorldParticleLightingView& u,unsigned tiles,
        const WorldDiffuseProbeUniforms& indirect,const WorldDirectionalShadowView& shadow,
        const std::array<float,4>& atlas,const WorldLocalShadowUniforms& locals) {
    ParticleLightRows p{};const auto env=packWorldEnvironment(u.environment);
    p[0]={float(u.view.width),float(u.view.height),float(particleLightingExtent),float(tiles)};
    p[1]={u.view.cameraPosition[0],u.view.cameraPosition[1],u.view.cameraPosition[2],float(u.environment.lightChannels)};
    const bool moon=std::max({env[7][0],env[7][1],env[7][2]})>std::max({env[1][0],env[1][1],env[1][2]});
    p[2]=env[moon?6:0];p[2][3]=u.environment.enabled?1.f:0.f;p[3]=env[moon?7:1];p[4]=env[2];
    const auto m=glm::make_mat4(u.view.viewProjection.data()),inverse=glm::inverse(m);
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col){p[6+row][col]=m[col][row];p[10+row][col]=inverse[col][row];}
    if(shadow.enabled){const auto matrix=glm::make_mat4(shadow.viewProjection.data());
        for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)p[14+row][col]=matrix[col][row];
        p[18]={shadow.strength,shadow.depthBias,shadow.normalOffset,1.f/shadow.resolution};p[19]=atlas;}
    std::copy_n(indirect.begin(),3,p.begin()+20);std::copy(locals.begin(),locals.end(),p.begin()+200);
    unsigned count=0;
    const auto append=[&](const auto& light,glm::vec3 intensity,glm::vec3 outward,bool oneSided,int shadowIndex,bool area,
                          WorldLocalLightSource source,float range) {
        if(range<=0||!(light.participation&WorldLightParticles)||!(light.participation&3)||
           !(light.channels&u.environment.lightChannels)||glm::length(intensity)==0)return;
        const auto at=32+count++*8;const auto packed=packWorldLocalLightSource(source);
        p[at]={light.position[0],light.position[1],light.position[2],range};
        p[at+1]={intensity.x,intensity.y,intensity.z,float(light.participation)};
        p[at+2]=packed[0];p[at+3]=packed[1];p[at+4]=packed[2];
        p[at+5]={outward.x,outward.y,outward.z,oneSided?1.f:0.f};
        p[at+6]={float(shadowIndex),area?1.f:0.f,0,0};
    };
    for(unsigned i=0;i<u.environment.pointLights.size();++i) {
        const auto& l=u.environment.pointLights[i];
        append(l,glm::make_vec3(l.intensity.data()),glm::make_vec3(l.source.axis.data()),
            false,int(worldRectLightCapacity+i),false,l.source,l.radius);
    }
    for(unsigned i=0;i<u.environment.rectLights.size();++i) {
        const auto& l=u.environment.rectLights[i];if(!validWorldRectLight(l)||l.range<=0)continue;
        const auto a=glm::make_vec3(l.halfU.data()),b=glm::make_vec3(l.halfV.data());
        auto source=l.source;auto intensity=glm::make_vec3(l.radiance.data());
        const auto n=glm::normalize(glm::cross(a,b));
        if(l.response!=WorldRectLightResponse::FiniteSource) {
            const float ha=glm::length(a),hb=glm::length(b);source.radius=std::min(ha,hb);
            source.length=2*(std::max(ha,hb)-source.radius);auto axis=glm::normalize(ha>=hb?a:b);
            source.axis={axis.x,axis.y,axis.z};intensity*=4*ha*hb;
        }
        append(l,intensity,n,l.response!=WorldRectLightResponse::FiniteSource,int(i),true,source,l.range);
    }
    p[24][0]=float(count);return p;
}
} // namespace engine::render
