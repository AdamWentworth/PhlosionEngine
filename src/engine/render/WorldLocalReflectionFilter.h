#pragma once
#include "engine/render/IWorldLocalReflection.h"
#include "engine/render/IRenderBackend.h"
#include "engine/render/RenderPhaseProfile.h"

namespace engine::render {
// Build a face-isolated box pyramid in unused rows of the same frame-owned
// atlas. Sampling and copying are separate draws/barriers, never feedback.
template<class Copy>
bool filterWorldLocalReflectionStep(IRenderBackend& renderer,WorldLocalReflectionUniforms& state,
    unsigned step,Copy copy,int scratchWidth,int scratchHeight,bool clear=true) {
    RenderPhaseScope phase(renderer,"local-reflection-filter");
    const auto original=state;const int base=int(original[2][3]);
    const int levels=std::min(5,int(std::log2(base)));
    if(step>=unsigned(levels*2))throw std::invalid_argument("Invalid reflection filter step");
    const int pass=step/levels,level=step%levels+1,size=base>>level;
    std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
    for(int i=0;i<4;++i){vertices[i].x=i==0||i==3?-1.f:1.f;vertices[i].y=i<2?-1.f:1.f;vertices[i].a=1;}
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto identity=glm::mat4(1);
    IRenderBackend::WorldTextureData texture;texture.materialMode=2;texture.depthTestEnabled=0;
    const bool batch=3*size<=scratchWidth && 2*size<=scratchHeight;
    const auto pad=[&](int targetLevel,bool prepared) {
        if(original[4][3]<.5f)return true;
        const int extent=(base>>targetLevel)+2;
        const bool packed=3*extent<=scratchWidth && 2*extent<=scratchHeight;
        for(int face=0;face<6;face+=packed?6:1) {
            state=original;state[0][3]=-6;state[3][2]=packed?-1.f:float(face);
            state[3][3]=float(targetLevel);state[4][2]=prepared?1.f:0.f;
            renderer.drawWorldIndexedMeshTextured(vertices.data(),vertices.size(),indices.data(),indices.size(),
                &texture,glm::value_ptr(identity),packed?3*extent:extent,packed?2*extent:extent);
            if(!copy(extent,face+24+(prepared?12:0),targetLevel,base,clear && (packed||face==5),packed))return false;
        }
        return true;
    };
    try {
        if(step==0 && !pad(0,false)){state=original;return false;}
        for(int face=0;face<6;face+=batch?6:1) {
            state=original;state[0][3]=pass?-4.f:-2.f;
            state[3][2]=batch?-1.f:float(face);state[3][3]=float(level);
            renderer.drawWorldIndexedMeshTextured(vertices.data(),vertices.size(),indices.data(),indices.size(),
                &texture,glm::value_ptr(identity),batch?3*size:size,batch?2*size:size);
            if(!copy(size,face+(pass?12:0),level,base,clear && (batch||face==5),batch)){state=original;return false;}
        }
        if(!pad(level,pass!=0)){state=original;return false;}
    } catch(...) {state=original;throw;}
    state=original;return true;
}
template<class Copy>
bool filterWorldLocalReflection(IRenderBackend& renderer,WorldLocalReflectionUniforms& state,bool prepared,Copy copy,int scratchWidth,int scratchHeight) {
    const unsigned count=(prepared?2:1)*std::min(5,int(std::log2(state[2][3])));
    for(unsigned step=0;step<count;++step)
        if(!filterWorldLocalReflectionStep(renderer,state,step,copy,scratchWidth,scratchHeight,step+1==count))return false;
    state[0][3]=prepared?2.f:1.f;return true;
}
}
