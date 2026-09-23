#pragma once

#include "ModelPoseSampler.h"

#include <string>
#include <vector>

namespace engine::render::animation {

// A full-body crossfade followed by optional masked override layers. Times are
// seconds on each clip's own timeline; the shared sampler owns loop behavior.
struct PoseLayer {
    int clipIndex = -1;
    float timeSec = 0.0f;
    float weight = 1.0f;
    // Empty means all nodes. Each listed node and its children must be named
    // explicitly if the intended mask includes a subtree.
    std::vector<int> nodeMask;
};

struct PoseBlendRequest {
    int baseClipIndex = -1;
    float baseTimeSec = 0.0f;
    int targetClipIndex = -1; // -1 disables the crossfade.
    float targetTimeSec = 0.0f;
    float crossfadeWeight = 0.0f;
    std::vector<PoseLayer> layers;
};

// Transactional: outputs are unchanged on invalid input or an unsupported
// matrix/TRS interpolation. Exact matrix-authored transforms pass through.
bool blendPose(const PoseSource& source,
               const PoseBlendRequest& request,
               std::vector<model_types::NodeTRS>& outLocal,
               std::vector<glm::mat4>& outGlobal,
               std::string* error = nullptr);

struct TimedAnimationEvent {
    std::string name;
    float timeSec = 0.0f; // [0, clip duration)
};

struct FiredAnimationEvent {
    std::string name;
    double absoluteTimeSec = 0.0;
};

// Collect events in (fromTimeSec, toTimeSec], including loop crossings, in
// chronological order. includeStart fires a marker exactly at fromTimeSec on
// activation; looping=false is for a one-shot clip and never repeats markers.
// A seek/cancel resets the caller's cursor; it does not replay skipped events.
bool collectAnimationEvents(const std::vector<TimedAnimationEvent>& events,
                            float clipDurationSec,
                            double fromTimeSec,
                            double toTimeSec,
                            std::vector<FiredAnimationEvent>& out,
                            std::string* error = nullptr,
                            std::size_t maxEvents = 256u,
                            bool includeStart = false,
                            bool looping = true);

} // namespace engine::render::animation
