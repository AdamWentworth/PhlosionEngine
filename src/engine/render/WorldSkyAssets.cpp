
#include "engine/render/WorldSkyAssets.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace engine::render {
namespace {
std::uint32_t hash(std::uint32_t v){v^=v>>16;v*=2146121005u;v^=v>>15;v*=2221713035u;return v^(v>>16);}
float random(std::uint32_t v){return float(hash(v)&65535u)/65535.f;}
}
void prepareCelestialAtlas(std::span<unsigned char> atlas){
    if(atlas.size()!=2048u*2112u*4u)throw std::invalid_argument("Celestial atlas shape");
    const auto pixel=[&](unsigned x,unsigned y,float r,float g,float b){
        const auto i=(std::size_t(y)*2048+x)*4;
        atlas[i]=static_cast<unsigned char>(std::lround(std::clamp(r,0.f,1.f)*255));
        atlas[i+1]=static_cast<unsigned char>(std::lround(std::clamp(g,0.f,1.f)*255));
        atlas[i+2]=static_cast<unsigned char>(std::lround(std::clamp(b,0.f,1.f)*255));atlas[i+3]=255;
    };
    // Moon albedo is prepared once, independent of view and lighting.
    for(unsigned y=0;y<258;++y)for(unsigned x=0;x<258;++x){
        float u=(float(std::clamp(int(x)-1,0,255))+.5f)/128-1;
        float v=(float(std::clamp(int(y)-1,0,255))+.5f)/128-1;
        float value=.82f-.25f*std::exp(-((u+.25f)*(u+.25f)+(v-.2f)*(v-.2f))*5);
        value-=.125f*std::exp(-((u-.38f)*(u-.38f)+(v+.12f)*(v+.12f))*12);
        for(unsigned crater=0;crater<32;++crater){
            const float cx=random(crater*7+11)*2-1,cy=random(crater*7+12)*2-1,r=.025f+random(crater*7+13)*.15f;
            const float d=std::hypot(u-cx,v-cy)/r;
            value+=.10f*std::exp(-std::pow((d-.88f)*14.f,2.f))-.12f*std::exp(-d*d*4);
        }
        pixel(512+x,32+y,value,value*.98f,value*.94f);
    }
    // Star cubemap faces: mask, brightness, constellation weight.
    for(unsigned face=0;face<6;++face)for(unsigned y=0;y<514;++y)for(unsigned x=0;x<514;++x){
        const float px=float(std::clamp(int(x)-1,0,511))+.5f,py=float(std::clamp(int(y)-1,0,511))+.5f;
        const unsigned cx=unsigned(px/16),cy=unsigned(py/16),key=cx+cy*131u+face*17161u;
        const float dx=px-float(cx*16)-(.15f+.7f*random(key+37))*16;
        const float dy=py-float(cy*16)-(.15f+.7f*random(key+73))*16;
        const float mask=random(key)>.88f?std::exp(-(dx*dx+dy*dy)*1.5f):0;
        pixel((face%3)*514+x,1024+(face/3)*514+y,mask,.15f+.85f*random(key+1),random(key+2)>.98f?1.f:0.f);
    }
    // Galaxy is an authored faint band, separate from point-star masks.
    for(unsigned y=0;y<258;++y)for(unsigned x=0;x<514;++x){
        const float u=float((int(x)+511)%512)/512,v=float((int(y)+255)%256)/256;
        const float band=std::exp(-std::pow((v-.5f-.035f*std::sin(u*18.84956f))*12.f,2.f));
        const float clouds=.4f+.3f*std::sin(u*31+v*27)+.2f*std::cos(u*63-v*51);
        const float a=std::clamp(band*clouds,0.f,1.f);
        pixel(800+x,32+y,a*.65f,a*.73f,a);
    }
}
}
