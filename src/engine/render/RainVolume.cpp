#include "engine/render/RainVolume.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace engine::render {
namespace {
std::uint32_t hash(std::uint32_t n) {
    n ^= n >> 16; n *= 2146121005u; n ^= n >> 15; n *= 2221713035u;
    return n ^ (n >> 16);
}
float random(std::uint32_t n) { return static_cast<float>(hash(n)&65535u)/65536.f; }
bool finite(glm::vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
std::uint32_t cellSeed(int x,int y,int z,std::uint32_t seed) {
    return hash((static_cast<std::uint32_t>(x)&255u)*73856093u ^
        (static_cast<std::uint32_t>(y)&255u)*19349663u ^
        (static_cast<std::uint32_t>(z)&255u)*83492791u ^ seed);
}
float fade(glm::vec3 delta) {
    return std::clamp((32.f-glm::length(glm::vec2(delta.x,delta.z)))/8.f,0.f,1.f) *
        std::clamp((16.f-std::abs(delta.y))/5.f,0.f,1.f) *
        std::clamp((glm::length(delta)-.6f)/1.4f,0.f,1.f);
}
void scopeCheck(bool ok) { if (!ok) throw std::runtime_error("Rain resource scope admission failed"); }
}
RainVolume::RainVolume() {
    drops_.reserve(maxDrops*4);impacts_.reserve(maxImpacts*4);indices_.reserve(maxDrops*6);
    for (std::uint32_t i=0;i<maxDrops;++i)
        for (auto corner : {0u,1u,2u,0u,2u,3u}) indices_.push_back(i*4+corner);
    for (int y=0;y<64;++y) for (int x=0;x<32;++x) {
        const float u=(x+.5f)/32.f,v=(y+.5f)/64.f;
        const float alpha=std::exp(-std::pow((u-.5f)*6.f,2.f))*std::sin(v*3.14159265f);
        const auto offset=static_cast<std::size_t>(y*32+x)*4;
        streakPixels_[offset]=streakPixels_[offset+1]=streakPixels_[offset+2]=255;
        streakPixels_[offset+3]=static_cast<unsigned char>(std::lround(alpha*255));
    }
    for (int y=0;y<32;++y) for (int x=0;x<32;++x) {
        const float radius=std::hypot((x+.5f)/16.f-1,(y+.5f)/16.f-1);
        const float alpha=std::exp(-std::pow((radius-.72f)*12.f,2.f));
        const auto offset=static_cast<std::size_t>(y*32+x)*4;
        impactPixels_[offset]=impactPixels_[offset+1]=impactPixels_[offset+2]=255;
        impactPixels_[offset+3]=static_cast<unsigned char>(std::lround(alpha*255));
    }
}
IRenderBackend::WorldTextureData RainVolume::texture(bool impact) const {
    IRenderBackend::WorldTextureData t;
    t.key=t.cacheKey=impact?"phlosion-rain-impact-v1":"phlosion-rain-streak-v1";
    t.rgba=impact?impactPixels_.data():streakPixels_.data();t.width=32;t.height=impact?32:64;
    t.wrapS=t.wrapT=33071;t.alphaMode=2;t.materialMode=255;t.textureSrgb=0;
    return t;
}
void RainVolume::quad(std::vector<IRenderBackend::WorldMeshVertex>& vertices,
    glm::vec3 start,glm::vec3 end,glm::vec3 side,glm::vec3 color,float alpha) {
    const std::array positions{start-side,start+side,end+side,end-side};
    for (std::size_t i=0;i<4;++i) {
        IRenderBackend::WorldMeshVertex v;
        v.x=positions[i].x;v.y=positions[i].y;v.z=positions[i].z;
        v.u=(i==1||i==2)?1.f:0.f;v.v=i<2?0.f:1.f;
        v.r=color.x;v.g=color.y;v.b=color.z;v.a=alpha;v.ny=1;v.tx=1;
        vertices.push_back(v);
    }
}
RainVolumeStats RainVolume::build(const WorldEnvironmentLighting& e,glm::vec3 camera,
    const RainCoverQuery& cover) {
    drops_.clear();impacts_.clear();RainVolumeStats stats;
    if (!e.enabled || !std::isfinite(e.rainIntensity) || e.rainIntensity<=0) return stats;
    if (!finite(camera)||glm::length(camera)>1000000.f||!std::isfinite(e.precipitationSeconds)||
        e.precipitationSeconds<0||e.precipitationSeconds>8192||
        !std::isfinite(e.precipitationWind[0])||!std::isfinite(e.precipitationWind[1])||
        std::abs(e.precipitationWind[0])>100||std::abs(e.precipitationWind[1])>100||!cover||
        packWorldEnvironment(e)[0][3]==0) return stats;
    const float intensity=std::min(e.rainIntensity,1.f);
    const glm::vec3 velocity(e.precipitationWind[0]*.25f,-22,e.precipitationWind[1]*.25f);
    const glm::vec3 direction=glm::normalize(velocity);
    const glm::dvec3 shift=glm::dvec3(velocity)*e.precipitationSeconds;
    const glm::ivec3 origin=glm::ivec3(glm::floor((glm::dvec3(camera)-shift)/8.));
    const float brightness=std::clamp(e.ambientRadiance[1]*2.f+.15f,.18f,.85f);
    const glm::vec3 color=glm::vec3(.72f,.82f,1.f)*brightness;
    for (int z=-4;z<=4;++z) for (int y=-2;y<=2;++y) for (int x=-4;x<=4;++x)
        for (std::uint32_t i=0;i<3;++i) {
            const glm::ivec3 cell=origin+glm::ivec3(x,y,z);
            const auto seed=cellSeed(cell.x,cell.y,cell.z,e.precipitationSeed)+i*17713u;
            const float opacity=std::clamp((intensity-random(seed+4))*8.f,0.f,1.f);
            if (opacity==0) continue;
            const glm::vec3 center=glm::vec3(glm::dvec3(cell)*8.+shift)+
                glm::vec3(random(seed+1),random(seed+2),random(seed+3))*8.f;
            const float alpha=opacity*fade(center-camera)*.65f*
                std::exp(-glm::length(center-camera)*e.hazeDensity);
            if (alpha<.005f) continue;
            const float length=.55f+random(seed+5)*.55f;
            glm::vec3 end=center+direction*length;
            ++stats.queries;
            const auto hit=cover(center-direction*96.f,direction*(96.f+length));
            if (hit && (!finite(hit->point)||!std::isfinite(hit->fraction)||hit->fraction<0||hit->fraction>1)) continue;
            if (hit && hit->fraction<=96.f/(96.f+length)) { ++stats.covered;continue; }
            if (hit) end=hit->point-direction*.015f;
            glm::vec3 side=glm::cross(direction,camera-center);
            if (glm::length(side)<.001f) continue;
            side=glm::normalize(side)*(.024f+random(seed+6)*.014f);
            quad(drops_,center,end,side,color,alpha);++stats.drops;
        }
    const glm::ivec2 surfaceOrigin=glm::ivec2(glm::floor(glm::vec2(camera.x,camera.z)/6.f));
    for (int z=-4;z<=4;++z) for (int x=-4;x<=4;++x) {
        const auto seed=cellSeed(surfaceOrigin.x+x,0,surfaceOrigin.y+z,e.precipitationSeed);
        const double t=e.precipitationSeconds*(2.+std::floor(random(seed)*12.f)/8.)+random(seed+7);
        const float age=static_cast<float>(t-std::floor(t));
        if (age>.55f) continue;
        glm::vec3 top((surfaceOrigin.x+x+random(seed+1))*6.f,camera.y+48.f,
            (surfaceOrigin.y+z+random(seed+2))*6.f);
        // Trace the same wind-aligned world column at any camera height.
        top.x+=velocity.x/velocity.y*top.y;top.z+=velocity.z/velocity.y*top.y;
        ++stats.queries;const auto hit=cover(top,direction*128.f);
        if (!hit||!finite(hit->point)||!finite(hit->normal)||std::abs(hit->normal.y)<.4f) continue;
        glm::vec3 normal=glm::normalize(hit->normal);if (normal.y<0) normal=-normal;
        const float distance=glm::length(hit->point-camera);
        const float alpha=intensity*(1-age/.55f)*.3f*std::clamp((32-distance)/8.f,0.f,1.f);
        if (alpha<.005f) continue;
        const float radius=.035f+age*.25f;
        const glm::vec3 right=glm::normalize(glm::cross(normal,glm::vec3(0,0,1)))*radius;
        const glm::vec3 up=glm::normalize(glm::cross(normal,right))*radius;
        const glm::vec3 center=hit->point+normal*.025f;
        quad(impacts_,center-up,center+up,right,color,alpha);++stats.impacts;
    }
    return stats;
}
void RainVolume::prewarm(IRenderBackend& renderer,WorldResourceScopeId scope) const {
    auto* scopes=dynamic_cast<IWorldResourceScope*>(&renderer);
    if (!scopes) throw std::runtime_error("Rain requires managed world resources");
    scopeCheck(scopes->beginWorldResourceScope(scope));
    for (bool impact : {false,true}) { const auto t=texture(impact);renderer.prewarmWorldTextureData(&t); }
    scopeCheck(scopes->endWorldResourceScope());
}
void RainVolume::draw(IRenderBackend& renderer,WorldResourceScopeId scope,const float* vp,int width,int height) const {
    if (drops_.empty()&&impacts_.empty()) return;
    auto* scopes=dynamic_cast<IWorldResourceScope*>(&renderer);
    if (!scopes) throw std::runtime_error("Rain requires managed world resources");
    scopeCheck(scopes->beginWorldResourceScope(scope));
    for (bool impact : {false,true}) {
        const auto& vertices=impact?impacts_:drops_;if (vertices.empty()) continue;
        const auto t=texture(impact);
        renderer.drawWorldIndexedMeshTextured(vertices.data(),vertices.size(),indices_.data(),vertices.size()/4*6,&t,vp,width,height);
    }
    scopeCheck(scopes->endWorldResourceScope());
}
} // namespace engine::render
