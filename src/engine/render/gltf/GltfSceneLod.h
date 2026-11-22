#pragma once
#include "GltfSceneAsset.h"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace engine::render::gltf::scene {
// Select exclusions for a view owner. Persistent cached views use their own
// stable anchor; primary-camera LOD must not invalidate world content.
// The last authored mesh persists at distance; there is no cull.
inline void appendGltfSceneLodExclusions(const GltfSceneAsset& asset,const GltfScenePose& pose,
    std::array<float,3> camera,float scale,std::vector<int>& excluded) {
    if(!std::isfinite(scale)||scale<=0||scale>16)throw std::invalid_argument("glTF LOD scale");
    for(float x:camera)if(!std::isfinite(x))throw std::invalid_argument("glTF LOD camera");
    if(!gltfScenePoseMatchesAsset(asset,pose))throw std::invalid_argument("glTF LOD pose generation");
    for(const auto& group:asset.lodGroups){
        glm::vec3 low(std::numeric_limits<float>::max()),high(std::numeric_limits<float>::lowest());
        for(std::size_t level=0;level<group.nodes.size();++level){
            const auto& transform=pose.global.at(group.nodes[level]);
            const auto& minimum=group.boundsMin.at(level);const auto& maximum=group.boundsMax.at(level);
            for(unsigned corner=0;corner<8;++corner){
                const auto point=glm::vec3(transform*glm::vec4(
                    (corner&1)?maximum[0]:minimum[0],
                    (corner&2)?maximum[1]:minimum[1],
                    (corner&4)?maximum[2]:minimum[2],1));
                low=glm::min(low,point);high=glm::max(high,point);
            }
        }
        const auto eye=glm::vec3(camera[0],camera[1],camera[2]);
        const float distance=glm::length(eye-glm::clamp(eye,low,high));
        std::size_t level=0;while(level<group.distances.size()&&distance>=group.distances[level]*scale)++level;
        for(std::size_t i=0;i<group.nodes.size();++i)if(i!=level)excluded.push_back(group.nodes[i]);
    }
}
} // namespace engine::render::gltf::scene
