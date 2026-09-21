// CPU contracts for the shared pose sampler. Every expectation below is
// authored independently of the implementation: transforms are composed by
// hand from the documented engine policy, and the sampler is exercised through
// its public interface only. No graphics context, Model instance or duplicated
// evaluator is involved.

#include "engine/render/ModelPoseSampler.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace {

using engine::render::animation::PoseSource;
using engine::render::animation::samplePose;
using engine::render::animation::trsToMat4;
using engine::render::model_types::AnimationChannel;
using engine::render::model_types::AnimationClip;
using engine::render::model_types::AnimationSampler;
using engine::render::model_types::ChannelPath;
using engine::render::model_types::NodeTRS;
using engine::render::model_types::SkinData;

constexpr float kEpsilon = 1.0e-5f;

bool near(float actual, float expected, float epsilon = kEpsilon) {
    return std::abs(actual - expected) <= epsilon;
}

bool nearVec3(const glm::vec3& actual, const glm::vec3& expected) {
    return near(actual.x, expected.x) && near(actual.y, expected.y) &&
        near(actual.z, expected.z);
}

bool nearQuat(const glm::quat& actual, const glm::quat& expected) {
    // Quaternion equality is sign-agnostic: q and -q are the same rotation.
    const float dot =
        actual.w * expected.w + actual.x * expected.x + actual.y * expected.y +
        actual.z * expected.z;
    if (near(std::abs(dot), 1.0f, 1.0e-4f)) {
        return true;
    }
    return near(glm::length(actual - expected), 0.0f, 1.0e-4f);
}

bool nearMat4(const glm::mat4& actual, const glm::mat4& expected) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!near(actual[column][row], expected[column][row])) {
                return false;
            }
        }
    }
    return true;
}

glm::vec3 transformPoint(const glm::mat4& matrix, const glm::vec3& point) {
    return glm::vec3(matrix * glm::vec4(point, 1.0f));
}

AnimationSampler vecSampler(std::vector<float> inputs,
                            std::vector<glm::vec4> outputs,
                            const char* interpolation) {
    AnimationSampler sampler;
    sampler.inputs = std::move(inputs);
    sampler.outputs = std::move(outputs);
    sampler.interpolation = interpolation;
    sampler.isVec4 = true;
    return sampler;
}

AnimationSampler cubicSampler(std::vector<float> inputs,
                              std::vector<glm::vec4> outputs,
                              bool isVec4) {
    AnimationSampler sampler;
    sampler.inputs = std::move(inputs);
    sampler.outputs = std::move(outputs);
    sampler.interpolation = "CUBICSPLINE";
    sampler.isVec4 = isVec4;
    return sampler;
}

AnimationClip clipOf(float duration, std::vector<AnimationSampler> samplers,
                     std::vector<AnimationChannel> channels) {
    AnimationClip clip;
    clip.name = "clip";
    clip.durationSec = duration;
    clip.samplers = std::move(samplers);
    clip.channels = std::move(channels);
    return clip;
}

// A single node with default transform, no children and no skins.
struct SingleNodeSource {
    std::vector<NodeTRS> nodes;
    std::vector<std::vector<int>> children;
    std::vector<int> roots;
    std::vector<SkinData> skins;
    std::vector<AnimationClip> animations;
    std::vector<std::string> names;

    PoseSource source() const {
        return PoseSource{nodes, children, roots, skins, animations, names};
    }
};

using Check = bool (*)(std::string&);

bool checkEmptyInputsReplaceOutputs(std::string& fail) {
    SingleNodeSource data;
    std::vector<NodeTRS> locals(3);
    std::vector<glm::mat4> globals(3, glm::mat4(2.0f));
    data.animations.push_back(clipOf(2.0f, {}, {}));
    samplePose(data.source(), -3.0f, 0, locals, globals);
    if (!locals.empty()) {
        fail = "empty node list did not clear the local pose output";
        return false;
    }
    if (!globals.empty()) {
        fail = "empty node list did not clear the global pose output";
        return false;
    }
    return true;
}

bool checkDefaultsAndInvalidClip(std::string& fail) {
    SingleNodeSource data;
    NodeTRS node;
    node.t = glm::vec3(1.0f, 2.0f, 3.0f);
    node.r = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    node.s = glm::vec3(2.0f, 3.0f, 4.0f);
    data.nodes.push_back(node);
    data.children.push_back({});
    data.animations.push_back(clipOf(1.0f, {}, {}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    // Out-of-range positive index and any negative index both leave defaults.
    samplePose(data.source(), 0.4f, 7, locals, globals);
    if (locals.size() != 1u || globals.size() != 1u) {
        fail = "invalid clip index changed the pose output size";
        return false;
    }
    if (!nearVec3(locals[0].t, node.t) || !nearVec3(locals[0].s, node.s) ||
        !nearQuat(locals[0].r, node.r) || locals[0].hasMatrix) {
        fail = "invalid positive clip index did not leave the default pose";
        return false;
    }
    if (!nearMat4(globals[0], trsToMat4(node))) {
        fail = "default pose global matrix does not match the authored transform";
        return false;
    }

    samplePose(data.source(), 0.4f, -1, locals, globals);
    if (!nearVec3(locals[0].t, node.t) || !nearMat4(globals[0], trsToMat4(node))) {
        fail = "negative clip index did not leave the default pose";
        return false;
    }
    return true;
}

bool checkLinearTranslationAndLooping(std::string& fail) {
    SingleNodeSource data;
    data.nodes.push_back(NodeTRS{});
    data.children.push_back({});
    // 0s -> origin, 2s -> (4, 2, -2); LINEAR halfway is exactly the midpoint.
    data.animations.push_back(clipOf(
        2.0f,
        {vecSampler({0.0f, 2.0f},
                    {glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
                     glm::vec4(4.0f, 2.0f, -2.0f, 0.0f)},
                    "LINEAR")},
        {AnimationChannel{0, 0, ChannelPath::Translation}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    samplePose(data.source(), 1.0f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(2.0f, 1.0f, -1.0f))) {
        fail = "LINEAR translation did not interpolate linearly at the midpoint";
        return false;
    }
    samplePose(data.source(), 0.5f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(1.0f, 0.5f, -0.5f))) {
        fail = "LINEAR translation did not interpolate linearly at a quarter key";
        return false;
    }
    // Positive time beyond the duration loops; 3s is one full period plus 1s.
    samplePose(data.source(), 3.0f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(2.0f, 1.0f, -1.0f))) {
        fail = "looping positive time did not wrap to the same sample";
        return false;
    }
    // Negative time wraps forward: -1s is equivalent to 1s for a 2s clip.
    samplePose(data.source(), -1.0f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(2.0f, 1.0f, -1.0f))) {
        fail = "negative time did not wrap forward into the clip range";
        return false;
    }
    samplePose(data.source(), -0.5f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(3.0f, 1.5f, -1.5f))) {
        fail = "negative fractional time did not wrap to the expected sample";
        return false;
    }
    return true;
}

bool checkStepTranslation(std::string& fail) {
    SingleNodeSource data;
    data.nodes.push_back(NodeTRS{});
    data.children.push_back({});
    data.animations.push_back(clipOf(
        2.0f,
        {vecSampler({0.0f, 1.0f},
                    {glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
                     glm::vec4(5.0f, 0.0f, 0.0f, 0.0f)},
                    "STEP")},
        {AnimationChannel{0, 0, ChannelPath::Translation}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    // Inside the key range STEP holds the earlier key.
    samplePose(data.source(), 0.5f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(0.0f, 0.0f, 0.0f))) {
        fail = "STEP translation did not hold the earlier key inside the range";
        return false;
    }
    // Past the last key STEP holds the last key instead of wrapping.
    samplePose(data.source(), 1.5f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(5.0f, 0.0f, 0.0f))) {
        fail = "STEP translation did not hold the last key beyond the range";
        return false;
    }
    return true;
}

bool checkLinearRotationNormalized(std::string& fail) {
    SingleNodeSource data;
    data.nodes.push_back(NodeTRS{});
    data.children.push_back({});
    // Unnormalized quaternions in the engine's stored (x, y, z, w) order:
    // identity and a 90 degree Z rotation.
    data.animations.push_back(clipOf(
        2.0f,
        {vecSampler({0.0f, 2.0f},
                    {glm::vec4(0.0f, 0.0f, 0.0f, 4.0f),
                     glm::vec4(0.0f, 0.0f, 2.0f, 2.0f)},
                    "LINEAR")},
        {AnimationChannel{0, 0, ChannelPath::Rotation}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    samplePose(data.source(), 1.0f, 0, locals, globals);
    if (!near(glm::length(locals[0].r), 1.0f, 1.0e-4f)) {
        fail = "rotation sampling did not normalize the output quaternion";
        return false;
    }
    const glm::vec3 rotated = locals[0].r * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 expected(
        std::cos(glm::radians(45.0f)), std::sin(glm::radians(45.0f)), 0.0f);
    if (!nearVec3(rotated, expected)) {
        fail = "normalized quaternion interpolation did not produce a 45 degree rotation";
        return false;
    }
    return true;
}

bool checkStepRotationAndScale(std::string& fail) {
    SingleNodeSource data;
    data.nodes.push_back(NodeTRS{});
    data.children.push_back({});
    data.animations.push_back(clipOf(
        2.0f,
        {vecSampler({0.0f, 1.0f},
                    {glm::vec4(0.0f, 0.0f, 0.0f, 4.0f),
                     glm::vec4(0.0f, 0.0f, 2.0f, 2.0f)},
                    "STEP"),
         vecSampler({0.0f, 1.0f},
                    {glm::vec4(2.0f, 3.0f, 4.0f, 0.0f),
                     glm::vec4(9.0f, 9.0f, 9.0f, 0.0f)},
                    "STEP")},
        {AnimationChannel{0, 0, ChannelPath::Rotation},
         AnimationChannel{1, 0, ChannelPath::Scale}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    samplePose(data.source(), 0.5f, 0, locals, globals);
    if (!nearQuat(locals[0].r, glm::quat(1.0f, 0.0f, 0.0f, 0.0f))) {
        fail = "STEP rotation did not hold the normalized earlier key";
        return false;
    }
    if (!nearVec3(locals[0].s, glm::vec3(2.0f, 3.0f, 4.0f))) {
        fail = "STEP scale did not hold the earlier key inside the range";
        return false;
    }
    return true;
}

bool checkLinearScaleInterpolation(std::string& fail) {
    SingleNodeSource data;
    data.nodes.push_back(NodeTRS{});
    data.children.push_back({});
    // Nonuniform scale keys: 0s -> (1, 2, 3), 2s -> (3, 4, 6). LINEAR halfway is
    // exactly the per-axis midpoint (2, 3, 4.5).
    data.animations.push_back(clipOf(
        2.0f,
        {vecSampler({0.0f, 2.0f},
                    {glm::vec4(1.0f, 2.0f, 3.0f, 0.0f),
                     glm::vec4(3.0f, 4.0f, 6.0f, 0.0f)},
                    "LINEAR")},
        {AnimationChannel{0, 0, ChannelPath::Scale}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    samplePose(data.source(), 1.0f, 0, locals, globals);
    if (!nearVec3(locals[0].s, glm::vec3(2.0f, 3.0f, 4.5f))) {
        fail = "LINEAR scale did not interpolate each axis to its literal midpoint";
        return false;
    }
    if (!nearMat4(
            globals[0],
            glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.5f)))) {
        fail = "LINEAR scale did not compose into the node's global matrix";
        return false;
    }
    // A unit cube corner transformed by the sampled scale.
    if (!nearVec3(
            transformPoint(globals[0], glm::vec3(1.0f, 1.0f, 1.0f)),
            glm::vec3(2.0f, 3.0f, 4.5f))) {
        fail = "the linearly scaled node did not transform a point as expected";
        return false;
    }
    samplePose(data.source(), 0.5f, 0, locals, globals);
    if (!nearVec3(locals[0].s, glm::vec3(1.5f, 2.5f, 3.75f))) {
        fail = "LINEAR scale did not interpolate at a quarter key";
        return false;
    }
    return true;
}

bool checkOneKeyChannel(std::string& fail) {
    SingleNodeSource data;
    data.nodes.push_back(NodeTRS{});
    data.children.push_back({});
    // A sampler with a single key holds that value for every time, including
    // times outside its (degenerate) key range and any interpolation mode.
    data.animations.push_back(clipOf(
        3.0f,
        {vecSampler({0.5f}, {glm::vec4(3.0f, 4.0f, 5.0f, 0.0f)}, "LINEAR")},
        {AnimationChannel{0, 0, ChannelPath::Translation}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    for (const float time : {0.0f, 0.5f, 1.7f, -2.0f}) {
        samplePose(data.source(), time, 0, locals, globals);
        if (!nearVec3(locals[0].t, glm::vec3(3.0f, 4.0f, 5.0f))) {
            fail = "a one-key channel did not hold its single value";
            return false;
        }
    }

    SingleNodeSource stepData;
    stepData.nodes.push_back(NodeTRS{});
    stepData.children.push_back({});
    stepData.animations.push_back(clipOf(
        1.0f,
        {vecSampler({0.0f}, {glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)}, "STEP")},
        {AnimationChannel{0, 0, ChannelPath::Translation}}));
    samplePose(stepData.source(), 0.75f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(1.0f, 0.0f, 0.0f))) {
        fail = "a one-key STEP channel did not hold its single value";
        return false;
    }
    return true;
}

bool checkExactPositiveEndpointPolicy(std::string& fail) {
    SingleNodeSource data;
    data.nodes.push_back(NodeTRS{});
    data.children.push_back({});
    data.animations.push_back(clipOf(
        2.0f,
        {vecSampler({0.0f, 2.0f},
                    {glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
                     glm::vec4(4.0f, 0.0f, 0.0f, 0.0f)},
                    "LINEAR")},
        {AnimationChannel{0, 0, ChannelPath::Translation}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    // Engine policy, not a universal glTF rule: a positive exact multiple of
    // the duration resolves to just before the end, so a LINEAR channel returns
    // its last key rather than wrapping back to the first key.
    samplePose(data.source(), 2.0f, 0, locals, globals);
    if (!near(locals[0].t.x, 4.0f, 1.0e-3f)) {
        fail = "sampling exactly at the positive duration did not return the last key";
        return false;
    }
    samplePose(data.source(), 4.0f, 0, locals, globals);
    if (!near(locals[0].t.x, 4.0f, 1.0e-3f)) {
        fail = "sampling at two whole durations did not return the last key";
        return false;
    }
    // Negative whole-duration time lands on the first key, not on the last one.
    samplePose(data.source(), -2.0f, 0, locals, globals);
    if (!near(locals[0].t.x, 0.0f, 1.0e-3f)) {
        fail = "negative whole-duration time did not wrap to the first key";
        return false;
    }
    // Negative fractional time wraps forward from the same anchor.
    samplePose(data.source(), -1.0f, 0, locals, globals);
    if (!near(locals[0].t.x, 2.0f, 1.0e-3f)) {
        fail = "negative fractional time did not wrap to the midpoint";
        return false;
    }
    samplePose(data.source(), 0.0f, 0, locals, globals);
    if (!near(locals[0].t.x, 0.0f, 1.0e-3f)) {
        fail = "sampling at zero did not return the first key";
        return false;
    }
    return true;
}

bool checkChannelShorterThanClip(std::string& fail) {
    SingleNodeSource data;
    data.nodes.push_back(NodeTRS{});
    data.children.push_back({});
    // The clip lasts 4s but this channel only has keys in [0, 1]. Past the last
    // key the engine wraps the sampled time into the remaining tail span and
    // interpolates from the last key back to the first.
    data.animations.push_back(clipOf(
        4.0f,
        {vecSampler({0.0f, 1.0f},
                    {glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
                     glm::vec4(2.0f, 0.0f, 0.0f, 0.0f)},
                    "LINEAR")},
        {AnimationChannel{0, 0, ChannelPath::Translation}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    // t = 2 is 1s past the last key; the tail span is (4 - 1) + 0 = 3s, so the
    // alpha is 1/3 from the last key back towards the first.
    samplePose(data.source(), 2.0f, 0, locals, globals);
    if (!near(locals[0].t.x, 2.0f - 2.0f / 3.0f)) {
        fail = "a channel shorter than the clip did not interpolate through the tail span";
        return false;
    }
    // Halfway through the tail span: alpha = 1.5 / 3 = 0.5.
    samplePose(data.source(), 2.5f, 0, locals, globals);
    if (!near(locals[0].t.x, 1.0f)) {
        fail = "the tail span of a short channel did not reach its midpoint";
        return false;
    }
    // Just before the clip wraps the value is close to the first key again.
    samplePose(data.source(), 3.999f, 0, locals, globals);
    if (!near(locals[0].t.x, 0.0f, 1.0e-2f)) {
        fail = "the tail span of a short channel did not approach the first key";
        return false;
    }
    return true;
}

bool checkHierarchyTransforms(std::string& fail) {
    SingleNodeSource data;
    NodeTRS root;
    root.t = glm::vec3(1.0f, 0.0f, 0.0f);
    NodeTRS middle;
    middle.t = glm::vec3(0.0f, 2.0f, 0.0f);
    NodeTRS leaf;
    leaf.t = glm::vec3(0.0f, 0.0f, 1.0f);
    leaf.r = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    data.nodes = {root, middle, leaf};
    data.children = {{1}, {2}, {}};
    data.names = {"root", "middle", "leaf"};
    data.animations.push_back(clipOf(
        1.0f,
        {vecSampler({0.0f, 1.0f},
                    {glm::vec4(0.0f, 2.0f, 0.0f, 0.0f),
                     glm::vec4(2.0f, 0.0f, 0.0f, 0.0f)},
                    "LINEAR")},
        {AnimationChannel{0, 1, ChannelPath::Translation}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    samplePose(data.source(), 1.0f, 0, locals, globals);
    if (!nearMat4(globals[0], glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)))) {
        fail = "scene root global transform is wrong";
        return false;
    }
    if (!nearMat4(globals[1], glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f)))) {
        fail = "animated child global transform did not use the sampled local translation";
        return false;
    }
    const glm::mat4 expectedLeaf =
        glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f)) *
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 1.0f)) *
        glm::toMat4(leaf.r);
    if (!nearMat4(globals[2], expectedLeaf)) {
        fail = "grandchild global transform did not compose its parent chain";
        return false;
    }
    if (!nearVec3(transformPoint(globals[2], glm::vec3(0.0f)), glm::vec3(3.0f, 0.0f, 1.0f))) {
        fail = "grandchild origin did not land at the composed hierarchy position";
        return false;
    }

    // With explicit scene roots the parentless helper node is not visited and
    // keeps the identity global the sampler initializes.
    data.roots = {1};
    samplePose(data.source(), 1.0f, 0, locals, globals);
    if (!nearMat4(globals[0], glm::mat4(1.0f))) {
        fail = "a node outside every scene root was not left with an identity global";
        return false;
    }
    if (!nearMat4(globals[1], glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)))) {
        fail = "an explicit scene root did not become its own world root";
        return false;
    }
    return true;
}

bool checkOutputReplacement(std::string& fail) {
    SingleNodeSource data;
    NodeTRS node;
    node.t = glm::vec3(0.0f, 1.0f, 0.0f);
    data.nodes.push_back(node);
    data.children.push_back({});
    data.animations.push_back(clipOf(
        1.0f,
        {vecSampler({0.0f, 1.0f},
                    {glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
                     glm::vec4(1.0f, 3.0f, 0.0f, 0.0f)},
                    "LINEAR")},
        {AnimationChannel{0, 0, ChannelPath::Translation}}));

    // Prefilled outputs of the wrong size and content must be replaced, not
    // appended to or partially overwritten.
    std::vector<NodeTRS> locals(5);
    std::vector<glm::mat4> globals(5, glm::mat4(7.0f));
    locals[0].t = glm::vec3(99.0f);
    samplePose(data.source(), 0.5f, 0, locals, globals);
    if (locals.size() != 1u || globals.size() != 1u) {
        fail = "prefilled outputs were not resized to the node count";
        return false;
    }
    if (!nearVec3(locals[0].t, glm::vec3(0.5f, 2.0f, 0.0f))) {
        fail = "prefilled local output was not fully replaced";
        return false;
    }
    if (!nearMat4(globals[0], glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 2.0f, 0.0f)))) {
        fail = "prefilled global output was not fully replaced";
        return false;
    }
    return true;
}

// Shared skeleton: root joint 0 -> joint 1, with a separate single-joint skin
// on node 2. Used to show which nodes the root-motion carrier rule selects.
struct RootMotionSource {
    SingleNodeSource data;

    RootMotionSource() {
        NodeTRS root;
        root.t = glm::vec3(10.0f, 0.0f, 10.0f);
        NodeTRS tip;
        tip.t = glm::vec3(1.0f, 0.0f, 0.0f);
        NodeTRS extra;
        extra.t = glm::vec3(0.0f, 0.0f, 0.0f);
        data.nodes = {root, tip, extra};
        data.children = {{1}, {}, {}};
        data.names = {"root", "tip", "extra"};
        SkinData skin;
        skin.joints = {0, 1};
        skin.inverseBind = {glm::mat4(1.0f), glm::mat4(1.0f)};
        SkinData extraSkin;
        extraSkin.joints = {2};
        extraSkin.inverseBind = {glm::mat4(1.0f)};
        data.skins = {skin, extraSkin};
        // One clip animates all three nodes to a constant translated value.
        data.animations.push_back(clipOf(
            2.0f,
            {AnimationSampler{},
             AnimationSampler{},
             AnimationSampler{}},
            {AnimationChannel{0, 0, ChannelPath::Translation},
             AnimationChannel{1, 1, ChannelPath::Translation},
             AnimationChannel{2, 2, ChannelPath::Translation}}));
        setConstant(data.animations.back().samplers[0], glm::vec4(12.0f, 5.0f, -7.0f, 0.0f));
        setConstant(data.animations.back().samplers[1], glm::vec4(3.0f, 4.0f, 5.0f, 0.0f));
        setConstant(data.animations.back().samplers[2], glm::vec4(-4.0f, 1.0f, 8.0f, 0.0f));
    }

    static void setConstant(AnimationSampler& sampler, const glm::vec4& value) {
        sampler.inputs = {0.0f, 2.0f};
        sampler.outputs = {value, value};
        sampler.interpolation = "LINEAR";
        sampler.isVec4 = true;
    }
};

bool checkRootMotionSuppressionEnabled(std::string& fail) {
    RootMotionSource rig;
    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    samplePose(rig.data.source(), 1.0f, 0, locals, globals, true);

    // Node 0 is a named skeleton root (no parent inside its skin's joint set),
    // so its animated X/Z is frozen to bind while Y still animates.
    if (!nearVec3(locals[0].t, glm::vec3(10.0f, 5.0f, 10.0f))) {
        fail = "skeleton-root carrier did not keep bind X/Z and animated Y";
        return false;
    }
    // Node 1 is a child inside its skin's joint set, so it is not a carrier.
    if (!nearVec3(locals[1].t, glm::vec3(3.0f, 4.0f, 5.0f))) {
        fail = "a non-carrier joint translation was suppressed";
        return false;
    }
    // Node 2 is the only joint of its own skin, so it is that skin's root too.
    if (!nearVec3(locals[2].t, glm::vec3(0.0f, 1.0f, 0.0f))) {
        fail = "single-joint skin root did not keep bind X/Z and animated Y";
        return false;
    }

    // The composed globals must reflect the suppressed locals.
    const glm::mat4 expectedGlobal0 =
        glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 5.0f, 10.0f));
    const glm::mat4 expectedGlobal1 = expectedGlobal0 *
        glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 4.0f, 5.0f));
    if (!nearMat4(globals[0], expectedGlobal0) ||
        !nearMat4(globals[1], expectedGlobal1)) {
        fail = "suppressed locals did not compose into the expected globals";
        return false;
    }
    return true;
}

bool checkRootMotionSuppressionDisabled(std::string& fail) {
    RootMotionSource rig;
    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    samplePose(rig.data.source(), 1.0f, 0, locals, globals, false);
    if (!nearVec3(locals[0].t, glm::vec3(12.0f, 5.0f, -7.0f)) ||
        !nearVec3(locals[1].t, glm::vec3(3.0f, 4.0f, 5.0f)) ||
        !nearVec3(locals[2].t, glm::vec3(-4.0f, 1.0f, 8.0f))) {
        fail = "disabling suppression did not keep every animated translation";
        return false;
    }
    return true;
}

bool checkNamedCarrierNodes(std::string& fail) {
    // "origin", "waist" and "hips" are selected by exact node name. Other names
    // are unaffected even when they are scene roots.
    for (const char* name : {"origin", "waist", "hips"}) {
        SingleNodeSource data;
        NodeTRS node;
        node.t = glm::vec3(2.0f, 0.0f, 3.0f);
        data.nodes.push_back(node);
        data.children.push_back({});
        data.names = {name};
        data.animations.push_back(clipOf(
            1.0f,
            {vecSampler({0.0f, 1.0f},
                        {glm::vec4(9.0f, 4.0f, -9.0f, 0.0f),
                         glm::vec4(9.0f, 4.0f, -9.0f, 0.0f)},
                        "LINEAR")},
            {AnimationChannel{0, 0, ChannelPath::Translation}}));
        std::vector<NodeTRS> locals;
        std::vector<glm::mat4> globals;
        samplePose(data.source(), 0.5f, 0, locals, globals, true);
        if (!nearVec3(locals[0].t, glm::vec3(2.0f, 4.0f, 3.0f))) {
            fail = std::string("named carrier '") + name +
                "' did not keep bind X/Z and the animated Y";
            return false;
        }
    }

    SingleNodeSource other;
    NodeTRS node;
    node.t = glm::vec3(2.0f, 0.0f, 3.0f);
    other.nodes.push_back(node);
    other.children.push_back({});
    other.names = {"mesh"};
    other.animations.push_back(clipOf(
        1.0f,
        {vecSampler({0.0f, 1.0f},
                    {glm::vec4(9.0f, 4.0f, -9.0f, 0.0f),
                     glm::vec4(9.0f, 4.0f, -9.0f, 0.0f)},
                    "LINEAR")},
        {AnimationChannel{0, 0, ChannelPath::Translation}}));
    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    samplePose(other.source(), 0.5f, 0, locals, globals, true);
    if (!nearVec3(locals[0].t, glm::vec3(9.0f, 4.0f, -9.0f))) {
        fail = "an unnamed non-joint node had its translation suppressed";
        return false;
    }
    return true;
}

bool checkSuppressionOnMatrixAuthoredCarrier(std::string& fail) {
    // A carrier authored as a matrix keeps its bind matrix but receives the
    // animated Y in the translation column.
    SingleNodeSource data;
    NodeTRS node;
    node.hasMatrix = true;
    node.matrix = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 1.0f, -6.0f));
    data.nodes.push_back(node);
    data.children.push_back({});
    data.names = {"hips"};
    data.animations.push_back(clipOf(
        1.0f,
        {vecSampler({0.0f, 1.0f},
                    {glm::vec4(7.0f, 2.5f, 7.0f, 0.0f),
                     glm::vec4(7.0f, 2.5f, 7.0f, 0.0f)},
                    "LINEAR")},
        {AnimationChannel{0, 0, ChannelPath::Translation}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    samplePose(data.source(), 0.5f, 0, locals, globals, true);
    if (!locals[0].hasMatrix) {
        fail = "matrix-authored carrier lost its matrix flag";
        return false;
    }
    const glm::mat4 expected =
        glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 2.5f, -6.0f));
    if (!nearMat4(locals[0].matrix, expected) ||
        !nearMat4(globals[0], expected)) {
        fail = "matrix-authored carrier did not keep bind X/Z and the animated Y";
        return false;
    }
    return true;
}

bool checkSegmentScaleCompensation(std::string& fail) {
    SingleNodeSource data;
    NodeTRS parent;
    parent.s = glm::vec3(2.0f, 1.0f, 1.0f);
    NodeTRS child;
    child.t = glm::vec3(1.0f, 0.0f, 0.0f);
    child.segmentScaleCompensate = true;
    data.nodes = {parent, child};
    data.children = {{1}, {}};

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    samplePose(data.source(), 0.0f, -1, locals, globals);
    if (!nearMat4(globals[1], glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)))) {
        fail = "segment scale compensation did not cancel the parent's local scale";
        return false;
    }

    data.nodes[1].segmentScaleCompensate = false;
    samplePose(data.source(), 0.0f, -1, locals, globals);
    const glm::mat4 expectedWithoutCompensation =
        glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 1.0f)) *
        glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    if (!nearMat4(globals[1], expectedWithoutCompensation)) {
        fail = "without compensation the child offset should inherit the parent scale";
        return false;
    }
    return true;
}

bool checkEmptySamplerFallbacks(std::string& fail) {
    // Engine policy for a channel whose sampler has no keys: translation and
    // scale sample to the zero vector, rotation samples to identity. Channels
    // with an out-of-range sampler or node index are skipped entirely. This is
    // the existing behavior, not a claim about malformed glTF.
    SingleNodeSource data;
    NodeTRS node;
    node.t = glm::vec3(1.0f, 1.0f, 1.0f);
    data.nodes.push_back(node);
    data.children.push_back({});
    data.animations.push_back(clipOf(
        1.0f,
        {AnimationSampler{}, AnimationSampler{}},
        {AnimationChannel{0, 0, ChannelPath::Translation},
         AnimationChannel{1, 0, ChannelPath::Scale},
         AnimationChannel{0, 0, ChannelPath::Rotation},
         AnimationChannel{5, 0, ChannelPath::Translation},
         AnimationChannel{0, 9, ChannelPath::Translation},
         AnimationChannel{0, -1, ChannelPath::Translation}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    samplePose(data.source(), 0.5f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(0.0f)) ||
        !nearVec3(locals[0].s, glm::vec3(0.0f)) ||
        !nearQuat(locals[0].r, glm::quat(1.0f, 0.0f, 0.0f, 0.0f))) {
        fail = "empty samplers did not fall back to the documented values";
        return false;
    }
    return true;
}

bool checkCubicTranslationInterpolation(std::string& fail) {
    // Two segments with non-unit intervals and asymmetric tangents. The
    // expectations are the closed-form Hermite values written out by hand.
    //
    // Keys 0 s, 1.5 s and 4 s with values (0,0,0), (3,0,0) and (3,3,0); the
    // chords are (3,0,0) over 1.5 s and (0,3,0) over 2.5 s. Segment 0 uses an
    // out-tangent scaled by 1.0x the chord and an in-tangent of 0, so its
    // weight at a = 0.5 is h10 + h01 = 0.625 and the value is
    // (0,0,0) + 0.625 * (3,0,0) = (1.875, 0, 0). Segment 1 uses +2.5x and
    // -1.0x per second, so its weight at a = 0.5 is
    // h01 + 2.5*h10 - (5/6)*h11 = 0.5 + 0.3125 + 0.1041667 = 0.9166667 and
    // the value is (3,0,0) + 0.9166667 * (0,3,0) = (3, 2.75, 0).
    SingleNodeSource data;
    data.nodes.push_back(NodeTRS{});
    data.children.push_back({});
    data.animations.push_back(clipOf(
        6.0f,
        {cubicSampler(
            {0.0f, 1.5f, 4.0f},
            {glm::vec4(9.0f, 9.0f, 9.0f, 9.0f),      // key 0 in-tangent: unused
             glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),      // key 0 value
             glm::vec4(2.0f, 0.0f, 0.0f, 0.0f),      // key 0 out-tangent
             glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),      // key 1 in-tangent
             glm::vec4(3.0f, 0.0f, 0.0f, 0.0f),      // key 1 value
             glm::vec4(0.0f, 3.0f, 0.0f, 0.0f),      // key 1 out-tangent
             glm::vec4(0.0f, -1.0f, 0.0f, 0.0f),     // key 2 in-tangent
             glm::vec4(3.0f, 3.0f, 0.0f, 0.0f),      // key 2 value
             glm::vec4(9.0f, 9.0f, 9.0f, 9.0f)},     // key 2 out-tangent: unused
            false)},
        {AnimationChannel{0, 0, ChannelPath::Translation}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;

    samplePose(data.source(), 0.75f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(1.875f, 0.0f, 0.0f))) {
        fail = "cubic translation inside the first segment did not match the "
               "hand-computed Hermite value";
        return false;
    }
    samplePose(data.source(), 2.75f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(3.0f, 2.75f, 0.0f))) {
        fail = "cubic translation inside the second segment did not match the "
               "hand-computed Hermite value";
        return false;
    }

    // Exact keys return the authored values, including the first and last.
    samplePose(data.source(), 0.0f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(0.0f, 0.0f, 0.0f))) {
        fail = "an exact first cubic key did not return the authored value";
        return false;
    }
    samplePose(data.source(), 1.5f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(3.0f, 0.0f, 0.0f))) {
        fail = "an exact interior cubic key did not return the authored value";
        return false;
    }
    samplePose(data.source(), 4.0f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(3.0f, 3.0f, 0.0f))) {
        fail = "an exact last cubic key did not return the authored value";
        return false;
    }

    // The key range ends at 4 s inside a 6 s clip: a wrapped sample time past
    // it clamps to the last key instead of interpolating across the tail.
    samplePose(data.source(), 5.0f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(3.0f, 3.0f, 0.0f))) {
        fail = "a cubic sample past the last key was not clamped to it";
        return false;
    }
    samplePose(data.source(), -1.0f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(3.0f, 3.0f, 0.0f))) {
        fail = "a negative wrapped cubic sample was not clamped to the last key";
        return false;
    }

    // The same values with LINEAR interpolation must give a different pose at
    // 0.75 s, so this contract would fail if the cubic path were not used.
    SingleNodeSource linear;
    linear.nodes.push_back(NodeTRS{});
    linear.children.push_back({});
    linear.animations.push_back(clipOf(
        6.0f,
        {vecSampler({0.0f, 1.5f, 4.0f},
                    {glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
                     glm::vec4(3.0f, 0.0f, 0.0f, 0.0f),
                     glm::vec4(3.0f, 3.0f, 0.0f, 0.0f)},
                    "LINEAR")},
        {AnimationChannel{0, 0, ChannelPath::Translation}}));
    samplePose(linear.source(), 0.75f, 0, locals, globals);
    if (!nearVec3(locals[0].t, glm::vec3(1.5f, 0.0f, 0.0f))) {
        fail = "the LINEAR reference sample changed";
        return false;
    }
    return true;
}

bool checkCubicScaleInterpolation(std::string& fail) {
    // Keys 0 s and 1 s with values (1,2,3) and (5,4,1); the chord is
    // (4,2,-2). The out-tangent (2,1,-1) per second scales the interval to
    // 0.5x the chord and the in-tangent of the second key is 0, so the weight
    // at a = 0.5 is h01 + 0.5*h10 = 0.5625 and the value is
    // (1,2,3) + 0.5625 * (4,2,-2) = (3.25, 3.125, 1.875).
    SingleNodeSource data;
    data.nodes.push_back(NodeTRS{});
    data.children.push_back({});
    data.animations.push_back(clipOf(
        1.0f,
        {cubicSampler({0.0f, 1.0f},
                      {glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
                       glm::vec4(1.0f, 2.0f, 3.0f, 0.0f),
                       glm::vec4(2.0f, 1.0f, -1.0f, 0.0f),
                       glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
                       glm::vec4(5.0f, 4.0f, 1.0f, 0.0f),
                       glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)},
                      false)},
        {AnimationChannel{0, 0, ChannelPath::Scale}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    samplePose(data.source(), 0.5f, 0, locals, globals);
    if (!nearVec3(locals[0].s, glm::vec3(3.25f, 3.125f, 1.875f))) {
        fail = "cubic scale did not match the hand-computed Hermite value";
        return false;
    }
    // The unused w component of a vec3 cubic sampler is never read into scale.
    samplePose(data.source(), 0.0f, 0, locals, globals);
    if (!nearVec3(locals[0].s, glm::vec3(1.0f, 2.0f, 3.0f))) {
        fail = "the exact first cubic scale key did not return the authored value";
        return false;
    }
    return true;
}

bool checkCubicRotationNormalization(std::string& fail) {
    // Keys 0 s and 2 s rotate from identity to -120 degrees about +Z. Values
    // and tangents are authored at twice the geometric scale, so the sampler
    // has to normalize the componentwise Hermite result. With an out-tangent
    // of 3x chord / interval on key 0 and an in-tangent of -1x on key 1 the
    // curve is value0 + (3a - 2a^2) * chord:
    //   a = 0.25 (t = 0.5 s) -> -76.4264214034764 degrees
    //   a = 0.5  (t = 1.0 s) -> the interpolated value equals the authored
    //                            -120 degree key exactly
    const float sinHalf = 0.8660254037844386f;
    SingleNodeSource data;
    data.nodes.push_back(NodeTRS{});
    data.children.push_back({});
    data.animations.push_back(clipOf(
        2.0f,
        {cubicSampler(
            {0.0f, 2.0f},
            {glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
             glm::vec4(0.0f, 0.0f, 0.0f, 2.0f),
             glm::vec4(0.0f, 0.0f, -3.0f * sinHalf, -1.5f),
             glm::vec4(0.0f, 0.0f, sinHalf, 0.5f),
             glm::vec4(0.0f, 0.0f, -2.0f * sinHalf, 1.0f),
             glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)},
            true)},
        {AnimationChannel{0, 0, ChannelPath::Rotation}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    const auto angleDegrees = [](const glm::quat& q) {
        return 2.0 * std::atan2(
                         static_cast<double>(q.z),
                         static_cast<double>(q.w)) *
            180.0 / 3.14159265358979323846;
    };

    samplePose(data.source(), 0.5f, 0, locals, globals);
    if (!near(glm::length(locals[0].r), 1.0f, 1.0e-6f)) {
        fail = "cubic rotation did not normalize the sampled quaternion";
        return false;
    }
    if (std::abs(angleDegrees(locals[0].r) - (-76.4264214034764)) > 1.0e-3) {
        fail = "cubic rotation did not match the hand-computed early angle";
        return false;
    }

    samplePose(data.source(), 1.0f, 0, locals, globals);
    if (std::abs(angleDegrees(locals[0].r) - (-120.0)) > 1.0e-3) {
        fail = "cubic rotation did not match the hand-computed midpoint angle";
        return false;
    }
    if (!near(locals[0].r.z, -sinHalf, 1.0e-5f) ||
        !near(locals[0].r.w, 0.5f, 1.0e-5f)) {
        fail = "the exact cubic midpoint did not return the authored quaternion";
        return false;
    }

    samplePose(data.source(), 0.0f, 0, locals, globals);
    if (!near(locals[0].r.w, 1.0f, 1.0e-6f) ||
        !near(locals[0].r.z, 0.0f, 1.0e-6f)) {
        fail = "the exact first cubic rotation key did not return identity";
        return false;
    }
    return true;
}

bool checkCubicRotationSignSensitivity(std::string& fail) {
    // The second key is the -240 degree rotation about +Z (z = -sqrt(3)/2,
    // w = -1/2), which is the same orientation as +120 degrees. The authored
    // component curve is not the short path: slerp would flip the sign onto
    // -120 degrees. Componentwise interpolation must land on the authored
    // quaternion at the midpoint instead of flipping it.
    const float sinHalf = 0.8660254037844386f;
    SingleNodeSource data;
    data.nodes.push_back(NodeTRS{});
    data.children.push_back({});
    data.animations.push_back(clipOf(
        2.0f,
        {cubicSampler({0.0f, 2.0f},
                      {glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
                       glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
                       glm::vec4(0.0f, 0.0f, -1.5f * sinHalf, -2.25f),
                       glm::vec4(0.0f, 0.0f, 0.5f * sinHalf, 0.75f),
                       glm::vec4(0.0f, 0.0f, -sinHalf, -0.5f),
                       glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)},
                      true)},
        {AnimationChannel{0, 0, ChannelPath::Rotation}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    samplePose(data.source(), 1.0f, 0, locals, globals);
    if (!near(locals[0].r.z, -sinHalf, 1.0e-5f) ||
        !near(locals[0].r.w, -0.5f, 1.0e-5f)) {
        fail = "the cubic midpoint flipped the authored quaternion sign";
        return false;
    }
    return true;
}

bool checkCubicExactKeysWithExtremeTangents(std::string& fail) {
    // Keys 1 s, 3 s and 5 s with finite FLT_MAX tangents and finite values. The
    // polynomial form would overflow (2 * FLT_MAX and -0.125 * Inf give NaN),
    // so a clamped endpoint or exact key must return the authored value without
    // evaluating it. The node default is deliberately different from every key
    // value, so a skipped channel is distinguishable from a sampled one.
    const float huge = std::numeric_limits<float>::max();
    SingleNodeSource data;
    NodeTRS node;
    node.t = glm::vec3(100.0f, 100.0f, 100.0f);
    data.nodes.push_back(node);
    data.children.push_back({});
    data.animations.push_back(clipOf(
        10.0f,
        {cubicSampler({1.0f, 3.0f, 5.0f},
                      {glm::vec4(huge, huge, huge, huge),    // key 0 in-tangent
                       glm::vec4(1.0f, 2.0f, 3.0f, 0.0f),    // key 0 value
                       glm::vec4(huge, huge, huge, 0.0f),    // key 0 out-tangent
                       glm::vec4(huge, huge, huge, 0.0f),    // key 1 in-tangent
                       glm::vec4(4.0f, 5.0f, 6.0f, 0.0f),    // key 1 value
                       glm::vec4(huge, huge, huge, 0.0f),    // key 1 out-tangent
                       glm::vec4(huge, huge, huge, 0.0f),    // key 2 in-tangent
                       glm::vec4(7.0f, 8.0f, 9.0f, 0.0f),    // key 2 value
                       glm::vec4(huge, huge, huge, huge)},   // key 2 out-tangent
                      false)},
        {AnimationChannel{0, 0, ChannelPath::Translation}}));

    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;
    const auto expectAt = [&](float time, const glm::vec3& expected,
                              const char* label) {
        samplePose(data.source(), time, 0, locals, globals);
        if (!nearVec3(locals[0].t, expected)) {
            fail = std::string("a cubic sample with extreme tangents did not "
                               "return the authored value: ") + label;
            return false;
        }
        return true;
    };
    if (!expectAt(0.0f, glm::vec3(1.0f, 2.0f, 3.0f),
                  "clamped before the first key")) {
        return false;
    }
    if (!expectAt(1.0f, glm::vec3(1.0f, 2.0f, 3.0f), "exact first key")) {
        return false;
    }
    if (!expectAt(3.0f, glm::vec3(4.0f, 5.0f, 6.0f), "exact interior key")) {
        return false;
    }
    if (!expectAt(5.0f, glm::vec3(7.0f, 8.0f, 9.0f), "exact last key")) {
        return false;
    }
    if (!expectAt(9.0f, glm::vec3(7.0f, 8.0f, 9.0f),
                  "clamped after the last key")) {
        return false;
    }

    // Strictly inside a segment the interpolation is required, and its
    // non-finite result must skip the channel and keep the current local value.
    samplePose(data.source(), 2.0f, 0, locals, globals);
    if (!nearVec3(locals[0].t, node.t)) {
        fail = "a non-finite interpolated cubic result was not skipped";
        return false;
    }
    return true;
}

bool checkMalformedCubicIsSkipped(std::string& fail) {
    // Malformed cubic data skips the channel and leaves the node's current
    // local component alone. Every sampler below is rejected by the sampler's
    // own validation, not by the importer.
    const std::vector<std::pair<AnimationSampler, const char*>> malformed{
        {cubicSampler({1.0f},
                      {glm::vec4(0.0f),
                       glm::vec4(1.0f, 1.0f, 1.0f, 0.0f),
                       glm::vec4(0.0f)},
                      false),
         "single-key cubic"},
        {cubicSampler({0.0f, 1.0f},
                      {glm::vec4(0.0f),
                       glm::vec4(1.0f, 1.0f, 1.0f, 0.0f),
                       glm::vec4(0.0f),
                       glm::vec4(2.0f, 2.0f, 2.0f, 0.0f)},
                      false),
         "wrong output count"},
        {cubicSampler({0.0f, 1.0f},
                      {glm::vec4(0.0f),
                       glm::vec4(1.0f, std::nanf(""), 1.0f, 0.0f),
                       glm::vec4(0.0f),
                       glm::vec4(0.0f),
                       glm::vec4(2.0f, 2.0f, 2.0f, 0.0f),
                       glm::vec4(0.0f)},
                      false),
         "non-finite value"},
        {cubicSampler({0.0f, 0.0f},
                      {glm::vec4(0.0f),
                       glm::vec4(1.0f, 1.0f, 1.0f, 0.0f),
                       glm::vec4(0.0f),
                       glm::vec4(0.0f),
                       glm::vec4(2.0f, 2.0f, 2.0f, 0.0f),
                       glm::vec4(0.0f)},
                      false),
         "non-increasing times"},
        {cubicSampler(
             {0.0f, std::numeric_limits<float>::infinity()},
             {glm::vec4(0.0f),
              glm::vec4(1.0f, 1.0f, 1.0f, 0.0f),
              glm::vec4(0.0f),
              glm::vec4(0.0f),
              glm::vec4(2.0f, 2.0f, 2.0f, 0.0f),
              glm::vec4(0.0f)},
             false),
         "non-finite key time"},
        {cubicSampler(
             {0.0f, 1.0f},
             {glm::vec4(0.0f),
              glm::vec4(1.0f, 1.0f, 1.0f, 0.0f),
              glm::vec4(std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f),
              glm::vec4(0.0f),
              glm::vec4(2.0f, 2.0f, 2.0f, 0.0f),
              glm::vec4(0.0f)},
             false),
         "non-finite out-tangent"},
        {cubicSampler(
             {0.0f, 1.0f},
             {glm::vec4(0.0f),
              glm::vec4(1.0f, 1.0f, 1.0f, 0.0f),
              glm::vec4(0.0f),
              glm::vec4(0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f),
              glm::vec4(2.0f, 2.0f, 2.0f, 0.0f),
              glm::vec4(0.0f)},
             false),
         "non-finite in-tangent"},
    };

    for (const auto& [sampler, label] : malformed) {
        SingleNodeSource data;
        NodeTRS node;
        node.t = glm::vec3(1.0f, 2.0f, 3.0f);
        data.nodes.push_back(node);
        data.children.push_back({});
        data.animations.push_back(clipOf(
            2.0f, {sampler}, {AnimationChannel{0, 0, ChannelPath::Translation}}));
        std::vector<NodeTRS> locals;
        std::vector<glm::mat4> globals;
        samplePose(data.source(), 0.5f, 0, locals, globals);
        if (!nearVec3(locals[0].t, node.t)) {
            fail = std::string("a ") + label +
                " translation was not skipped";
            return false;
        }
    }

    // A malformed cubic channel after a valid LINEAR channel on the same node
    // keeps the pose the valid channel produced.
    {
        SingleNodeSource data;
        data.nodes.push_back(NodeTRS{});
        data.children.push_back({});
        data.animations.push_back(clipOf(
            2.0f,
            {vecSampler({0.0f, 2.0f},
                        {glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
                         glm::vec4(4.0f, 4.0f, 4.0f, 0.0f)},
                        "LINEAR"),
             cubicSampler({1.0f, 1.0f},
                          {glm::vec4(0.0f),
                           glm::vec4(9.0f, 9.0f, 9.0f, 0.0f),
                           glm::vec4(0.0f),
                           glm::vec4(0.0f),
                           glm::vec4(9.0f, 9.0f, 9.0f, 0.0f),
                           glm::vec4(0.0f)},
                          false)},
            {AnimationChannel{0, 0, ChannelPath::Translation},
             AnimationChannel{1, 0, ChannelPath::Translation}}));
        std::vector<NodeTRS> locals;
        std::vector<glm::mat4> globals;
        samplePose(data.source(), 1.0f, 0, locals, globals);
        if (!nearVec3(locals[0].t, glm::vec3(2.0f, 2.0f, 2.0f))) {
            fail = "a malformed cubic channel overwrote the current local value";
            return false;
        }
    }

    // A degenerate rotation curve has a zero-length quaternion even though its
    // components are finite; the channel must be skipped.
    {
        SingleNodeSource data;
        NodeTRS node;
        node.r = glm::angleAxis(glm::radians(40.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        data.nodes.push_back(node);
        data.children.push_back({});
        data.animations.push_back(clipOf(
            2.0f,
            {cubicSampler({0.0f, 1.0f},
                          {glm::vec4(0.0f),
                           glm::vec4(0.0f),
                           glm::vec4(0.0f),
                           glm::vec4(0.0f),
                           glm::vec4(0.0f),
                           glm::vec4(0.0f)},
                          true)},
            {AnimationChannel{0, 0, ChannelPath::Rotation}}));
        std::vector<NodeTRS> locals;
        std::vector<glm::mat4> globals;
        samplePose(data.source(), 0.5f, 0, locals, globals);
        if (!nearQuat(locals[0].r, node.r)) {
            fail = "a degenerate cubic rotation did not keep the default rotation";
            return false;
        }
        if (!near(locals[0].r.w, node.r.w, 1.0e-6f) ||
            !near(locals[0].r.y, node.r.y, 1.0e-6f)) {
            fail = "a degenerate cubic rotation changed the authored quaternion";
            return false;
        }
    }
    return true;
}

const Check kChecks[] = {
    &checkEmptyInputsReplaceOutputs,
    &checkDefaultsAndInvalidClip,
    &checkLinearTranslationAndLooping,
    &checkStepTranslation,
    &checkLinearRotationNormalized,
    &checkStepRotationAndScale,
    &checkLinearScaleInterpolation,
    &checkOneKeyChannel,
    &checkExactPositiveEndpointPolicy,
    &checkChannelShorterThanClip,
    &checkHierarchyTransforms,
    &checkOutputReplacement,
    &checkRootMotionSuppressionEnabled,
    &checkRootMotionSuppressionDisabled,
    &checkNamedCarrierNodes,
    &checkSuppressionOnMatrixAuthoredCarrier,
    &checkSegmentScaleCompensation,
    &checkEmptySamplerFallbacks,
    &checkCubicTranslationInterpolation,
    &checkCubicScaleInterpolation,
    &checkCubicRotationNormalization,
    &checkCubicRotationSignSensitivity,
    &checkCubicExactKeysWithExtremeTangents,
    &checkMalformedCubicIsSkipped,
};

} // namespace

bool test_model_pose_sampler_contract(std::string& outFail) {
    for (const Check check : kChecks) {
        std::string failure;
        if (!check(failure)) {
            outFail = failure;
            return false;
        }
    }
    return true;
}
