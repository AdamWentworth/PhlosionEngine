#pragma once
#include "engine/models/ModelData.h"
namespace engine::models {
// Source animation clips remain immutable. This policy is applied only while
// evaluating a runtime pose, before the game-owned instance transform is added.
enum class RootMotionPolicy : std::uint8_t {
    PreserveAuthored,
    InPlaceHorizontal,
    InPlaceAll,
};

struct PoseEval {
    bool hasScenePose = false;
    bool hasClipPose = false;
    std::vector<engine::render::model_types::NodeTRS> nodeLocals;
    std::vector<glm::mat4> nodeGlobals;
};

}
