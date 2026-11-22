#include "engine/render/WorldSunShadows.h"
#include "engine/render/WorldReflectionBlueNoise.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace engine::render {
const std::vector<unsigned char>& worldSunShadowNoise(){static const auto noise=worldReflectionBlueNoise();return noise;}
WorldSunShadowBudget worldSunShadowBudget(unsigned quality) {
    switch(quality) {case 0:return {2,1024,1024};case 1:return {3,2048,2048};
        case 2:return {4,2048,2048};case 3:return {4,4096,2048};}
    throw std::invalid_argument("Invalid sun shadow quality");
}
namespace {
glm::vec3 normalized(glm::vec3 v) {
    if(!std::isfinite(glm::dot(v,v))||glm::dot(v,v)<1e-10f)throw std::invalid_argument("Invalid sun shadow direction");
    return glm::normalize(v);
}
std::array<float,4> tile(unsigned slot,const WorldSunShadowBudget& b) {
    unsigned x=0,y=0,res=b.resolution;
    if(slot<4){x=(slot%2)*res;y=(slot/2)*res;}
    else {res=b.distantResolution;const unsigned columns=b.width()/res;x=((slot-4)%columns)*res;y=2*b.resolution+((slot-4)/columns)*res;}
    return {float(x)/b.width(),float(y)/b.height(),float(res)/b.width(),float(res)/b.height()};
}
WorldDirectionalShadowView makeView(glm::vec3 centre,glm::vec3 light,glm::vec3 up,float radius,float depth,float extension,unsigned resolution,const WorldSunShadowInput& i) {
    WorldDirectionalShadowView v;v.enabled=true;v.resolution=resolution;v.strength=i.strength;
    v.depthBias=i.depthBias;v.normalOffset=i.normalOffset;
    const auto matrix=glm::ortho(-radius,radius,-radius,radius,.01f,2*depth+extension+.01f)*
        glm::lookAt(centre+light*(depth+extension),centre,up);
    std::copy_n(glm::value_ptr(matrix),16,v.viewProjection.begin());
    return v;
}
void record(WorldSunShadowPlan& plan,unsigned logical,unsigned slot,const WorldDirectionalShadowView& view,float far,float blend,float filter) {
    const unsigned row=logical*7;
    for(unsigned c=0;c<4;++c)std::copy_n(view.viewProjection.data()+c*4,4,plan.uniforms[row+c].begin());
    plan.uniforms[row+4]={view.strength,view.depthBias,view.normalOffset,1.f/view.resolution};
    plan.uniforms[row+5]=tile(slot,plan.budget);
    plan.uniforms[row+6]={far,blend,filter/float(view.resolution),float(slot)};
}
}
void WorldSunShadowPlanner::reset(){*this={};}
WorldSunShadowPlan WorldSunShadowPlanner::prepare(const WorldSunShadowInput& i) {
    WorldSunShadowPlan p;p.budget=worldSunShadowBudget(i.quality);
    if(!i.enabled){reset();return p;}
    const glm::vec3 light=normalized(i.light),forward=normalized(i.forward);
    for(float value:{i.nearPlane,i.blendRange,i.casterExtension,i.strength,i.normalOffset,
                    i.depthBias,i.filterTexels,i.distantBaseRadius,i.distantBlendSeconds})
        if(!std::isfinite(value))throw std::invalid_argument("Nonfinite sun shadow settings");
    if(!i.width||!i.height||i.width>16384||i.height>16384||i.casterExtension<0||
       i.strength<0||i.strength>1||i.normalOffset<0||i.depthBias<0||i.filterTexels<0||
       i.filterTexels>32||i.distantBaseRadius<=0)
        throw std::invalid_argument("Invalid sun shadow settings");
    if(!i.view||!i.sceneRevision||!std::isfinite(i.deltaSeconds)||i.deltaSeconds<0||i.deltaSeconds>10||
       i.blendRange<=0||i.blendRange>=.5f||i.nearPlane<=0||i.distantBlendSeconds<=0)
        throw std::invalid_argument("Invalid sun shadow snapshot");
    for(unsigned k=0;k<3;++k)if(!std::isfinite(i.camera[k])||!std::isfinite(i.boundsMin[k])||!std::isfinite(i.boundsMax[k])||i.boundsMin[k]>=i.boundsMax[k])throw std::invalid_argument("Invalid sun shadow bounds");
    for(unsigned k=0;k<4;++k)if(!std::isfinite(i.ranges[k])||i.ranges[k]<=(k?i.ranges[k-1]:i.nearPlane))throw std::invalid_argument("Invalid cascade ranges");
    const std::array<float,10> settings{i.boundsMin.x,i.boundsMin.y,i.boundsMin.z,i.boundsMax.x,i.boundsMax.y,i.boundsMax.z,
        i.distantBaseRadius,i.strength,i.normalOffset,i.depthBias};
    const bool pausedEdit=i.deltaSeconds==0&&(light!=oldLight_||i.camera!=oldCamera_);
    const bool lightCut=valid_&&glm::dot(light,oldLight_)<.95f;
    const bool resetState=!valid_||i.cut||view_!=i.view||revision_!=i.sceneRevision||quality_!=i.quality||
        settings!=distantSettings_||pausedEdit||lightCut;
    if(resetState)reset();
    const auto right=normalized(glm::cross(std::abs(light.z)>.98f?glm::vec3(1,0,0):glm::vec3(0,0,1),light));
    const auto up=normalized(glm::cross(light,right));
    const auto inv=glm::inverse(i.viewProjection);
    std::array<glm::vec3,4> rays{};unsigned corner=0;
    for(int y:{-1,1})for(int x:{-1,1}) {
        const glm::vec4 v=inv*glm::vec4(float(x),float(y),1,1);
        if(!std::isfinite(v.w)||std::abs(v.w)<1e-10f)throw std::invalid_argument("Invalid sun shadow camera matrix");
        rays[corner++]=normalized(glm::vec3(v)/v.w-i.camera);
    }
    float near=i.nearPlane;
    for(unsigned cascade=0;cascade<p.budget.cascades;++cascade) {
        const float far=i.ranges[cascade];std::array<glm::vec3,8> corners{};
        for(unsigned k=0;k<4;++k){const float d=glm::dot(rays[k],forward);if(d<=.001f)throw std::invalid_argument("Invalid cascade frustum");
            corners[k]=i.camera+rays[k]*(near/d);corners[4+k]=i.camera+rays[k]*(far/d);}
        const auto a=corners[0]-i.camera,b=corners[4]-i.camera;
        const float along=(glm::dot(b,b)-glm::dot(a,a))/(2*glm::dot(b-a,forward));
        const glm::vec3 sphere=i.camera+forward*along;
        float radius=0;glm::vec3 lo(1e30f),hi(-1e30f);
        for(const auto& c:corners){radius=std::max(radius,glm::length(c-sphere));const glm::vec3 q(glm::dot(c,right),glm::dot(c,up),glm::dot(c,light));lo=glm::min(lo,q);hi=glm::max(hi,q);}
        radius=std::ceil(radius+4*radius/float(p.budget.resolution));
        const auto mid=(lo+hi)*.5f;glm::vec3 centre=right*mid.x+up*mid.y+light*mid.z;
        const float step=8*radius/float(p.budget.resolution);
        auto& phase=snapPhase_[cascade];
        if(!resetState){phase.x+=glm::dot(centre,right-oldRight_);phase.y+=glm::dot(centre,up-oldUp_);}
        for(unsigned k=0;k<2;++k){phase[k]-=std::floor(phase[k]/step)*step;const auto axis=k?up:right;const float projection=glm::dot(centre,axis);
            centre+=axis*(std::floor((projection-phase[k])/step)*step+phase[k]-projection);}
        auto v=makeView(centre,light,up,radius,(hi.z-lo.z)*.5f,i.casterExtension,p.budget.resolution,i);
        v.sunTile=cascade;p.draws[cascade]=v;p.drawMask|=1u<<cascade;
        record(p,cascade,cascade,v,far,1-i.blendRange,i.filterTexels);
        near=far*(1-i.blendRange);
    }
    oldRight_=right;oldUp_=up;oldLight_=light;oldCamera_=i.camera;distantSettings_=settings;
    const auto distantView=[&](unsigned level) {
        const float worldRadius=std::max((i.boundsMax.x-i.boundsMin.x)*.5f,(i.boundsMax.z-i.boundsMin.z)*.5f);
        const float radius=level==3?worldRadius:i.distantBaseRadius*std::pow(4.f,float(level));
        glm::vec3 centre=glm::clamp(i.camera,i.boundsMin,i.boundsMax);
        if(level==3)centre=(i.boundsMin+i.boundsMax)*.5f;
        else {const float grid=radius*.125f,texel=2*radius/p.budget.distantResolution;
            for(unsigned k=0;k<3;++k){centre[k]=std::round(centre[k]/grid)*grid;centre[k]=std::round(centre[k]/texel)*texel;}}
        auto v=makeView(centre,light,up,radius,radius,0,p.budget.distantResolution,i);v.staticOnly=true;return v;
    };
    if(resetState) {
        for(unsigned level=0;level<4;++level){distant_[level]=distantView(level);auto v=distant_[level];v.sunTile=slots_[level];p.draws[v.sunTile]=v;p.drawMask|=1u<<v.sunTile;}
        blend_=1;
    } else {
        blend_=std::min(1.f,blend_+i.deltaSeconds/i.distantBlendSeconds);
        if(blend_==1) {
            const unsigned level=(iteration_%2)?0:(iteration_/2)+1;
            const auto desired=distantView(level);
            if(desired.viewProjection!=distant_[level].viewProjection) {
                previous_=distant_[level];previousSlot_=slots_[level];updated_=level;
                slots_[level]=spare_;spare_=previousSlot_;distant_[level]=desired;
                auto v=desired;v.sunTile=slots_[level];p.draws[v.sunTile]=v;p.drawMask|=1u<<v.sunTile;blend_=0;
            }
            iteration_=(iteration_+1)%6;
        }
    }
    for(unsigned level=0;level<4;++level)record(p,4+level,slots_[level],distant_[level],0,1,i.filterTexels);
    if(previous_.enabled)record(p,8,previousSlot_,previous_,0,1,i.filterTexels);
    p.uniforms[63]={i.camera.x,i.camera.y,i.camera.z,float(p.budget.cascades)};
    p.uniforms[64]={forward.x,forward.y,forward.z,4};
    p.uniforms[65]={float(p.budget.width()),float(p.budget.height()),blend_,float(updated_)};
    p.uniforms[66]={light.x,light.y,light.z,previous_.enabled?1.f:0.f};
    p.uniforms[67]={1,float(i.width),float(i.height),float(i.frame%64)};
    for(unsigned c=0;c<4;++c)std::copy_n(glm::value_ptr(i.viewProjection)+c*4,4,p.uniforms[68+c].begin());
    p.identity=i.view;
    valid_=true;view_=i.view;revision_=i.sceneRevision;quality_=i.quality;
    validateWorldSunShadowPlan(p);return p;
}
void validateWorldSunShadowPlan(const WorldSunShadowPlan& p) {
    bool supported=false;
    for(unsigned tier=0;tier<4;++tier){const auto b=worldSunShadowBudget(tier);
        supported|=b.cascades==p.budget.cascades&&b.resolution==p.budget.resolution&&b.distantResolution==p.budget.distantResolution;}
    if(!supported)throw std::invalid_argument("Invalid sun shadow atlas budget");
    if(p.drawMask&~511u)throw std::invalid_argument("Invalid sun shadow draw mask");
    for(const auto& row:p.uniforms)for(float v:row)if(!std::isfinite(v))throw std::invalid_argument("Nonfinite sun shadow plan");
    for(unsigned k=0;k<9;++k)if(p.drawMask&(1u<<k))if(!validWorldDirectionalShadowView(p.draws[k])||p.draws[k].sunTile!=k)throw std::invalid_argument("Invalid sun shadow draw");
}
}
