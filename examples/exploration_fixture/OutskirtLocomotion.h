#pragma once

#include "engine/render/AnimationBlend.h"
#include <algorithm>

namespace engine::exploration_fixture {
// Forward-stick thresholds recovered from Colosseum's hero_move/people routines.
inline float outskirtMotionAmount(float stickMagnitude) {
    return 2.0f * std::clamp(stickMagnitude, 0.0f, 1.0f);
}
inline float outskirtDesiredSpeed(float amount, float walkSpeed, float runSpeed) {
    amount = std::clamp(amount, 0.0f, 2.0f);
    if (amount <= .4f) return amount / .4f * walkSpeed * .5f;
    if (amount <= 1.0f) return walkSpeed * (.5f + (amount - .4f) / .6f * .5f);
    return walkSpeed + (amount - 1.0f) * (runSpeed - walkSpeed);
}
inline float outskirtAnimationAmount(float actualSpeed, float requestedAmount,
                                     float walkSpeed, float runSpeed) {
    const float measured = actualSpeed <= walkSpeed ? actualSpeed / walkSpeed :
        1.0f + (actualSpeed - walkSpeed) / (runSpeed - walkSpeed);
    // The retail leader keeps a small walk pose when collision blocks motion.
    return std::clamp(measured < .2f ? std::min(requestedAmount, .2f) : measured, 0.0f, 2.0f);
}
inline render::animation::PoseBlendRequest outskirtLocomotionPose(float amount,
        float cyclePhase, float idleDuration, float walkDuration, float runDuration) {
    render::animation::PoseBlendRequest pose;
    const float phase = cyclePhase - static_cast<int>(cyclePhase);
    if (amount <= 0.0f) {
        pose.baseClipIndex = 1;
        pose.baseTimeSec = phase * idleDuration;
    } else if (amount < .4f) {
        pose.baseClipIndex = 1;
        pose.baseTimeSec = phase * idleDuration;
        pose.targetClipIndex = 2;
        pose.targetTimeSec = phase * walkDuration;
        pose.crossfadeWeight = amount / .4f;
    } else if (amount <= 1.0f) {
        pose.baseClipIndex = 2;
        pose.baseTimeSec = phase * walkDuration;
    } else {
        pose.baseClipIndex = 2;
        pose.baseTimeSec = phase * walkDuration;
        pose.targetClipIndex = 3;
        pose.targetTimeSec = phase * runDuration;
        pose.crossfadeWeight = std::min(amount - 1.0f, 1.0f);
    }
    return pose;
}
} // namespace engine::exploration_fixture
