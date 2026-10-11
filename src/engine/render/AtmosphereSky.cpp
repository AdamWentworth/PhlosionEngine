#include "engine/render/AtmosphereSky.h"
#include "engine/core/Environment.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace engine::render {
namespace {
constexpr float pi = 3.14159265359f;
constexpr float earthRadius = 6360.f; // Kilometres inside the atmosphere integrator.
constexpr float atmosphereRadius = earthRadius + 100.f;
const glm::vec3 rayleigh(.0058f,.0135f,.0331f);
constexpr float mie = .004f;
float exitDistance(glm::vec3 p, glm::vec3 d, float radius) {
    const float b = glm::dot(p,d), c = glm::dot(p,p)-radius*radius;
    return -b + std::sqrt(std::max(0.f,b*b-c));
}
glm::vec2 density(glm::vec3 p) {
    const float altitude = std::max(0.f,glm::length(p)-earthRadius);
    return {std::exp(-altitude/8.f),std::exp(-altitude/1.2f)};
}
void requireScope(bool okay) {
    if (!okay) throw std::runtime_error("Atmosphere world resource scope admission failed");
}
std::uint32_t cloudHash(std::uint32_t value) {
    value ^= value >> 16u; value *= 2146121005u;
    value ^= value >> 15u; value *= 2221713035u;
    return value ^ (value >> 16u);
}
float periodicNoise(float u, float v, int cells, std::uint32_t seed) {
    const float x = (u - std::floor(u)) * cells;
    const float y = (v - std::floor(v)) * cells;
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const float fx = x - ix, fy = y - iy;
    const float sx = fx*fx*(3-2*fx), sy = fy*fy*(3-2*fy);
    const auto sample = [cells,seed](int a, int b) {
        return static_cast<float>(cloudHash(static_cast<std::uint32_t>(a % cells) +
            static_cast<std::uint32_t>(b % cells)*131u + seed) & 65535u) / 65535.f;
    };
    const float a = sample(ix,iy), b = sample(ix+1,iy);
    const float c = sample(ix,iy+1), d = sample(ix+1,iy+1);
    return (a+(b-a)*sx)*(1-sy) + (c+(d-c)*sx)*sy;
}
}

std::array<float, 2> AtmosphereSky::referenceCloudDensity(float u, float v) {
    if (!std::isfinite(u) || !std::isfinite(v)) return {};
    u -= std::floor(u);v -= std::floor(v);
    std::array<float, 2> result{};
    float weight = .5333333f;
    for (int octave = 0; octave < 4; ++octave) {
        result[0] += weight * periodicNoise(u,v,4 << octave,2317u);
        result[1] += weight * periodicNoise(u*2,v,8 << octave,7189u);
        weight *= .5f;
    }
    return result;
}

std::array<float, 3> AtmosphereSky::referenceRadiance(float sunElevation,
    float viewElevation, float azimuth) {
    if (!std::isfinite(sunElevation) || !std::isfinite(viewElevation) || !std::isfinite(azimuth))
        return {};
    const glm::vec3 origin(0,earthRadius+.002f,0);
    const glm::vec3 sun(std::cos(sunElevation),std::sin(sunElevation),0);
    const float horizontal = std::cos(viewElevation);
    const glm::vec3 view(horizontal*std::cos(azimuth),std::sin(viewElevation),horizontal*std::sin(azimuth));
    const float step = exitDistance(origin,view,atmosphereRadius)/24.f;
    glm::vec2 viewDepth(0);
    glm::vec3 rayleighSum(0),mieSum(0);
    for (int sample=0;sample<24;++sample) {
        const glm::vec3 p = origin + view*((static_cast<float>(sample)+.5f)*step);
        const glm::vec2 localDensity = density(p);
        const glm::vec2 midpointDepth = viewDepth+localDensity*(step*.5f);
        viewDepth += localDensity*step;
        const float b = glm::dot(p,sun);
        const float discriminant = b*b-(glm::dot(p,p)-earthRadius*earthRadius);
        if (b<0 && discriminant>0 && -b-std::sqrt(discriminant)>0) continue;
        const float sunStep = exitDistance(p,sun,atmosphereRadius)/8.f;
        glm::vec2 sunDepth(0);
        for (int lightSample=0;lightSample<8;++lightSample)
            sunDepth += density(p+sun*((static_cast<float>(lightSample)+.5f)*sunStep))*sunStep;
        const glm::vec2 opticalDepth = midpointDepth+sunDepth;
        const glm::vec3 transmittance = glm::exp(-(rayleigh*opticalDepth.x+glm::vec3(mie*1.1f*opticalDepth.y)));
        rayleighSum += transmittance*(localDensity.x*step);
        mieSum += transmittance*(localDensity.y*step);
    }
    const float cosine = glm::dot(sun,view);
    const float rayleighPhase = 3.f/(16.f*pi)*(1.f+cosine*cosine);
    constexpr float g = .76f;
    const float miePhase = 3.f/(8.f*pi)*(1.f-g*g)*(1.f+cosine*cosine)/
        ((2.f+g*g)*std::pow(std::max(.001f,1.f+g*g-2.f*g*cosine),1.5f));
    const glm::vec3 color = 22.f*(rayleighSum*rayleigh*rayleighPhase+mieSum*mie*miePhase);
    return {color.r,color.g,color.b};
}

AtmosphereSky::AtmosphereSky() : atlas_(2048u*290u*4u) {
    for (int layer=0;layer<32;++layer) for (int row=0;row<32;++row) for (int column=0;column<64;++column) {
        const float elevation = static_cast<float>(row)/31.f;
        const auto radiance = referenceRadiance(-.14f+static_cast<float>(layer)/31.f*(pi*.5f+.14f),
            elevation*elevation*pi*.5f,static_cast<float>(column)/63.f*pi);
        const auto offset = (static_cast<std::size_t>(row)*2048u+static_cast<std::size_t>(layer*64+column))*4u;
        for (std::size_t channel=0;channel<3;++channel)
            atlas_[offset+channel] = static_cast<unsigned char>(std::lround(std::sqrt(std::clamp(radiance[channel]/16.f,0.f,1.f))*255.f));
        atlas_[offset+3] = 255;
    }
    // Keep the original 32-row atmosphere intact. A padded 256-square cloud
    // tile below it repeats continuously under bilinear filtering, without
    // another sampler or GPU generation during active play.
    for (int y = 0; y < 258; ++y) for (int x = 0; x < 258; ++x) {
        const auto cloud = referenceCloudDensity(
            static_cast<float>((x+255)%256)/256.f,
            static_cast<float>((y+255)%256)/256.f);
        const auto offset = (static_cast<std::size_t>(y+32)*2048u+x)*4u;
        atlas_[offset] = static_cast<unsigned char>(std::lround(cloud[0]*255.f));
        atlas_[offset+1] = static_cast<unsigned char>(std::lround(cloud[1]*255.f));
        atlas_[offset+3] = 255;
    }
    for (std::size_t i=0;i<vertices_.size();++i) {
        auto& v = vertices_[i];
        v.x = (i&1u) ? 10.f : -10.f;
        v.y = (i&2u) ? 10.f : -10.f;
        v.z = (i&4u) ? 10.f : -10.f;
        v.r=v.g=v.b=v.a=1;v.ny=1;v.tx=1;
    }
    indices_ = {0,2,3,0,3,1,4,5,7,4,7,6,0,4,6,0,6,2,
                1,3,7,1,7,5,0,1,5,0,5,4,2,6,7,2,7,3};
}

IRenderBackend::WorldTextureData AtmosphereSky::texture() const {
    IRenderBackend::WorldTextureData t;
    t.key=t.cacheKey="phlosion-layered-atmosphere-v2";
    t.rgba=atlas_.data();t.width=2048;t.height=290;t.textureSrgb=0;
    t.wrapS=t.wrapT=33071;t.materialMode=0;t.depthTestEnabled=0;
    return t;
}
void AtmosphereSky::prewarm(IRenderBackend& renderer, WorldResourceScopeId scope) const {
    auto* scopes=dynamic_cast<IWorldResourceScope*>(&renderer);
    if (!scopes) throw std::runtime_error("Atmosphere requires managed world resources");
    requireScope(scopes->beginWorldResourceScope(scope));
    const auto t=texture();
    renderer.prewarmWorldTextureData(&t);
    renderer.prewarmWorldIndexedMeshCached("phlosion-atmosphere-cube-v1",vertices_.data(),vertices_.size(),indices_.data(),indices_.size());
    requireScope(scopes->endWorldResourceScope());
}
void AtmosphereSky::draw(IRenderBackend& renderer, WorldResourceScopeId scope,
    const WorldEnvironmentLighting& environment, const std::array<float,3>& camera,
    const float* vp, int width, int height) const {
    if (!environment.enabled) return;
    if (packWorldEnvironment(environment)[0][3]==0)
        throw std::invalid_argument("Invalid atmosphere lighting input");
    auto* scopes=dynamic_cast<IWorldResourceScope*>(&renderer);
    if (!scopes) throw std::runtime_error("Atmosphere requires managed world resources");
    auto t=texture();t.environment=environment;t.environment.skyDraw=true;
    if (engine::env::flagEnabled("PHLOSION_DISABLE_CLOUD_LAYERS")) t.environment.cloudCoverage=0;
    t.cameraPosX=camera[0];t.cameraPosY=camera[1];t.cameraPosZ=camera[2];
    const auto model=glm::translate(glm::mat4(1),glm::vec3(camera[0],camera[1],camera[2]));
    std::copy_n(glm::value_ptr(model),16,t.modelMatrix.begin());
    requireScope(scopes->beginWorldResourceScope(scope));
    renderer.drawWorldIndexedMeshTexturedCached("phlosion-atmosphere-cube-v1",vertices_.data(),vertices_.size(),indices_.data(),indices_.size(),&t,vp,width,height);
    requireScope(scopes->endWorldResourceScope());
}
} // namespace engine::render
