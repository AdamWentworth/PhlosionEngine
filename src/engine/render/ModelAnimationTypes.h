// src/engine/render/ModelAnimationTypes.h
#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::render::model_types {

struct NodeTRS {
    glm::vec3 t{0.0f};
    glm::quat r{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 s{1.0f};
    // Some authored skeletons cancel the immediate parent's local scale
    // before evaluating this node. Game Freak rigs use this for helper
    // children beneath squash/stretch bones; omitting it distorts limbs.
    bool segmentScaleCompensate = false;
    bool hasMatrix = false;
    glm::mat4 matrix{1.0f};
};

struct SkinData {
    std::vector<int> joints;
    std::vector<glm::mat4> inverseBind;
};

enum class ChannelPath { Translation, Rotation, Scale };

struct AnimationSampler {
    // Key times in seconds, strictly increasing for imported data.
    std::vector<float> inputs;
    // One (x, y, z, w) entry per key for LINEAR and STEP. For CUBICSPLINE this
    // holds the raw per-key glTF [in-tangent, value, out-tangent] triplets
    // exactly as authored, so outputs.size() == 3 * inputs.size(); the shared
    // sampler consumes the tangents directly and no other engine code may
    // assume one entry per key. Rotation entries are xyzw in every case.
    std::vector<glm::vec4> outputs;
    std::string interpolation;  // "LINEAR", "STEP" or "CUBICSPLINE"
    bool isVec4 = false;
};

struct AnimationChannel {
    int samplerIndex = -1;
    int targetNode = -1;
    ChannelPath path = ChannelPath::Translation;
};

struct AnimationClip {
    std::string name;
    float durationSec = 0.0f;
    std::vector<AnimationSampler> samplers;
    std::vector<AnimationChannel> channels;
};

} // namespace engine::render::model_types
