#pragma once
#include "SceneBloomSource.h"
#include <string>
#include <string_view>
#include <utility>

namespace engine::render {
inline std::string injectSceneBloom(std::string_view shader,bool hlsl=false) {
    std::string body=sceneBloomSource;
    if(hlsl) {
        for(const auto& [from,to] : {std::pair{"vec3","float3"},std::pair{"vec2","float2"}}) {
            std::size_t p=0;
            while((p=body.find(from,p))!=std::string::npos) {body.replace(p,4,to);p+=6;}
        }
    }
    std::string result(shader);
    constexpr std::string_view marker="__PHLOSION_SCENE_BLOOM__";
    result.replace(result.find(marker),marker.size(),body);
    return result;
}
}
