#include "AnimationBlend.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <glm/gtx/quaternion.hpp>

namespace engine::render::animation {
namespace {

bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool clipValid(const PoseSource& source, int index) {
    return index == -1 ||
           (index >= 0 && static_cast<std::size_t>(index) < source.animations.size());
}

bool finite(const glm::mat4& matrix) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(matrix[c][r])) return false;
    return true;
}

bool mixLocal(const model_types::NodeTRS& a,
              const model_types::NodeTRS& b,
              float weight,
              model_types::NodeTRS& out) {
    if (weight <= 0.0f) { out = a; return true; }
    if (weight >= 1.0f) { out = b; return true; }
    if (a.hasMatrix || b.hasMatrix) {
        if (!a.hasMatrix || !b.hasMatrix || a.matrix != b.matrix) return false;
        out = a;
        return true;
    }
    out = a;
    out.t = glm::mix(a.t, b.t, weight);
    out.r = glm::normalize(glm::slerp(a.r, b.r, weight));
    out.s = glm::mix(a.s, b.s, weight);
    return true;
}

} // namespace

bool blendPose(const PoseSource& source,
               const PoseBlendRequest& request,
               std::vector<model_types::NodeTRS>& outLocal,
               std::vector<glm::mat4>& outGlobal,
               std::string* error) {
    if (!clipValid(source, request.baseClipIndex) ||
        !clipValid(source, request.targetClipIndex))
        return fail(error, "animation blend clip index is out of range");
    if (!std::isfinite(request.baseTimeSec) ||
        !std::isfinite(request.targetTimeSec) ||
        !std::isfinite(request.crossfadeWeight) ||
        request.crossfadeWeight < 0.0f || request.crossfadeWeight > 1.0f)
        return fail(error, "animation blend time or weight is invalid");
    if (request.targetClipIndex == -1 && request.crossfadeWeight != 0.0f)
        return fail(error, "crossfade weight requires a target clip");
    for (const auto& layer : request.layers) {
        if (!clipValid(source, layer.clipIndex) ||
            !std::isfinite(layer.timeSec) || !std::isfinite(layer.weight) ||
            layer.weight < 0.0f || layer.weight > 1.0f)
            return fail(error, "animation layer clip, time or weight is invalid");
        for (int node : layer.nodeMask)
            if (node < 0 || static_cast<std::size_t>(node) >= source.nodesDefault.size())
                return fail(error, "animation layer node mask is out of range");
    }

    std::vector<model_types::NodeTRS> local;
    std::vector<glm::mat4> globals;
    samplePose(source, request.baseTimeSec, request.baseClipIndex, local, globals);
    const auto apply = [&](int clip, float time, float weight,
                           const std::vector<int>* mask) {
        if (weight == 0.0f) return true;
        std::vector<model_types::NodeTRS> sampled;
        std::vector<glm::mat4> ignored;
        samplePose(source, time, clip, sampled, ignored);
        for (std::size_t node = 0; node < local.size(); ++node) {
            if (mask && !mask->empty() &&
                std::find(mask->begin(), mask->end(), static_cast<int>(node)) == mask->end())
                continue;
            model_types::NodeTRS mixed;
            if (!mixLocal(local[node], sampled[node], weight, mixed)) return false;
            local[node] = mixed;
        }
        return true;
    };
    if (request.targetClipIndex != -1 &&
        !apply(request.targetClipIndex, request.targetTimeSec,
               request.crossfadeWeight, nullptr))
        return fail(error, "cannot interpolate differing matrix-authored node transforms");
    for (const auto& layer : request.layers)
        if (!apply(layer.clipIndex, layer.timeSec, layer.weight, &layer.nodeMask))
            return fail(error, "cannot interpolate differing matrix-authored node transforms");
    composePose(source, local, globals);
    for (const auto& matrix : globals)
        if (!finite(matrix)) return fail(error, "blended node transform is not finite");
    outLocal = std::move(local);
    outGlobal = std::move(globals);
    if (error) error->clear();
    return true;
}

bool collectAnimationEvents(const std::vector<TimedAnimationEvent>& events,
                            float clipDurationSec,
                            double fromTimeSec,
                            double toTimeSec,
                            std::vector<FiredAnimationEvent>& out,
                            std::string* error,
                            std::size_t maxEvents,
                            bool includeStart,
                            bool looping) {
    if (!std::isfinite(clipDurationSec) || clipDurationSec <= 0.0f ||
        !std::isfinite(fromTimeSec) || !std::isfinite(toTimeSec) ||
        fromTimeSec < 0.0 || toTimeSec < fromTimeSec)
        return fail(error, "animation event interval or clip duration is invalid");
    for (const auto& event : events)
        if (event.name.empty() || !std::isfinite(event.timeSec) ||
            event.timeSec < 0.0f || event.timeSec >= clipDurationSec)
            return fail(error, "animation event marker is invalid");
    std::vector<FiredAnimationEvent> fired;
    const double duration = clipDurationSec;
    for (const auto& event : events) {
        double cycle = std::floor((fromTimeSec - event.timeSec) / duration);
        for (;;) {
            const double at = cycle * duration + event.timeSec;
            if (!std::isfinite(at))
                return fail(error, "animation event timeline exceeds numeric range");
            if (at > toTimeSec || (!looping && cycle > 0.0)) break;
            if (at >= 0.0 && (at > fromTimeSec ||
                              (includeStart && at == fromTimeSec))) {
                if (fired.size() >= maxEvents)
                    return fail(error, "animation event catch-up exceeds the limit");
                fired.push_back({event.name, at});
            }
            const double nextCycle = cycle + 1.0;
            if (nextCycle == cycle)
                return fail(error, "animation event timeline has insufficient precision");
            cycle = nextCycle;
        }
    }
    std::stable_sort(fired.begin(), fired.end(),
                     [](const auto& a, const auto& b) {
                         return a.absoluteTimeSec < b.absoluteTimeSec;
                     });
    out = std::move(fired);
    if (error) error->clear();
    return true;
}

} // namespace engine::render::animation
