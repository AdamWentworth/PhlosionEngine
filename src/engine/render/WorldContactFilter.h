#pragma once
#include "engine/render/IWorldContactVisibility.h"
#include "engine/render/RenderPhaseProfile.h"
#include <cstdint>

namespace engine::render {
struct WorldContactHistory {
    WorldContactUniforms view{};
    std::uint64_t frame=0, surface=0, scope=0;
    bool valid=false;
    bool accepts(const WorldContactUniforms& next,std::uint64_t number,std::uint64_t target,std::uint64_t owner) const {
        if(!valid || frame+1!=number || surface!=target || scope!=owner ||
           view[13][3]!=next[13][3] || view[8][0]!=next[8][0] || view[8][1]!=next[8][1] || view[8][2]!=next[8][2])return false;
        float distance=0,alignment=0;
        for(int i=0;i<3;++i){const float delta=view[10][i]-next[10][i];distance+=delta*delta;alignment+=view[13][i]*next[13][i];}
        return distance<1.f && alignment>.95f && std::abs(view[11][3]-next[11][3])<.00001f && std::abs(view[12][3]-next[12][3])<.00001f;
    }
};

// The HDR target is temporary scratch before scene color. Depth selection is
// prepared once; all sampling uses immutable snapshots, including previous depth.
template<class Bind,class Copy>
bool filterWorldContact(IRenderBackend& renderer,WorldContactUniforms& state,Bind bind,Copy copy) {
    RenderPhaseScope phase(renderer,"contact-filter");
    const int width=(int(state[8][0])+1)/2,height=(int(state[8][1])+1)/2;
    std::array<IRenderBackend::WorldMeshVertex,4> vertices{};
    for(int i=0;i<4;++i){vertices[i].x=i==0||i==3?-1.f:1.f;vertices[i].y=i<2?-1.f:1.f;vertices[i].a=1;}
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto identity=glm::mat4(1);
    IRenderBackend::WorldTextureData texture;texture.materialMode=2;
    for(int stage : {5,2,3,4}) {
        state[8][3]=float(stage);bind(stage);
        {
            RenderPhaseScope draw(renderer,stage==5?"contact-prepare":stage==2?"contact-horizon":stage==3?"contact-spatial":"contact-temporal");
            renderer.drawWorldIndexedMeshTextured(vertices.data(),vertices.size(),indices.data(),indices.size(),
                &texture,glm::value_ptr(identity),width,height);
        }
        {
            RenderPhaseScope publish(renderer,"contact-publish");
            if(!copy(stage,width,height))return false;
        }
    }
    state[8][3]=1;bind(1);return true;
}
}
