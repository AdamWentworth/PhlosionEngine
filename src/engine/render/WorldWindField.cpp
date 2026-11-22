#include "engine/render/IWorldWindField.h"
#include <algorithm>
#include <cmath>
namespace engine::render {
namespace {
bool finite(glm::vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
}
WorldWindFieldPlan planWorldWindField(const WorldWindFieldInput& u){
    if(!u.owner||u.quality>3||!finite(u.camera)||!finite(u.forward)||
       !std::isfinite(u.wind.seconds)||u.wind.seconds<0||u.wind.seconds>1.e12||
       !std::isfinite(u.wind.strength)||u.wind.strength<0||u.wind.strength>1||
       !std::isfinite(u.wind.velocity[0])||!std::isfinite(u.wind.velocity[1])||
       std::abs(u.wind.velocity[0])>150||std::abs(u.wind.velocity[1])>150||
       glm::any(glm::greaterThan(glm::abs(u.camera),glm::vec3(1.e6f)))||u.impulses.size()>510||u.shelters.size()>12)
        throw std::invalid_argument("Invalid wind field input");
    WorldWindFieldPlan p;auto forward=glm::vec3(u.forward.x,0,u.forward.z);
    if(glm::length(forward)>1.e-5f)forward=glm::normalize(forward);else forward={0,0,-1};
    p.center=u.camera+8.f*forward+glm::vec3(0,4,0);p.snapped=glm::floor(p.center);
    p.input.resize(windClusterRows+4096*9);
    std::array<unsigned,2> typeCounts{};
    p.input[0]={p.snapped.x,p.snapped.y,p.snapped.z,7391};
    p.input[2]={u.wind.velocity[0],0,u.wind.velocity[1],u.wind.strength};
    p.input[4]={float(u.wind.seconds),float(u.wind.generation),float(u.shelters.size()),0};
    for(unsigned i=0;i<u.shelters.size();++i){
        const auto& s=u.shelters[i];
        if(!finite(s.low)||!finite(s.high)||glm::any(glm::greaterThanEqual(s.low,s.high))||
           !std::isfinite(s.transmission)||s.transmission<0||s.transmission>1||!std::isfinite(s.wake)||s.wake<0||s.wake>100)
            throw std::invalid_argument("Invalid wind shelter");
        p.input[8+i*2]={s.low.x,s.low.y,s.low.z,s.transmission};
        p.input[9+i*2]={s.high.x,s.high.y,s.high.z,s.wake};
    }
    for(const auto& shape:u.impulses){
        if(!finite(shape.a)||!finite(shape.b)||!finite(shape.anchor)||
           glm::any(glm::greaterThan(glm::abs(shape.a),glm::vec3(1.e6f)))||
           glm::any(glm::greaterThan(glm::abs(shape.b),glm::vec3(1.e6f)))||
           glm::any(glm::greaterThan(glm::abs(shape.anchor),glm::vec3(32.f)))||!std::isfinite(shape.radius)||shape.radius<=0||shape.radius>32||
           !std::isfinite(shape.intensity)||shape.intensity<0||shape.intensity>20||!std::isfinite(shape.age)||shape.age<0||
           !std::isfinite(shape.lifetime)||shape.lifetime<=0||shape.lifetime>3600)
            throw std::invalid_argument("Invalid wind impulse");
        if(!shape.persistent&&shape.age>=shape.lifetime){++p.rejected;continue;}
        const auto low=glm::min(shape.a,shape.b)-shape.radius,high=glm::max(shape.a,shape.b)+shape.radius;
        if(glm::any(glm::lessThan(high,p.snapped-16.f))||glm::any(glm::greaterThan(low,p.snapped+16.f))){++p.rejected;continue;}
        const unsigned type=glm::length(shape.b-shape.a)>1.e-4f?1u:0u;
        if(typeCounts[type]==255){++p.rejected;continue;}++typeCounts[type];
        const unsigned index=p.shapes++;auto& rows=p.input;
        rows[windShapeRows+index*4]={shape.a.x,shape.a.y,shape.a.z,shape.radius};
        rows[windShapeRows+index*4+1]={shape.b.x,shape.b.y,shape.b.z,shape.intensity};
        rows[windShapeRows+index*4+2]={shape.anchor.x,shape.anchor.y,shape.anchor.z,shape.age/shape.lifetime};
        rows[windShapeRows+index*4+3]={shape.persistent?1.f:0.f,0,0,0};
        const auto a=glm::clamp(glm::ivec3(glm::floor((low-p.snapped+16.f)/2.f)),glm::ivec3(0),glm::ivec3(15));
        const auto b=glm::clamp(glm::ivec3(glm::floor((high-p.snapped+16.f)/2.f)),glm::ivec3(0),glm::ivec3(15));
        for(int z=a.z;z<=b.z;++z)for(int y=a.y;y<=b.y;++y)for(int x=a.x;x<=b.x;++x){
            const auto base=windClusterRows+unsigned(x+y*16+z*256)*9;
            unsigned count=unsigned(rows[base][0]);
            if(rows[base+8][1+type]>=16){++p.overflow;continue;}
            rows[base+8][1+type]+=1;
            ++count;rows[base][0]=float(count);rows[base+count/4][count%4]=float(index);
        }
    }
    return p;
}
}

namespace engine::render {
WorldWindFieldUpdate updateWorldWindField(const WorldWindFieldInput& u,const WorldWindFieldHistory& h){
    WorldWindFieldUpdate out;out.plan=planWorldWindField(u);auto& p=out.plan;
    const double elapsed=u.wind.seconds-h.seconds;
    out.reset=h.seconds<0||elapsed<0||elapsed>.128||u.wind.generation!=h.generation||
        glm::any(glm::greaterThanEqual(glm::abs(p.snapped-h.snapped),glm::vec3(32)));
    auto& input=p.input;
    input[1]=out.reset?input[0]:h.header[0];
    input[3]=out.reset?input[2]:h.header[2];
    input[5]=out.reset?input[4]:h.header[4];
    for(unsigned i=0;i<24;++i)input[32+i]=out.reset?input[8+i]:h.header[8+i];
    unsigned count=out.reset?1:unsigned(std::max(0.,std::floor(u.wind.seconds/.016+1.e-7)-std::floor(h.seconds/.016+1.e-7)));
    if(count>8)throw std::runtime_error("Wind update budget exceeded");
    const auto shift=(p.snapped-h.snapped)*2.f;
    for(unsigned i=0;i<std::max(1u,count);++i){
        std::array<std::array<float,4>,2> rows{};
        if(i==0)rows[0]={shift.x,shift.y,shift.z,out.reset?1.f:0.f};
        rows[1]={p.center.x,p.center.y,p.center.z,count?.016f:0.f};out.steps.push_back(rows);
    }
    return out;
}
void commitWorldWindField(WorldWindFieldHistory& h,const WorldWindFieldInput& u,const WorldWindFieldUpdate& update){
    h.generation=u.wind.generation;h.seconds=u.wind.seconds;h.snapped=update.plan.snapped;
    std::copy_n(update.plan.input.begin(),64,h.header.begin());++h.updates;
}
}
