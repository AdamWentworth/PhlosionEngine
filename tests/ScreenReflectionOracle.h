#pragma once
#include "engine/render/WorldReflectionBlueNoise.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
// Double-precision reference over analytically intersected fixture planes.
// Inputs are the native evaluated material, motion, invalidation and compressed
// depth readbacks; this does not invoke or translate the production shader.
namespace reflection_oracle {
using namespace engine::render;
template<class T> glm::dvec4 bilinear(const T& image,int w,int h,glm::dvec2 uv) {
    const auto p=uv*glm::dvec2(w,h)-.5;const int x=int(std::floor(p.x)),y=int(std::floor(p.y));
    const auto at=[&](int a,int b){const auto q=image[std::clamp(b,0,h-1)*w+std::clamp(a,0,w-1)];return glm::dvec4(q[0],q[1],q[2],q[3]);};
    return glm::mix(glm::mix(at(x,y),at(x+1,y),p.x-x),glm::mix(at(x,y+1),at(x+1,y+1),p.x-x),p.y-y);
}
inline glm::dvec2 noise(int x,int y,int f) {
    static const auto bytes=worldReflectionBlueNoise();
    const auto i=std::size_t((((f&63)*64+(y&63))*64+(x&63))*4);
    return {double(bytes[i]+256u*bytes[i+1])/65535.,double(bytes[i+2]+256u*bytes[i+3])/65535.};
}
inline double radicalInverse(unsigned i) {
    double value=0,place=.5;while(i){value+=(i&1)*place;i>>=1;place*=.5;}return value;
}
inline double confidence(int x,int y,const WorldTemporalImage& image,const WorldMotionImage& surface,
                          const glm::dmat4& vp,const glm::dmat4& view,glm::dvec3 eye,
                          unsigned frame,int tier) {
    const int w=image.width,h=image.height;const auto index=y*w+x;
    const auto nr=surface.normal[index];const auto material=surface.material[index];
    if(surface.identity[index]!=1 || surface.materialClass[index]==3)return -1; // analytic floor only
    const double rough=std::clamp(double(material>>24)/255.,.04,1.);
    const double startR=.3+.1*tier,endR=.5+.1*tier;
    if(rough>endR)return 0;
    const glm::dvec2 uv((x+.5)/w,(y+.5)/h);
    const auto inv=glm::inverse(vp);
    auto near4=inv*glm::dvec4(2*uv.x-1,1-2*uv.y,-1,1),far4=inv*glm::dvec4(2*uv.x-1,1-2*uv.y,1,1);
    const auto nearPoint=glm::dvec3(near4)/near4.w,ray=glm::dvec3(far4)/far4.w-nearPoint;
    const auto position=nearPoint+ray*(-nearPoint.y/ray.y);
    const auto project=[&](glm::dvec3 p){const auto c=vp*glm::dvec4(p,1);return glm::dvec3(.5+.5*c.x/c.w,.5-.5*c.y/c.w,.5-.5*c.z/c.w);};
    const auto toView=[&](glm::dvec3 p){auto r=glm::dmat3(view)*p;r.z=-r.z;return r;};
    const auto fromView=[&](glm::dvec3 p){p.z=-p.z;return glm::transpose(glm::dmat3(view))*p;};
    const auto incident=glm::normalize(toView(position-eye));
    const auto n=glm::normalize(toView(glm::dvec3(nr&1023,(nr>>10)&1023,(nr>>20)&1023)*(2./1023.)-1.));
    const double sign=n.z<0?-1:1,a=-1/(sign+n.z);
    const glm::dvec3 u(1+sign*n.x*n.x*a,sign*n.x*n.y*a,-sign*n.x),v(n.x*n.y*a,sign+n.y*n.y*a,-n.y);
    const auto random=noise(x,y,frame);
    double offset=noise(y,x,frame+32).x;
    const double invalid=bilinear(image.mask,image.maskWidth,image.maskHeight,uv).z;
    const int count=std::clamp(tier+1+int(5*invalid),1,16),steps=tier==0?8:tier==3?16:12;
    double total=0;
    for(int sample=0;sample<count;++sample) {
        auto xi=glm::fract(random+glm::dvec2(double(sample)/count,radicalInverse(sample)));
        const double r=tier==0&&count==1?rough*.75:rough;
        if(tier==0&&count==1)xi=random;
        const double cosine=std::sqrt((1-xi.y)/(1+(std::pow(r,4)-1)*xi.y)),sine=std::sqrt(std::max(0.,1-cosine*cosine));
        const double phi=6.283185307179586*xi.x;
        const auto half=u*(std::cos(phi)*sine)+v*(std::sin(phi)*sine)+n*cosine;
        const auto direction=glm::reflect(incident,half);
        const auto start=project(position),end=project(position+.1*fromView(direction));
        auto delta=end-start;
        const double length2=glm::dot(glm::dvec2(delta),glm::dvec2(delta));
        const double currentOffset=offset;offset=glm::fract(offset+1.61803399);
        if(direction.z<-.8 || length2<1e-12)continue;
        const auto step=delta/(std::sqrt(length2)*(steps+1.));
        auto cursor=start+(currentOffset+.1)*step,hit=cursor;
        const double z=toView(position-eye).z;
        const double tolerance=std::max(std::abs(step.z)*(.6+1.4*std::pow(std::min(1.,z/12),4)),1e-8);
        bool found=false;
        for(int group=0;group<steps && !found;group+=4) {
            if(cursor.x<=0 || cursor.y<=0 || cursor.x>=1 || cursor.y>=1)break;
            for(int lane=0;lane<4;++lane) {
                const auto point=cursor+double(lane)*step;
                const int qx=std::clamp(int(std::floor(point.x*w/4)),0,image.reflectionDepthWidth-1);
                const int qy=std::clamp(int(std::floor(point.y*h/4)),0,image.reflectionDepthHeight-1);
                const double d=image.reflectionDepth[qy*image.reflectionDepthWidth+qx];
                const double diff=d*d-point.z;
                if(!found && diff>0 && diff<2*tolerance){hit=point;found=true;}
            }
            cursor+=4.*step;
        }
        if(!found || hit.x<=0 || hit.y<=0 || hit.x>=1 || hit.y>=1)continue;
        const auto motion=bilinear(surface.velocity,w,h,glm::dvec2(hit));
        const auto previous=glm::dvec2(hit)+glm::dvec2(motion);
        if(motion.w<.5 || previous.x<=0 || previous.y<=0 || previous.x>=1 || previous.y>=1)continue;
        const double edge=std::clamp(20*std::min({hit.x,hit.y,1-hit.x,1-hit.y}),0.,1.);
        total+=edge*std::clamp((endR-rough)/(endR-startR),0.,1.)*std::clamp((direction.z+.8)/.4,0.,1.);
    }
    return total/count;
}
}
