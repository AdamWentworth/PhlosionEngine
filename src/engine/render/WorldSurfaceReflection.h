#pragma once
#include "engine/render/IWorldSurfaceBuffer.h"
#include "engine/render/IWorldScreenReflection.h"
#include <algorithm>
namespace engine::render {
inline bool surfaceReflectionViewMatches(const WorldContactUniforms& surface,const WorldScreenReflectionUniforms& screen) {
    return screen[8][3]==1.f && surface[9][3]>.5f &&
        surface[8][0]==screen[8][0] && surface[8][1]==screen[8][1] &&
        std::equal(screen.begin(),screen.begin()+8,surface.begin());
}
inline void drawWorldSurfaceReflectionResolve(IRenderBackend& renderer,int width,int height,const WorldClusteredLightingView* view=nullptr, bool preserveDeferredHaze=false) {
    std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
    for(int i=0;i<4;++i){vertices[i].x=i==0||i==3?-1.f:1.f;vertices[i].y=i<2?-1.f:1.f;vertices[i].a=1;}
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto matrix=glm::mat4(1);
    IRenderBackend::WorldTextureData material;material.materialMode=2;
    if(view) {
        material.environment=view->environment;material.environment.skyDraw=false;
        if(!preserveDeferredHaze)material.environment.localHazeDeferred=false;
        material.cameraPosX=view->camera[0];material.cameraPosY=view->camera[1];material.cameraPosZ=view->camera[2];
        material.cameraForwardX=view->forward[0];material.cameraForwardY=view->forward[1];material.cameraForwardZ=view->forward[2];
    }
    renderer.drawWorldIndexedMeshTextured(vertices.data(),vertices.size(),indices.data(),indices.size(),
        &material,glm::value_ptr(matrix),width,height);
}
}
