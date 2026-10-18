#pragma once

#include "engine/render/IRenderBackend.h"
#include "engine/render/IWorldDepthControl.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <stdexcept>

namespace engine::render {
// Private HDR scratch target, independent of scene/reflection color. End restores
// the scene target and exposes the scratch image to material 248 only.
class IWorldLocalHaze {
public:
    virtual ~IWorldLocalHaze() = default;
    virtual bool beginWorldLocalHaze(int width, int height) = 0;
    virtual bool endWorldLocalHaze() = 0;
};

inline bool isWorldLocalHazePass(const IRenderBackend::WorldTextureData* t) {
    return t && (t->materialMode == 247 || t->materialMode == 248);
}

// Call after opaque surfaces and before transparent effects. Ordinary material
// draws must have localHazeAlbedo zero; this adds the view integral once.
inline void drawWorldLocalHaze(IRenderBackend& renderer, const WorldEnvironmentLighting& environment,
    const std::array<float,3>& camera, const float* viewProjection, int width, int height) {
    if (!environment.enabled || environment.localHazeAlbedo <= 0 || environment.hazeDensity <= 0) return;
    if (!viewProjection || width <= 0 || height <= 0 || width > 8192 || height > 8192)
        throw std::invalid_argument("Invalid local haze view");
    auto* target = dynamic_cast<IWorldLocalHaze*>(&renderer);
    auto* depth = dynamic_cast<IWorldDepthControl*>(&renderer);
    if (!target || !depth || !depth->captureWorldDepthSnapshot())
        throw std::runtime_error("Local haze requires scene depth and HDR target");
    const int lowWidth=(width+3)/4, lowHeight=(height+3)/4;
    const glm::mat4 inverse=glm::inverse(glm::make_mat4(viewProjection));
    for (int i=0;i<16;++i) if (!std::isfinite(glm::value_ptr(inverse)[i]))
        throw std::invalid_argument("Singular local haze view");
    constexpr std::array<unsigned char,4> white{255,255,255,255};
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
    for (int i=0;i<4;++i) {
        auto& v=vertices[i]; v.x=(i==0||i==3)?-1.f:1.f;v.y=i<2?-1.f:1.f;
        v.u=(v.x+1)*.5f;v.v=(v.y+1)*.5f;v.r=v.g=v.b=v.a=1;v.nz=1;v.tx=1;
    }
    IRenderBackend::WorldTextureData t;
    t.key=t.cacheKey="phlosion://world/local-haze-white";t.rgba=white.data();t.width=t.height=1;
    t.lightProjectionKey=IWorldDepthControl::snapshotTextureKey;
    t.environment=environment;t.environment.skyDraw=false;t.environment.localHazeDeferred=false;
    t.cameraPosX=camera[0];t.cameraPosY=camera[1];t.cameraPosZ=camera[2];
    t.alphaMode=2;t.blendMode=2;t.depthTestEnabled=0;t.materialMode=247;
    t.materialRect0U=float(width);t.materialRect0V=float(height);
    t.materialRect0W=float(lowWidth);t.materialRect0H=float(lowHeight);
    std::copy_n(glm::value_ptr(inverse),16,t.projectedShadowMatrix.begin());
    for(int col=0;col<4;++col)t.lightProjectionUvRowU[col]=inverse[col][3];
    const glm::mat4 identity(1);
    if (!target->beginWorldLocalHaze(lowWidth,lowHeight)) throw std::runtime_error("Local haze target unavailable");
    renderer.drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&t,glm::value_ptr(identity),lowWidth,lowHeight);
    if (!target->endWorldLocalHaze()) throw std::runtime_error("Local haze target close failed");
    t.materialMode=248;t.blendMode=1;
    renderer.drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&t,glm::value_ptr(identity),width,height);
}
}
