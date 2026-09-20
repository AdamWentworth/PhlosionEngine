#pragma once

#include "engine/render/ModelAnimationTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine::animation_fixture {

using engine::render::model_types::AnimationClip;
using engine::render::model_types::NodeTRS;
using engine::render::model_types::SkinData;

// One authored bind vertex. The palette is applied to these exact positions,
// joint indices and weights by the renderer's skinning path; the sample never
// deforms them on the CPU.
struct FixtureVertexSpec {
    glm::vec3 position{0.0f};
    std::array<int, 4> joints{0, 0, 0, 0};
    std::array<float, 4> weights{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 color{1.0f};
    glm::vec2 uv{0.0f};
};

// Public, asset-independent hinged-strip rig: a fixed base joint and a moving
// tip joint with visibly blended weights between them.
struct FixtureRig {
    std::vector<NodeTRS> nodesDefault;
    std::vector<std::vector<int>> nodeChildren;
    std::vector<int> sceneRoots;
    std::vector<SkinData> skins;
    std::vector<AnimationClip> animations;
    std::vector<std::string> nodeNames;
    int linearClipIndex = 0;
    int stepClipIndex = 1;
    int skinIndex = 0;
    // Nonidentity mesh transform. The caller-owned palette has to undo it, so a
    // palette built in the wrong space shows up as a visibly displaced rig.
    glm::mat4 meshTransform{1.0f};
    std::vector<FixtureVertexSpec> vertices;
    std::vector<std::uint32_t> indices;
    std::array<float, 4> backgroundColor{0.05f, 0.60f, 0.08f, 1.0f};
    std::array<float, 3> backgroundExtentMin{-8.0f, -4.5f, -2.0f};
    std::array<float, 3> backgroundExtentMax{8.0f, 4.5f, -2.0f};
};

const FixtureRig& fixtureRig();

// Authored camera used by both hosts and by the verification expectations.
glm::vec3 fixtureCameraPosition();
glm::vec3 fixtureCameraTarget();
float fixtureCameraFovDegrees();
float fixtureCameraNearPlane();
float fixtureCameraFarPlane();

// The authored tip rotation for a case, converted to a matrix. This is the
// analytic outcome the verification tools check against; it is computed from
// the case catalog, not from the sampled pose.
glm::mat4 fixtureTipRotation(float angleDegrees);

// Analytic linear-blend-skinning position of one bind vertex for a tip
// rotation, derived from the authored joints rather than from the sampler.
glm::vec3 fixtureAnalyticBlendPosition(
    const FixtureVertexSpec& vertex,
    float angleDegrees);

// Analytic position of a bind vertex when every vertex follows the tip joint
// (the rigid alternative a broken weight/joint mapping would produce).
glm::vec3 fixtureAnalyticRigidTipPosition(
    const FixtureVertexSpec& vertex,
    float angleDegrees);

} // namespace engine::animation_fixture
