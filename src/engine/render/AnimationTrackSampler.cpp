#include "AnimationTrackSampler.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
namespace engine::render::animation {
namespace {
glm::quat normalizeQuatIfNeeded(const glm::quat& q) {
    const float lenSq = glm::dot(q, q);
    if (lenSq <= 0.0f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (std::abs(lenSq - 1.0f) <= 0.0001f) return q;
    return q * glm::inversesqrt(lenSq);
}

std::size_t findKeyframe(const std::vector<float>& times, float t) {
    if (times.empty()) return 0u;
    if (t <= times.front()) return 0u;
    if (t >= times.back()) return times.size() - 1u;
    const auto it = std::upper_bound(times.begin(), times.end(), t);
    return (it == times.begin()) ? 0u : static_cast<std::size_t>((it - times.begin()) - 1u);
}

float sceneLoopClosureBlendWindowSec(float duration, float closureWindowSec) {
    if (!(duration > 0.0f)) return 0.0f;
    const float kDefaultWindowSec = closureWindowSec;
    return std::min(kDefaultWindowSec, duration * 0.25f);
}

glm::vec4 sampleVec4NoLoop(const engine::render::model_types::AnimationSampler& sampler, float t) {
    if (sampler.inputs.empty() || sampler.outputs.empty()) return glm::vec4(0.0f);
    const auto valueAt = [&](std::size_t index) {
        return sampler.outputs[std::min(index, sampler.outputs.size() - 1u)];
    };
    if (sampler.inputs.size() == 1u || sampler.outputs.size() == 1u) {
        return valueAt(0u);
    }

    const float firstTime = sampler.inputs.front();
    const float lastTime = sampler.inputs.back();
    const glm::vec4 lastValue = valueAt(sampler.inputs.size() - 1u);
    if (t <= firstTime) return valueAt(0u);
    if (t >= lastTime) return lastValue;

    const std::size_t i = findKeyframe(sampler.inputs, t);
    if (i >= sampler.inputs.size() - 1u) return lastValue;
    const float t0 = sampler.inputs[i];
    const float t1 = sampler.inputs[i + 1u];
    const float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;
    const glm::vec4 v0 = valueAt(i);
    const glm::vec4 v1 = valueAt(i + 1u);
    if (sampler.interpolation == "STEP") return v0;
    return glm::mix(v0, v1, std::clamp(a, 0.0f, 1.0f));
}

glm::vec4 sampleVec4LoopBase(const engine::render::model_types::AnimationSampler& sampler,
                             float t,
                             float duration) {
    if (sampler.inputs.empty() || sampler.outputs.empty()) return glm::vec4(0.0f);
    const auto valueAt = [&](std::size_t index) {
        return sampler.outputs[std::min(index, sampler.outputs.size() - 1u)];
    };
    if (sampler.inputs.size() == 1u || sampler.outputs.size() == 1u) {
        return valueAt(0u);
    }

    const float firstTime = sampler.inputs.front();
    const float lastTime = sampler.inputs.back();
    const glm::vec4 firstValue = valueAt(0u);
    const glm::vec4 lastValue = valueAt(sampler.inputs.size() - 1u);
    const bool outsideKeyRange = t < firstTime || t > lastTime;
    if (outsideKeyRange) {
        if (sampler.interpolation == "STEP") return lastValue;
        const float wrappedSpan = (duration - lastTime) + firstTime;
        if (!(wrappedSpan > 0.0f)) return lastValue;
        const float wrappedT =
            (t >= lastTime) ? (t - lastTime) : (t + duration - lastTime);
        const float a = std::clamp(wrappedT / wrappedSpan, 0.0f, 1.0f);
        return glm::mix(lastValue, firstValue, a);
    }

    const std::size_t i = findKeyframe(sampler.inputs, t);
    if (i >= sampler.inputs.size() - 1u) {
        if (sampler.interpolation == "STEP") return lastValue;
        const float wrappedSpan = (duration - lastTime) + firstTime;
        if (!(wrappedSpan > 0.0f)) return lastValue;
        const float a = std::clamp((t - lastTime) / wrappedSpan, 0.0f, 1.0f);
        return glm::mix(lastValue, firstValue, a);
    }
    const float t0 = sampler.inputs[i];
    const float t1 = sampler.inputs[i + 1u];
    const float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;
    const glm::vec4 v0 = valueAt(i);
    const glm::vec4 v1 = valueAt(i + 1u);
    if (sampler.interpolation == "STEP") return v0;
    return glm::mix(v0, v1, std::clamp(a, 0.0f, 1.0f));
}

} // namespace

glm::vec4 sampleVectorTrack(const engine::render::model_types::AnimationSampler& sampler,
                     float t,
                     float duration,
                     bool loopingClip, float closureWindowSec) {
    if (!loopingClip) {
        return sampleVec4NoLoop(sampler, t);
    }

    const glm::vec4 baseValue = sampleVec4LoopBase(sampler, t, duration);
    const float blendWindowSec = sceneLoopClosureBlendWindowSec(duration, closureWindowSec);
    if (!(blendWindowSec > 0.0f) || !(blendWindowSec < duration)) {
        return baseValue;
    }

    if (t < blendWindowSec) {
        const float alpha = std::clamp(t / blendWindowSec, 0.0f, 1.0f);
        const float endTime = duration - blendWindowSec + t;
        const glm::vec4 endValue = sampleVec4LoopBase(sampler, endTime, duration);
        return glm::mix(endValue, baseValue, alpha);
    }
    if (t > duration - blendWindowSec) {
        const float alpha =
            std::clamp((t - (duration - blendWindowSec)) / blendWindowSec, 0.0f, 1.0f);
        const float startTime = t - (duration - blendWindowSec);
        const glm::vec4 startValue = sampleVec4LoopBase(sampler, startTime, duration);
        return glm::mix(baseValue, startValue, alpha);
    }

    return baseValue;
}

namespace {

glm::quat sampleQuatNoLoop(const engine::render::model_types::AnimationSampler& sampler, float t) {
    if (sampler.inputs.empty() || sampler.outputs.empty()) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const auto quatAt = [&](std::size_t index) {
        const glm::vec4 v = sampler.outputs[std::min(index, sampler.outputs.size() - 1u)];
        return normalizeQuatIfNeeded(glm::quat(v.w, v.x, v.y, v.z));
    };
    if (sampler.inputs.size() == 1u || sampler.outputs.size() == 1u) {
        return quatAt(0u);
    }

    const float firstTime = sampler.inputs.front();
    const float lastTime = sampler.inputs.back();
    const glm::quat lastValue = quatAt(sampler.inputs.size() - 1u);
    if (t <= firstTime) return quatAt(0u);
    if (t >= lastTime) return lastValue;

    const std::size_t i = findKeyframe(sampler.inputs, t);
    if (i >= sampler.inputs.size() - 1u) return lastValue;

    const float t0 = sampler.inputs[i];
    const float t1 = sampler.inputs[i + 1u];
    const float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;
    const glm::quat q0 = quatAt(i);
    const glm::quat q1 = quatAt(i + 1u);
    if (sampler.interpolation == "STEP") return q0;
    return normalizeQuatIfNeeded(glm::slerp(q0, q1, std::clamp(a, 0.0f, 1.0f)));
}

glm::quat sampleQuatLoopBase(const engine::render::model_types::AnimationSampler& sampler,
                             float t,
                             float duration) {
    if (sampler.inputs.empty() || sampler.outputs.empty()) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const auto quatAt = [&](std::size_t index) {
        const glm::vec4 v = sampler.outputs[std::min(index, sampler.outputs.size() - 1u)];
        return normalizeQuatIfNeeded(glm::quat(v.w, v.x, v.y, v.z));
    };
    if (sampler.inputs.size() == 1u || sampler.outputs.size() == 1u) {
        return quatAt(0u);
    }

    const float firstTime = sampler.inputs.front();
    const float lastTime = sampler.inputs.back();
    const glm::quat firstValue = quatAt(0u);
    const glm::quat lastValue = quatAt(sampler.inputs.size() - 1u);
    const bool outsideKeyRange = t < firstTime || t > lastTime;
    if (outsideKeyRange) {
        if (sampler.interpolation == "STEP") return lastValue;
        const float wrappedSpan = (duration - lastTime) + firstTime;
        if (!(wrappedSpan > 0.0f)) return lastValue;
        const float wrappedT =
            (t >= lastTime) ? (t - lastTime) : (t + duration - lastTime);
        const float a = std::clamp(wrappedT / wrappedSpan, 0.0f, 1.0f);
        return normalizeQuatIfNeeded(glm::slerp(lastValue, firstValue, a));
    }

    const std::size_t i = findKeyframe(sampler.inputs, t);
    if (i >= sampler.inputs.size() - 1u) {
        if (sampler.interpolation == "STEP") return lastValue;
        const float wrappedSpan = (duration - lastTime) + firstTime;
        if (!(wrappedSpan > 0.0f)) return lastValue;
        const float a = std::clamp((t - lastTime) / wrappedSpan, 0.0f, 1.0f);
        return normalizeQuatIfNeeded(glm::slerp(lastValue, firstValue, a));
    }

    const float t0 = sampler.inputs[i];
    const float t1 = sampler.inputs[i + 1u];
    const float a = (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;
    const glm::quat q0 = quatAt(i);
    const glm::quat q1 = quatAt(i + 1u);
    if (sampler.interpolation == "STEP") return q0;
    return normalizeQuatIfNeeded(glm::slerp(q0, q1, std::clamp(a, 0.0f, 1.0f)));
}

} // namespace

glm::quat sampleRotationTrack(const engine::render::model_types::AnimationSampler& sampler,
                     float t,
                     float duration,
                     bool loopingClip, float closureWindowSec) {
    if (!loopingClip) {
        return sampleQuatNoLoop(sampler, t);
    }

    const glm::quat baseValue = sampleQuatLoopBase(sampler, t, duration);
    const float blendWindowSec = sceneLoopClosureBlendWindowSec(duration, closureWindowSec);
    if (!(blendWindowSec > 0.0f) || !(blendWindowSec < duration)) {
        return baseValue;
    }

    if (t < blendWindowSec) {
        const float alpha = std::clamp(t / blendWindowSec, 0.0f, 1.0f);
        const float endTime = duration - blendWindowSec + t;
        const glm::quat endValue = sampleQuatLoopBase(sampler, endTime, duration);
        return normalizeQuatIfNeeded(glm::slerp(endValue, baseValue, alpha));
    }
    if (t > duration - blendWindowSec) {
        const float alpha =
            std::clamp((t - (duration - blendWindowSec)) / blendWindowSec, 0.0f, 1.0f);
        const float startTime = t - (duration - blendWindowSec);
        const glm::quat startValue = sampleQuatLoopBase(sampler, startTime, duration);
        return normalizeQuatIfNeeded(glm::slerp(baseValue, startValue, alpha));
    }

    return baseValue;
}

} // namespace engine::render::animation
