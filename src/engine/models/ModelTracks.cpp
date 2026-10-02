#include "engine/models/ModelTracks.h"
#include <algorithm>
#include <cmath>
namespace engine::models {
float sampleMaterialCurve(
    const engine::models::MaterialAnimationCurve& curve,
    float timeSec,
    float fallback,
    bool periodicOffset,
    float sourceFrameRate,
    bool holdSourceFrame) {
    const auto& keys = curve.keys;
    if (keys.empty()) return fallback;
    if (keys.size() == 1u || timeSec <= keys.front().timeSec) {
        return keys.front().value;
    }
    const auto upper = std::upper_bound(
        keys.begin(),
        keys.end(),
        timeSec,
        [](float time,
           const engine::models::MaterialAnimationKey& key) {
            return time < key.timeSec;
        });
    if (upper == keys.end()) return keys.back().value;
    const auto lower = upper - 1;
    // Step tracks retain the previous authored sample.
    if (holdSourceFrame) return lower->value;
    const float span = upper->timeSec - lower->timeSec;
    if (span <= 1e-6f) return upper->value;
    const float alpha = std::clamp(
        (timeSec - lower->timeSec) / span,
        0.0f,
        1.0f);
    float delta = upper->value - lower->value;
    // Preserve the shortest displacement across a single-frame periodic reset.
    const float sourceFrameSeconds =
        sourceFrameRate > 0.0f ? 1.0f / sourceFrameRate : 0.0f;
    const bool sourceFrameReset =
        periodicOffset &&
        sourceFrameSeconds > 0.0f &&
        span <= sourceFrameSeconds * 1.01f &&
        std::abs(delta) > 0.5f;
    if (sourceFrameReset) {
        delta -= std::round(delta);
    }
    return lower->value + delta * alpha;
}

bool sampleContinuousMaterialAnimation(
    const engine::models::MeshData& mesh,
    std::size_t submeshIndex,
    engine::models::MaterialAnimationParameter parameter,
    float materialTimeSec,
    glm::vec4& outValue) {
    const auto track = std::find_if(
        mesh.continuousMaterialAnimations.begin(),
        mesh.continuousMaterialAnimations.end(),
        [&](const auto& candidate) {
            return candidate.submeshIndex == submeshIndex &&
                candidate.parameter == parameter &&
                candidate.durationSec > 0.0f;
        });
    if (track == mesh.continuousMaterialAnimations.end()) {
        return false;
    }
    float sampleTime = std::max(materialTimeSec, 0.0f);
    if (track->loop) {
        sampleTime = std::fmod(sampleTime, track->durationSec);
        if (sampleTime < 0.0f) sampleTime += track->durationSec;
    } else {
        sampleTime = std::min(sampleTime, track->durationSec);
    }
    outValue = track->defaultValue;
    for (std::size_t component = 0u; component < 4u; ++component) {
        outValue[static_cast<glm::length_t>(component)] =
            sampleMaterialCurve(
                track->components[component],
                sampleTime,
                outValue[static_cast<glm::length_t>(component)],
                component >= 2u && parameter != engine::models::MaterialAnimationParameter::EmissiveFactor,
                track->sourceFrameRate,
                false);
    }
    return true;
}

bool sampleClipBoundMaterialAnimation(
    const engine::models::MeshData& mesh,
    int animationIndex,
    std::size_t submeshIndex,
    engine::models::MaterialAnimationParameter parameter,
    float animationTimeSec,
    glm::vec4& outValue) {
    if (animationIndex < 0 ||
        static_cast<std::size_t>(animationIndex) >=
            mesh.animationMaterialParameters.size()) {
        return false;
    }
    const auto& tracks = mesh.animationMaterialParameters[
        static_cast<std::size_t>(animationIndex)];
    const auto track = std::find_if(
        tracks.begin(),
        tracks.end(),
        [&](const auto& candidate) {
            return candidate.submeshIndex == submeshIndex &&
                candidate.parameter == parameter &&
                candidate.durationSec > 0.0f;
        });
    if (track == tracks.end()) return false;

    float sampleTime = std::max(animationTimeSec, 0.0f);
    if (track->loop) {
        sampleTime = std::fmod(sampleTime, track->durationSec);
        if (sampleTime < 0.0f) sampleTime += track->durationSec;
    } else {
        sampleTime = std::min(sampleTime, track->durationSec);
    }
    outValue = track->defaultValue;
    for (std::size_t component = 0u; component < 4u; ++component) {
        outValue[static_cast<glm::length_t>(component)] =
            sampleMaterialCurve(
                track->components[component],
                sampleTime,
                outValue[static_cast<glm::length_t>(component)],
                component >= 2u && parameter != engine::models::MaterialAnimationParameter::EmissiveFactor,
                track->sourceFrameRate,
                track->sampling == engine::models::
                    MaterialAnimationSampling::HoldSourceFrame);
    }
    return true;
}
float sampleVisibilityTrack(const MeshVisibilityTrack& track, float timeSec) {
    if (track.inputs.empty() || track.inputs.size() != track.values.size()) return 1.0f;

        float sampleTimeSec = std::max(timeSec, 0.0f);
        // Visibility keys are authored on integral source frames. A wrapped
        // loop time can land a few ULPs below that frame (for example 70/60
        // modulo a one-second clip becomes just less than 10/60), delaying a
        // step by one runtime tick. Snap only values already within a tiny
        // fraction of a source frame so the authored boundary remains exact
        // without quantizing arbitrary playback times.
        if (track.sourceFrameRate > 0.0f) {
            const float sourceFrame =
                sampleTimeSec * track.sourceFrameRate;
            const float nearestFrame = std::round(sourceFrame);
            if (std::abs(sourceFrame - nearestFrame) <= 1.0e-3f) {
                sampleTimeSec = nearestFrame / track.sourceFrameRate;
            }
        }
        const auto upper = std::upper_bound(
            track.inputs.begin(),
            track.inputs.end(),
            sampleTimeSec);
        const std::size_t valueIndex =
            upper == track.inputs.begin()
                ? 0u
                : static_cast<std::size_t>(
                      std::distance(track.inputs.begin(), upper) - 1);
        return track.values[valueIndex] == 0u ? 0.0f : 1.0f;
}

}
