#pragma once

#include "engine/render/IWorldDirectionalShadow.h"
#include "engine/render/WorldEnvironmentLighting.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <span>

namespace engine::render {

inline constexpr std::uint32_t localShadowResolution = 1024;
inline constexpr std::uint32_t localShadowAtlasResolution = 4096;
inline constexpr std::size_t localShadowCapacity = 8;
using WorldLocalShadowUniforms = std::array<std::array<float, 4>, worldLocalShadowSourceCapacity * 6>;

// A representative perspective projection for a one-sided area source. The
// luminous plane is the near clip plane; housing behind it cannot occlude it.
inline WorldDirectionalShadowView rectLightShadowView(const WorldRectLight& light,
                                                       float fieldOfViewDegrees = 0.f,
                                                       const std::array<float,3>& camera = {}) {
    WorldDirectionalShadowView result;
    if(fieldOfViewDegrees==0)fieldOfViewDegrees=light.shadow.angle;
    if ((light.response==WorldRectLightResponse::FiniteSource && !light.source.cone) || !(light.participation&WorldLightShadows) || !validWorldRectLight(light) || light.range <= 0 || !std::isfinite(fieldOfViewDegrees) ||
        fieldOfViewDegrees <= 0 || fieldOfViewDegrees >= 180 ||
        std::max({light.radiance[0], light.radiance[1], light.radiance[2]}) <= 0) return result;
    const glm::vec3 centre(light.position[0], light.position[1], light.position[2]);
    const glm::vec3 u(light.halfU[0], light.halfU[1], light.halfU[2]);
    const glm::vec3 v(light.halfV[0], light.halfV[1], light.halfV[2]);
    const auto normal = glm::normalize(glm::cross(u, v));
    const float hu = glm::length(u), hv = glm::length(v);
    const float tanHalfFov = std::tan(glm::radians(fieldOfViewDegrees * .5f));
    if (tanHalfFov > 16.f) return result;
    const bool finiteLine = light.response != WorldRectLightResponse::Ltc;
    // Match the inscribed line used by direct lighting. A finite line's virtual
    // eye is halfLength*cot(fov/2) behind the source; its radial range includes
    // that shift once. The near floor defines the sphere/very-short-line case.
    const float halfLength = light.response == WorldRectLightResponse::FiniteSource ? light.source.length*.5f
        : finiteLine ? std::max(hu, hv) - std::min(hu, hv) : std::max(hu, hv);
    const float nearPlane = std::max(light.response==WorldRectLightResponse::FiniteSource?.02f:.05f, halfLength / tanHalfFov);
    const auto eye = centre - normal * nearPlane;
    const float farPlane = nearPlane + light.range + (finiteLine ? 0.f : hu + hv);
    const auto matrix = glm::perspective(glm::radians(fieldOfViewDegrees), 1.f, nearPlane,
        farPlane) *
        glm::lookAt(eye, centre, glm::normalize(v));
    std::copy_n(glm::value_ptr(matrix), 16, result.viewProjection.begin());
    const float queryRange=light.shadow.radius>0?std::min(light.range,std::max(1.f,light.shadow.radius)):light.range;
    const auto casterMatrix=glm::perspective(glm::radians(fieldOfViewDegrees),1.f,nearPlane,
        nearPlane+queryRange+(finiteLine?0.f:hu+hv))*glm::lookAt(eye,centre,glm::normalize(v));
    std::copy_n(glm::value_ptr(casterMatrix),16,result.casterViewProjection.begin());
    result.enabled = true;
    result.resolution = localShadowResolution;
    result.strength = worldShadowOpacity(light.shadow,light.position,camera);
    if(result.strength<=0)return {};
    result.momentKernel=worldShadowKernel(light.shadow.softness);
    result.momentCentreRay=true;
    result.staticOnly=light.shadow.staticOnly;
    result.depthBias = .00015f;
    result.normalOffset = .012f;
    if (light.shadowFilter == WorldLocalShadowFilter::DistanceMoments)
        result.distanceMoments = {farPlane / nearPlane, 1 - farPlane / nearPlane, tanHalfFov, 1 / farPlane};
    return result;
}


inline WorldDirectionalShadowView pointLightShadowView(const WorldPointLight& light,
        const std::array<float,3>& camera={}) {
    WorldDirectionalShadowView result;
    if(!light.source.native || !light.source.cone || !(light.participation&WorldLightShadows) ||
       !validWorldLocalLightSource(light.source) || !validWorldLocalShadowSettings(light.shadow) ||
       light.radius<=.02f || light.radius>1000 || light.channels>255 || light.participation>255 ||
       std::max({light.intensity[0],light.intensity[1],light.intensity[2]})<=0)return result;
    for(float x:light.position)if(!std::isfinite(x))return result;
    for(float x:light.intensity)if(!std::isfinite(x)||x<0)return result;
    const glm::vec3 eye=glm::make_vec3(light.position.data()),direction=glm::make_vec3(light.source.axis.data());
    const glm::vec3 up=std::abs(direction.y)<.99f?glm::vec3(0,1,0):glm::vec3(1,0,0);
    const float tangent=std::tan(glm::radians(light.shadow.angle*.5f));
    if(tangent>16)return result;
    const auto matrix=glm::perspective(glm::radians(light.shadow.angle),1.f,.02f,light.radius)*
        glm::lookAt(eye,eye+direction,up);
    result.strength=worldShadowOpacity(light.shadow,light.position,camera);
    if(result.strength<=0)return {};
    std::copy_n(glm::value_ptr(matrix),16,result.viewProjection.begin());
    const float queryRange=light.shadow.radius>0?std::min(light.radius,std::max(1.f,light.shadow.radius)):light.radius;
    const auto casterMatrix=glm::perspective(glm::radians(light.shadow.angle),1.f,.02f,queryRange)*
        glm::lookAt(eye,eye+direction,up);
    std::copy_n(glm::value_ptr(casterMatrix),16,result.casterViewProjection.begin());
    result.enabled=true;result.resolution=localShadowResolution;
    result.distanceMoments={light.radius/.02f,1-light.radius/.02f,tangent,1/light.radius};
    result.momentKernel=worldShadowKernel(light.shadow.softness);
    result.momentCentreRay=true;result.staticOnly=light.shadow.staticOnly;
    return result;
}
inline WorldDirectionalShadowView worldLocalShadowView(const WorldEnvironmentLighting& e,
        unsigned index,const std::array<float,3>& camera={}) {
    if(!e.localShadowMapResolution)return {};
    if(e.localShadowMapResolution!=512 && e.localShadowMapResolution!=1024)
        throw std::invalid_argument("Local shadow resolution must be 0, 512 or 1024");
    auto view=index<worldRectLightCapacity?rectLightShadowView(e.rectLights.at(index),0,camera):
        pointLightShadowView(e.pointLights.at(index-worldRectLightCapacity),camera);
    view.resolution=e.localShadowMapResolution;return view;
}
inline std::uint64_t worldLocalShadowIdentity(const WorldEnvironmentLighting& e,unsigned index) {
    return index<worldRectLightCapacity?e.rectLights.at(index).identity:e.pointLights.at(index-worldRectLightCapacity).identity;
}

// Shared allocation/layout policy. GPU targets remain backend-owned. The sun
// occupies the lower/upper-left 2048 square (API UV origin); locals use the right
// half. Receiver UVs and raster viewport offsets use the same API convention.
struct WorldLocalShadowAtlas {
    bool enabled = false;
    int activeLight = -1;
    std::uint32_t tileCount = 0;
    std::array<std::uint32_t,localShadowCapacity> lights = [] {
        std::array<std::uint32_t,localShadowCapacity> value{};value.fill(UINT32_MAX);return value;
    }();
    std::array<std::uint64_t,worldLocalShadowSourceCapacity> sourceIdentities{};
    std::array<std::uint64_t,localShadowCapacity> owners{};
    std::uint64_t owner(std::uint32_t index) const {
        // Authored IDs occupy a distinct namespace from legacy array indices.
        return sourceIdentities.at(index)?sourceIdentities[index]:0x8000000000000000ull+index;
    }
    void identities(std::span<const std::uint64_t> ids) {
        if(ids.size()!=sourceIdentities.size() || activeLight>=0 || tileCount)
            throw std::invalid_argument("Invalid local shadow identities phase");
        for(std::size_t i=0;i<ids.size();++i) {
            if(ids[i]>=0x8000000000000000ull)throw std::invalid_argument("Reserved shadow identity");
            if(ids[i])for(std::size_t j=0;j<i;++j)
                if(ids[j]==ids[i])throw std::invalid_argument("Duplicate shadow identity");
        }
        std::copy(ids.begin(),ids.end(),sourceIdentities.begin());
    }
    bool planned=false;
    std::uint32_t activeTile=0;
    WorldDirectionalShadowView directionalView{};
    WorldLocalShadowUniforms uniforms{};

    void clear() { activeLight = -1; tileCount = 0; planned=false; directionalView = {}; uniforms = {}; }
    // Reserve the whole visible set before assigning newcomers. Otherwise an
    // early new light could evict a later still-visible light and cascade.
    void plan(std::span<const std::uint32_t> visible) {
        if(activeLight>=0 || tileCount || visible.size()>localShadowCapacity)
            throw std::invalid_argument("Invalid local shadow allocation phase");
        std::array<bool,worldLocalShadowSourceCapacity> seen{};
        for(auto light:visible) {
            if(light>=seen.size() || seen[light])throw std::invalid_argument("Invalid shadow source set");
            seen[light]=true;
        }
        for(auto& id:owners)if(std::none_of(visible.begin(),visible.end(),[&](auto light){return owner(light)==id;}))id=0;
        for(auto light:visible)if(std::find(owners.begin(),owners.end(),owner(light))==owners.end())
            *std::find(owners.begin(),owners.end(),0)=owner(light);
        lights.fill(UINT32_MAX);
        for(auto light:visible)lights[std::find(owners.begin(),owners.end(),owner(light))-owners.begin()]=light;
        planned=true;
    }
    std::uint32_t slot(std::uint32_t light) const {
        if(!planned)return tileCount;
        const auto it=std::find(lights.begin(),lights.end(),light);
        return it==lights.end()?std::uint32_t(localShadowCapacity):std::uint32_t(it-lights.begin());
    }
    std::array<std::uint32_t, 2> offset() const {
        if (activeLight < 0) return {};
        return {(2 + activeTile % 2) * localShadowResolution,
                (activeTile / 2) * localShadowResolution};
    }
    std::array<float, 4> directionalUv() const {
        return {0, 0, enabled ? .5f : 1.f, enabled ? .5f : 1.f};
    }
    bool begin(std::uint32_t index, const WorldDirectionalShadowView& view) {
        if (!enabled || activeLight >= 0 || tileCount >= localShadowCapacity ||
            index >= worldLocalShadowSourceCapacity || uniforms[index * 6 + 4][0] != 0 ||
            !validWorldDirectionalShadowView(view) || (view.resolution != 512 && view.resolution != localShadowResolution)) return false;
        activeTile=slot(index);
        if(activeTile>=localShadowCapacity)return false;
        activeLight = static_cast<int>(index);
        return true;
    }
    void finish(const WorldDirectionalShadowView& view) {
        if (activeLight < 0) return;
        const auto first = static_cast<std::size_t>(activeLight) * 6;
        const auto state = packWorldDirectionalShadow(view);
        for (std::size_t c = 0; c < 4; ++c)
            std::copy_n(state.viewProjection.begin() + c * 4, 4, uniforms[first + c].begin());
        uniforms[first + 4] = state.parameters;
        if (view.distanceMoments[3] > 0)
            uniforms[first + 4] = {view.strength, view.distanceMoments[3],
                view.distanceMoments[2], -1.f / static_cast<float>(view.resolution)};
        const auto xy = offset();
        uniforms[first + 5] = {xy[0] / 4096.f, xy[1] / 4096.f, view.resolution / 4096.f, view.resolution / 4096.f};
        ++tileCount;
        activeLight = -1;
    }
};

} // namespace engine::render

class IWorldLocalShadow {
public:
    virtual ~IWorldLocalShadow() = default;
    virtual void prewarmWorldLocalShadows() = 0;
    virtual void clearWorldLocalShadows() = 0;
    virtual bool beginWorldLocalShadow(std::uint32_t rectangleIndex,
        const engine::render::WorldDirectionalShadowView& view) = 0;
    virtual bool endWorldLocalShadow() = 0;
};

namespace engine::render {
template<class Draw>
void drawWorldRectShadows(IWorldLocalShadow& renderer,
                         const WorldEnvironmentLighting& environment, Draw&& draw, const std::array<float,3>& camera={}) {
    if (packWorldEnvironment(environment)[0][3] == 0) return;
    std::size_t count = 0;
    for (std::uint32_t i = 0; i < worldLocalShadowSourceCapacity && count < localShadowCapacity; ++i) {
        const auto view = worldLocalShadowView(environment,i,camera);
        if (!view.enabled) continue;
        if (!renderer.beginWorldLocalShadow(i, view))
            throw std::runtime_error("Local shadow pass admission failed; prewarm the renderer first.");
        try { draw(view); }
        catch (...) { renderer.endWorldLocalShadow(); throw; }
        if (!renderer.endWorldLocalShadow()) throw std::runtime_error("Local shadow pass close failed.");
        ++count;
    }
}
}
