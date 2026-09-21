#include "AnimationFixtureRig.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::animation_fixture {
namespace {

// Authored rig constants. The base joint sits slightly left of the strip; the
// tip joint sits slightly right of it. Both inverse binds are translations, so
// every expected position is easy to verify by hand.
const glm::vec3 kBaseJointOrigin(-0.5f, 0.0f, 0.0f);
const glm::vec3 kTipJointOrigin(2.5f, 0.0f, 0.0f);
constexpr float kStripHalfHeight = 0.35f;
constexpr std::array<float, 5> kStripStations{0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
constexpr std::array<float, 3> kBaseColor{0.85f, 0.08f, 0.08f};
constexpr std::array<float, 3> kTipColor{0.90f, 0.85f, 0.05f};

void appendQuad(std::vector<std::uint32_t>& indices,
                std::size_t leftColumn,
                std::size_t rightColumn) {
    const std::uint32_t bottomLeft =
        static_cast<std::uint32_t>(leftColumn * 2u);
    const std::uint32_t topLeft = bottomLeft + 1u;
    const std::uint32_t bottomRight =
        static_cast<std::uint32_t>(rightColumn * 2u);
    const std::uint32_t topRight = bottomRight + 1u;
    indices.push_back(bottomLeft);
    indices.push_back(bottomRight);
    indices.push_back(topRight);
    indices.push_back(bottomLeft);
    indices.push_back(topRight);
    indices.push_back(topLeft);
}

FixtureRig buildRig() {
    FixtureRig rig;

    NodeTRS base;
    base.t = kBaseJointOrigin;
    NodeTRS tip;
    tip.t = kTipJointOrigin - kBaseJointOrigin;
    rig.nodesDefault = {base, tip};
    rig.nodeChildren = {{1}, {}};
    rig.sceneRoots = {0};
    rig.nodeNames = {"base", "tip"};

    SkinData skin;
    skin.joints = {0, 1};
    skin.inverseBind = {
        glm::translate(glm::mat4(1.0f), -kBaseJointOrigin),
        glm::translate(glm::mat4(1.0f), -kTipJointOrigin)};
    rig.skins = {skin};

    const auto rotationChannel = [](int samplerIndex) {
        engine::render::model_types::AnimationChannel channel;
        channel.samplerIndex = samplerIndex;
        channel.targetNode = 1;
        channel.path = engine::render::model_types::ChannelPath::Rotation;
        return channel;
    };
    const auto zRotationSampler = [](float duration,
                                     float endAngleDegrees,
                                     const char* interpolation) {
        engine::render::model_types::AnimationSampler sampler;
        sampler.inputs = {0.0f, duration};
        const glm::quat start(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::quat end = glm::angleAxis(
            glm::radians(endAngleDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
        // Engine storage order for rotation keys is (x, y, z, w).
        sampler.outputs = {
            glm::vec4(start.x, start.y, start.z, start.w),
            glm::vec4(end.x, end.y, end.z, end.w)};
        sampler.interpolation = interpolation;
        sampler.isVec4 = true;
        return sampler;
    };

    AnimationClip linear;
    linear.name = "linear";
    linear.durationSec = 2.0f;
    linear.samplers = {zRotationSampler(2.0f, -70.0f, "LINEAR")};
    linear.channels = {rotationChannel(0)};

    AnimationClip step;
    step.name = "step";
    // The key range is shorter than the clip on purpose: the step case samples
    // past the last key so the engine's STEP hold-last-key policy produces a
    // state that is distinct from the LINEAR near-endpoint pose.
    step.durationSec = 2.0f;
    step.samplers = {zRotationSampler(1.0f, -110.0f, "STEP")};
    step.channels = {rotationChannel(0)};

    // CUBICSPLINE rotation: the same identity -> -120 degree keys as a LINEAR
    // curve would use, but with real tangents. The out-tangent of the first key
    // is 3x the chord / interval and the in-tangent of the second is -1x, so
    // the componentwise Hermite curve is value0 + (3a - 2a^2) * chord: the
    // midpoint lands exactly on the authored -120 degree key while a LINEAR
    // fallback would sit at -60 degrees.
    AnimationClip cubic;
    cubic.name = "cubic";
    cubic.durationSec = 2.0f;
    {
        engine::render::model_types::AnimationSampler sampler;
        sampler.inputs = {0.0f, 2.0f};
        const glm::quat start(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::quat end = glm::angleAxis(
            glm::radians(-120.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        const glm::vec4 chord(
            end.x - start.x, end.y - start.y, end.z - start.z, end.w - start.w);
        // Engine storage order for rotation keys is (x, y, z, w), and the
        // glTF tangents are per-second derivatives, so they scale by 1 / 2 s.
        sampler.outputs = {
            glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),                 // key 0 in-tangent
            glm::vec4(start.x, start.y, start.z, start.w),     // key 0 value
            (3.0f / 2.0f) * chord,                             // key 0 out-tangent
            (-1.0f / 2.0f) * chord,                            // key 1 in-tangent
            glm::vec4(end.x, end.y, end.z, end.w),             // key 1 value
            glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),                 // key 1 out-tangent
        };
        sampler.interpolation = "CUBICSPLINE";
        sampler.isVec4 = true;
        cubic.samplers = {sampler};
        cubic.channels = {rotationChannel(0)};
    }

    rig.animations = {linear, step, cubic};
    rig.linearClipIndex = 0;
    rig.stepClipIndex = 1;
    rig.cubicClipIndex = 2;

    rig.meshTransform =
        glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f, 0.25f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, 0.5f));

    for (const float station : kStripStations) {
        const float tipWeight = station / kStripStations.back();
        const float baseWeight = 1.0f - tipWeight;
        const glm::vec3 color(
            kBaseColor[0] + (kTipColor[0] - kBaseColor[0]) * tipWeight,
            kBaseColor[1] + (kTipColor[1] - kBaseColor[1]) * tipWeight,
            kBaseColor[2] + (kTipColor[2] - kBaseColor[2]) * tipWeight);
        for (int row = 0; row < 2; ++row) {
            FixtureVertexSpec vertex;
            vertex.position = glm::vec3(
                station,
                row == 0 ? -kStripHalfHeight : kStripHalfHeight,
                0.0f);
            vertex.joints = {0, 1, 0, 0};
            vertex.weights = {baseWeight, tipWeight, 0.0f, 0.0f};
            vertex.color = color;
            vertex.uv = glm::vec2(
                tipWeight, row == 0 ? 0.0f : 1.0f);
            rig.vertices.push_back(vertex);
        }
    }
    for (std::size_t column = 0; column + 1u < kStripStations.size();
         ++column) {
        appendQuad(rig.indices, column, column + 1u);
    }

    return rig;
}

const FixtureRig& rig() {
    static const FixtureRig instance = buildRig();
    return instance;
}

} // namespace

const FixtureRig& fixtureRig() {
    return rig();
}

glm::vec3 fixtureCameraPosition() {
    return glm::vec3(1.25f, 0.35f, 6.0f);
}

glm::vec3 fixtureCameraTarget() {
    return glm::vec3(1.25f, 0.0f, 0.0f);
}

float fixtureCameraFovDegrees() {
    return 35.0f;
}

float fixtureCameraNearPlane() {
    return 0.1f;
}

float fixtureCameraFarPlane() {
    return 100.0f;
}

glm::mat4 fixtureTipRotation(float angleDegrees) {
    return glm::rotate(
        glm::mat4(1.0f),
        glm::radians(angleDegrees),
        glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::vec3 fixtureAnalyticBlendPosition(
    const FixtureVertexSpec& vertex,
    float angleDegrees) {
    const glm::mat4 tipTransform =
        glm::translate(glm::mat4(1.0f), kTipJointOrigin) *
        fixtureTipRotation(angleDegrees) *
        glm::translate(glm::mat4(1.0f), -kTipJointOrigin);
    const glm::vec3 rigidTip =
        glm::vec3(tipTransform * glm::vec4(vertex.position, 1.0f));
    return vertex.weights[0] * vertex.position + vertex.weights[1] * rigidTip;
}

glm::vec3 fixtureAnalyticRigidTipPosition(
    const FixtureVertexSpec& vertex,
    float angleDegrees) {
    const glm::mat4 tipTransform =
        glm::translate(glm::mat4(1.0f), kTipJointOrigin) *
        fixtureTipRotation(angleDegrees) *
        glm::translate(glm::mat4(1.0f), -kTipJointOrigin);
    return glm::vec3(tipTransform * glm::vec4(vertex.position, 1.0f));
}

} // namespace engine::animation_fixture
