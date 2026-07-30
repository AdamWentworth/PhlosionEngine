#pragma once

#include <string>
#include <vector>

#include <fastgltf/tools.hpp>

#include "engine/render/ModelAnimationTypes.h"

namespace pac::model_fastgltf {

void buildSceneData(const fastgltf::Asset& asset,
                    fastgltf::DefaultBufferDataAdapter& adapter,
                    std::vector<pac_model_types::NodeTRS>& outNodesDefault,
                    std::vector<std::string>* outNodeNames,
                    std::vector<std::vector<int>>& outNodeChildren,
                    std::vector<int>& outNodeMesh,
                    std::vector<int>& outNodeSkin,
                    std::vector<int>& outSceneRoots,
                    std::vector<pac_model_types::SkinData>& outSkins,
                    std::vector<pac_model_types::AnimationClip>& outAnimations);

}  // namespace pac::model_fastgltf
