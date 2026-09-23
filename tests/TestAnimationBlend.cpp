#include "engine/render/AnimationBlend.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace {
using namespace engine::render::animation;
using namespace engine::render::model_types;

bool near(float a, float b) { return std::abs(a - b) < 1.0e-4f; }

AnimationClip translationClip(int node, float target) {
    AnimationClip clip;
    clip.durationSec = 2.0f;
    AnimationSampler sampler;
    sampler.inputs = {0.0f, 2.0f};
    sampler.outputs = {glm::vec4(0.0f), glm::vec4(target, 0.0f, 0.0f, 0.0f)};
    sampler.interpolation = "LINEAR";
    clip.samplers.push_back(sampler);
    clip.channels.push_back({0, node, ChannelPath::Translation});
    return clip;
}

} // namespace

bool test_animation_blend_contract(std::string& failure) {
    std::vector<NodeTRS> nodes(2);
    nodes[1].t.y = 1.0f;
    std::vector<std::vector<int>> children{{1}, {}};
    std::vector<int> roots{0};
    std::vector<SkinData> skins;
    std::vector<AnimationClip> clips{
        translationClip(0, 4.0f), translationClip(0, 8.0f),
        translationClip(1, 6.0f)};
    std::vector<std::string> names{"root", "child"};
    PoseSource source{nodes, children, roots, skins, clips, names};
    PoseBlendRequest request;
    request.baseClipIndex = 0;
    request.baseTimeSec = 1.0f;
    request.targetClipIndex = 1;
    request.targetTimeSec = 1.0f;
    request.crossfadeWeight = 0.5f;
    request.layers.push_back({2, 1.0f, 0.5f, {1}});
    std::vector<NodeTRS> local;
    std::vector<glm::mat4> global;
    std::string error;
    if (!blendPose(source, request, local, global, &error)) {
        failure = "valid synthetic blend failed: " + error;
        return false;
    }
    if (local.size() != 2 || !near(local[0].t.x, 3.0f) ||
        !near(local[1].t.x, 1.5f) || !near(local[1].t.y, 0.5f) ||
        !near(global[1][3].x, 4.5f) || !near(global[1][3].y, 0.5f)) {
        failure = "crossfade, masked layer or hierarchy produced the wrong transform";
        return false;
    }
    request.crossfadeWeight = 0.0f;
    request.layers.clear();
    if (!blendPose(source, request, local, global, &error) ||
        !near(local[0].t.x, 2.0f)) {
        failure = "zero-weight crossfade did not preserve the base clip";
        return false;
    }
    const auto prior = local;
    request.crossfadeWeight = std::numeric_limits<float>::quiet_NaN();
    if (blendPose(source, request, local, global, &error) || error.empty() ||
        !near(local[0].t.x, prior[0].t.x)) {
        failure = "invalid weight did not fail transactionally";
        return false;
    }
    request.crossfadeWeight = 0.5f;
    request.layers.push_back({2, 1.0f, 0.5f, {3}});
    if (blendPose(source, request, local, global, &error) || error.empty()) {
        failure = "out-of-range mask was accepted";
        return false;
    }
    request.layers.clear();
    nodes[0].hasMatrix = true;
    nodes[0].matrix = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f));
    request.baseClipIndex = -1;
    if (blendPose(source, request, local, global, &error) || error.empty()) {
        failure = "interpolation between differing matrix and TRS nodes was accepted";
        return false;
    }

    const std::vector<TimedAnimationEvent> markers{{"impact", 0.0f}, {"step", 0.5f}};
    std::vector<FiredAnimationEvent> fired;
    if (!collectAnimationEvents(markers, 2.0f, 0.25, 4.5, fired, &error) ||
        fired.size() != 5 || fired[0].name != "step" ||
        fired[1].name != "impact" || fired[2].name != "step" ||
        fired[3].name != "impact" || fired[4].name != "step" ||
        fired[0].absoluteTimeSec != 0.5 ||
        fired[3].absoluteTimeSec != 4.0) {
        failure = "event loop crossings or ordering were wrong: " + error;
        return false;
    }
    if (!collectAnimationEvents(markers, 2.0f, 0.5, 0.5, fired, &error) ||
        !fired.empty()) {
        failure = "zero-length event interval replayed a marker";
        return false;
    }
    if (collectAnimationEvents(markers, 2.0f, 0.0, 10.0, fired,
                               &error, 2u) || error.empty() || !fired.empty()) {
        failure = "event catch-up limit did not fail transactionally";
        return false;
    }
    if (!collectAnimationEvents(markers, 2.0f, 0.0, 4.5, fired,
                                &error, 256u, true, false) ||
        fired.size() != 2 || fired[0].name != "impact" ||
        fired[0].absoluteTimeSec != 0.0 || fired[1].name != "step" ||
        fired[1].absoluteTimeSec != 0.5) {
        failure = "one-shot activation did not fire the start marker exactly once";
        return false;
    }
    failure.clear();
    return true;
}
