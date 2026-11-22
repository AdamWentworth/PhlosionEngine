#pragma once
#include "engine/render/IWorldPrecipitationLighting.h"
#include "engine/render/IWorldLocalHaze.h"
namespace engine::render {
struct WorldFogResult {WorldPrecipitationLightingResult nearVolume,distant;};
inline WorldFogResult prepareWorldFog(IRenderBackend& renderer,WorldPrecipitationLightingView view) {
    auto* fog=dynamic_cast<IWorldPrecipitationLighting*>(&renderer);
    if(!fog)throw std::runtime_error("Renderer requires volumetric fog");
    view.distant=false;WorldFogResult result;
    result.nearVolume=fog->updateWorldPrecipitationLighting(view);view.distant=true;
    result.distant=fog->updateWorldPrecipitationLighting(view);view.distant=false;
    fog->bindWorldFog(&view,&result.nearVolume,&result.distant);return result;
}
inline void composeWorldFog(IRenderBackend& renderer,const WorldPrecipitationLightingView& view) {
    const auto& v=view.view;
    const glm::mat4 inverse=glm::inverse(glm::make_mat4(v.viewProjection.data())),identity(1);
    constexpr std::array<unsigned char,4> white{255,255,255,255};
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
    for(int i=0;i<4;++i) {
        auto& p=vertices[i];p.x=(i==0||i==3)?-1.f:1.f;p.y=i<2?-1.f:1.f;
        p.u=(p.x+1)*.5f;p.v=(p.y+1)*.5f;p.r=p.g=p.b=p.a=1;p.nz=1;p.tx=1;
    }
    IRenderBackend::WorldTextureData t;
    t.key=t.cacheKey="phlosion://world/fog-white";t.rgba=white.data();t.width=t.height=1;
    t.lightProjectionKey=IWorldDepthControl::snapshotTextureKey;
    t.environment=view.environment;t.environment.skyDraw=false;
    t.cameraPosX=v.cameraPosition[0];t.cameraPosY=v.cameraPosition[1];t.cameraPosZ=v.cameraPosition[2];
    t.alphaMode=2;t.blendMode=2;t.depthTestEnabled=0;t.materialMode=239;
    t.materialRect0U=float(v.width);t.materialRect0V=float(v.height);
    std::copy_n(glm::value_ptr(inverse),16,t.projectedShadowMatrix.begin());
    for(int c=0;c<4;++c)t.lightProjectionUvRowU[c]=inverse[c][3];
    RenderPhaseScope phase(renderer,"fog-compose");
    renderer.drawWorldIndexedMeshTextured(vertices.data(),4,indices.data(),6,&t,glm::value_ptr(identity),v.width,v.height);
}
}
