#pragma once
#include "GltfSceneAsset.h"
#include <span>

namespace engine::render::gltf::scene {
// Immutable digest of the current scene inputs, prepared by the owner before
// dependent views. A new preparation observes in-place pose/exclusion changes;
// it never treats an address or animation timestamp as a content revision.
struct GltfSceneContentRevision {
    std::uint64_t generation=0, pose=0, exclusions=0;
    bool staticCompatible=false;
};
inline GltfSceneContentRevision prepareGltfSceneContentRevision(
        const GltfSceneAsset& asset,const GltfScenePose& pose,std::span<const int> excluded) {
    return {asset.generation,
        gltfSceneContentHash(pose.global.data(),pose.global.size()*sizeof(glm::mat4)),
        gltfSceneContentHash(excluded.data(),excluded.size_bytes()),
        asset.skins.empty() && asset.animations.empty() &&
        pose.clipIndex==kBindPoseClip && gltfScenePoseMatchesAsset(asset,pose)};
}
} // namespace engine::render::gltf::scene
