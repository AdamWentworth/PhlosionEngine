#pragma once
#include "engine/render/IWorldScreenReflection.h"

namespace engine::render {
constexpr int worldScreenReflectionTileSize=8;
inline std::array<int,3> worldScreenReflectionTileLayout(int width,int height) {
    const int columns=(width+worldScreenReflectionTileSize-1)/worldScreenReflectionTileSize;
    const int rows=(height+worldScreenReflectionTileSize-1)/worldScreenReflectionTileSize;
    return {columns,rows,std::max(width,height+rows)};
}

// Place conservative depth bounds below the unchanged radiance image. The
// scene target is scratch; copying the tiles clears it before the final view.
template<class Copy>
bool buildWorldScreenReflectionTiles(IRenderBackend& renderer,WorldScreenReflectionUniforms& state,Copy copy) {
    const auto original=state;
    const auto layout=worldScreenReflectionTileLayout(int(state[8][0]),int(state[8][1]));
    std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
    for(int i=0;i<4;++i){vertices[i].x=i==0||i==3?-1.f:1.f;vertices[i].y=i<2?-1.f:1.f;vertices[i].a=1;}
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto identity=glm::mat4(1);
    IRenderBackend::WorldTextureData texture;texture.materialMode=2;
    state[8][3]=-2;
    renderer.drawWorldIndexedMeshTextured(vertices.data(),vertices.size(),indices.data(),indices.size(),
        &texture,glm::value_ptr(identity),layout[0],layout[1]);
    if(!copy(layout[0],layout[1],layout[2],int(original[8][1]))){state={};return false;}
    state=original;state[9][2]=float(worldScreenReflectionTileSize);
    return true;
}
}
