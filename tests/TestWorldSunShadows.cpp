#include "engine/render/WorldSunShadows.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace engine::render;
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main()try {
    for(unsigned tier=0;tier<4;++tier) {
        WorldSunShadowInput i;i.quality=tier;i.camera={0,5,12};i.forward=glm::normalize(glm::vec3(0,-.2f,-1));i.light=glm::normalize(glm::vec3(.3f,1,.2f));
        const auto camera=[&]{i.viewProjection=glm::perspective(glm::radians(60.f),1.6f,.05f,10000.f)*glm::lookAt(i.camera,i.camera+i.forward,glm::vec3(0,1,0));};
        camera();WorldSunShadowPlanner planner;auto p=planner.prepare(i);
        const unsigned count=tier==0?2:tier==1?3:4;check(p.budget.cascades==count,"Quality cascade count");
        check(p.budget.resolution==(tier==0?1024u:tier==3?4096u:2048u),"Quality resolution");
        check(p.drawMask==(((1u<<count)-1)|240u),"Initial distant coverage");
        const auto inverse=glm::inverse(i.viewProjection);
        float near=i.nearPlane;
        for(unsigned k=0;k<count;++k) {
            auto matrix=glm::make_mat4(p.draws[k].viewProjection.data());
            for(int y:{-1,1})for(int x:{-1,1}) {
                const auto v=inverse*glm::vec4(float(x),float(y),1,1);const auto ray=glm::normalize(glm::vec3(v)/v.w-i.camera);
                for(float distance:{near,i.ranges[k]}) {
                    const auto point=i.camera+ray*(distance/glm::dot(ray,i.forward));const auto clip=matrix*glm::vec4(point,1);
                    check(std::abs(clip.x)<=1.0001f&&std::abs(clip.y)<=1.0001f&&std::abs(clip.z)<=1.0001f,"Cascade must contain frustum slice");
                }
            }
            near=i.ranges[k]*(1-i.blendRange);
        }
        auto stable=planner.prepare(i);check(stable.drawMask==((1u<<count)-1),"Stable distant cache redraw");
        i.camera.x+=128;camera();auto moved=planner.prepare(i);check((moved.drawMask&~15u)!=0,"Camera invalidates affected distant map");
        i.cut=true;auto cut=planner.prepare(i);check((cut.drawMask&240u)==240u,"Camera cut rebuilds all distant coverage");
        i.cut=false;i.sceneRevision++;auto edit=planner.prepare(i);check((edit.drawMask&240u)==240u,"Scene edit invalidates distant cache");
        i.deltaSeconds=0;auto paused=planner.prepare(i);
        check(paused.drawMask==((1u<<count)-1),"Paused stable sky redrew distant maps");
        i.light=glm::normalize(glm::vec3(.31f,1,.2f));auto scrubbed=planner.prepare(i);
        check((scrubbed.drawMask&240u)==240u&&scrubbed.uniforms[65][2]==1,"Paused sky edit retained a frozen distant blend");
        i.deltaSeconds=1.f/60;i.strength=.5f;auto authored=planner.prepare(i);
        check((authored.drawMask&240u)==240u&&authored.uniforms[32][0]==.5f,"Distant shadows missed authored changes");
        i.light=glm::normalize(glm::vec3(-1,.1f,0));auto preset=planner.prepare(i);
        check((preset.drawMask&240u)==240u,"Large celestial jump retained stale distant maps");
        i.enabled=false;check(planner.prepare(i).drawMask==0,"Disabled sun draws");
    }
    bool rejected=false;try{worldSunShadowBudget(4);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid tier");
    WorldSunShadowInput invalid;invalid.viewProjection=glm::perspective(glm::radians(60.f),1.6f,.05f,10000.f);
    for(unsigned caseIndex=0;caseIndex<5;++caseIndex) {
        auto input=invalid;
        if(caseIndex==0)input.boundsMax.x=std::numeric_limits<float>::infinity();
        if(caseIndex==1)input.filterTexels=-1;
        if(caseIndex==2)input.strength=2;
        if(caseIndex==3)input.width=0;
        if(caseIndex==4)input.distantBlendSeconds=std::numeric_limits<float>::quiet_NaN();
        bool failed=false;try{WorldSunShadowPlanner planner;planner.prepare(input);}catch(const std::invalid_argument&){failed=true;}
        check(failed,"Invalid sun snapshot accepted");
    }
    auto malformed=WorldSunShadowPlan{};malformed.budget.distantResolution=0;
    rejected=false;try{validateWorldSunShadowPlan(malformed);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Malformed sun atlas admitted");
    std::cout<<"PASS sun budgets, frustum coverage, cache reuse, movement, cuts and edits\n";
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
