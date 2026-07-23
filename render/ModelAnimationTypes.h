// src/engine/render/ModelAnimationTypes.h
#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace pac_model_types {

struct NodeTRS {
    glm::vec3 t{0.0f};
    glm::quat r{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 s{1.0f};
    bool hasMatrix = false;
    glm::mat4 matrix{1.0f};
};

struct SkinData {
    std::vector<int> joints;
    std::vector<glm::mat4> inverseBind;
};

enum class ChannelPath { Translation, Rotation, Scale };

struct AnimationSampler {
    std::vector<float> inputs;
    std::vector<glm::vec4> outputs;
    std::string interpolation;
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

} // namespace pac_model_types
