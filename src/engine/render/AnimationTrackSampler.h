#pragma once
#include "ModelAnimationTypes.h"
#include <glm/gtc/quaternion.hpp>
namespace engine::render::animation {
// Sample linear/step tracks at resolved clip time. An optional closure window
// blends the two ends of a looping clip; one-shot tracks clamp to authored keys.
glm::vec4 sampleVectorTrack(const model_types::AnimationSampler& sampler,
    float time, float duration, bool looping, float closureWindowSec = 0.0f);
glm::quat sampleRotationTrack(const model_types::AnimationSampler& sampler,
    float time, float duration, bool looping, float closureWindowSec = 0.0f);
}
