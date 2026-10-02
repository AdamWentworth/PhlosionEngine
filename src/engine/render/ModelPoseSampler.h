// src/engine/render/ModelPoseSampler.h
//
// CPU pose sampling for imported skeletal data. This is the shared
// implementation used by Model and by CPU consumers that cannot construct a
// Model (whose constructor requires an OpenGL context).
#pragma once

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include "ModelAnimationTypes.h"

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine::render::animation {

// Borrows the arrays for the duration of one samplePose call. The source data
// is validated, acyclic imported data and must not alias the outputs.
struct PoseSource {
    const std::vector<model_types::NodeTRS>& nodesDefault;
    const std::vector<std::vector<int>>& nodeChildren;
    const std::vector<int>& sceneRoots;
    const std::vector<model_types::SkinData>& skins;
    const std::vector<model_types::AnimationClip>& animations;
    const std::vector<std::string>& nodeNames;
};

// Local TRS or authored matrix to a local transform matrix.
glm::mat4 trsToMat4(const model_types::NodeTRS& node);

// Writes the sampled local pose and the composed global matrices for one clip.
// Defaults, root-motion suppression, loop and endpoint behavior follow the
// existing engine policy documented in ModelAnimation.cpp.
void samplePose(const PoseSource& source,
                float timeSec,
                int animIndex,
                std::vector<model_types::NodeTRS>& outLocal,
                std::vector<glm::mat4>& outGlobal,
                bool suppressHorizontalRootMotion = true,
                bool looping = true);

// Recompose a caller-owned local pose using the same hierarchy and segment
// scale compensation rules as samplePose. Used after blending sampled locals.
void composePose(const PoseSource& source,
                 const std::vector<model_types::NodeTRS>& local,
                 std::vector<glm::mat4>& outGlobal);

} // namespace engine::render::animation
