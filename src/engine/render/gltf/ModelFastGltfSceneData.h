#pragma once

#include <string>
#include <vector>

#include <fastgltf/tools.hpp>

#include "engine/render/ModelAnimationTypes.h"

namespace engine::render::gltf::model {

void buildSceneData(const fastgltf::Asset& asset,
                    fastgltf::DefaultBufferDataAdapter& adapter,
                    std::vector<engine::render::model_types::NodeTRS>& outNodesDefault,
                    std::vector<std::string>* outNodeNames,
                    std::vector<std::vector<int>>& outNodeChildren,
                    std::vector<int>& outNodeMesh,
                    std::vector<int>& outNodeSkin,
                    std::vector<int>& outSceneRoots,
                    std::vector<engine::render::model_types::SkinData>& outSkins,
                    std::vector<engine::render::model_types::AnimationClip>& outAnimations);

}  // namespace engine::render::gltf::model
