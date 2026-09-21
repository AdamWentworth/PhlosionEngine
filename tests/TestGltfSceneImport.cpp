// tests/TestGltfSceneImport.cpp
//
// CPU contract for the existing glTF character-import path. The test authors a
// tiny public glTF file, loads it through the production loader boundary
// (engine::render::gltf::loader::tryLoad) and inspects the production importer
// output (engine::render::gltf::model::buildSceneData). It never constructs a
// Model, uploads a texture or touches a graphics context.

#include "gltf_import/GltfImportTestSupport.h"

#include "engine/render/gltf/FastGLTFLoader.h"
#include "engine/render/gltf/ModelFastGltfSceneData.h"
#include "engine/render/ModelPoseSampler.h"

#include <fastgltf/tools.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

using engine::render::model_types::AnimationClip;
using engine::render::model_types::AnimationSampler;
using engine::render::model_types::ChannelPath;
using engine::render::model_types::NodeTRS;
using engine::render::model_types::SkinData;
using engine::render::animation::PoseSource;
using engine::render::animation::samplePose;
using gltf_import_contract::FixtureDirectory;
using gltf_import_contract::LittleEndianBuffer;
using gltf_import_contract::expect;
using gltf_import_contract::floatText;
using gltf_import_contract::matText;
using gltf_import_contract::nearlyEqual;
using gltf_import_contract::quatXyzwText;
using gltf_import_contract::vecText;

// Authored fixture values shared by the assertions below.
constexpr float kSqrtHalf = 0.70710678f;
constexpr float kSqrtThreeOverTwo = 0.86602540f;
constexpr std::size_t kFixtureBufferBytes = 408u;

struct SceneImport {
    std::vector<NodeTRS> nodes;
    std::vector<std::string> names;
    std::vector<std::vector<int>> children;
    std::vector<int> nodeMesh;
    std::vector<int> nodeSkin;
    std::vector<int> sceneRoots;
    std::vector<SkinData> skins;
    std::vector<AnimationClip> animations;
};

enum class SceneSelection {
    FirstSceneFallback,   // No "scene" member: the first scene is selected.
    SelectedSecondScene,  // "scene": 1 selects the second, non-first scene.
    NoScenes,             // No scene list at all.
};

glm::mat4 authoredFirstInverseBind() {
    glm::mat4 matrix(1.0f);
    matrix[0] = glm::vec4(2.0f, 0.0f, 0.0f, 0.0f);
    matrix[1] = glm::vec4(0.0f, 3.0f, 0.0f, 0.0f);
    matrix[2] = glm::vec4(0.0f, 0.0f, 4.0f, 0.0f);
    matrix[3] = glm::vec4(1.0f, 2.0f, 3.0f, 1.0f);
    return matrix;
}

glm::mat4 authoredSecondInverseBind() {
    glm::mat4 matrix(1.0f);
    matrix[0] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
    matrix[1] = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f);
    matrix[2] = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
    matrix[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    return matrix;
}

// Explicit little-endian fixture data. The bufferView offsets in
// characterSceneJson below match this layout exactly.
std::vector<std::uint8_t> characterSceneBinary() {
    LittleEndianBuffer buffer;
    // bufferView 0 @0: POSITION, 3 * VEC3.
    buffer.addVec3({0.0f, 0.0f, 0.0f});
    buffer.addVec3({1.0f, 0.0f, 0.0f});
    buffer.addVec3({0.0f, 1.0f, 0.0f});
    // bufferView 1 @36: indices, 3 * UNSIGNED_SHORT, then 2 padding bytes so
    // every later bufferView starts 4-byte aligned at @44.
    buffer.addU16(0u);
    buffer.addU16(1u);
    buffer.addU16(2u);
    buffer.addU16(0u);
    // bufferView 2 @44: Idle translation input.
    buffer.addF32(0.0f);
    buffer.addF32(1.5f);
    // bufferView 3 @52: Idle translation output.
    buffer.addVec3({0.0f, 0.0f, 0.0f});
    buffer.addVec3({0.0f, 1.0f, 0.0f});
    // bufferView 4 @76: Idle rotation input.
    buffer.addF32(0.0f);
    buffer.addF32(0.75f);
    // bufferView 5 @84: Idle rotation output, authored xyzw.
    buffer.addVec4({0.0f, 0.0f, 0.0f, 1.0f});
    buffer.addVec4({0.0f, kSqrtHalf, 0.0f, kSqrtHalf});
    // bufferView 6 @116: Idle scale input.
    buffer.addF32(0.0f);
    buffer.addF32(0.5f);
    // bufferView 7 @124: Idle scale output.
    buffer.addVec3({1.0f, 1.0f, 1.0f});
    buffer.addVec3({2.0f, 2.0f, 2.0f});
    // bufferView 8 @148: Walk translation input.
    buffer.addF32(0.0f);
    buffer.addF32(2.0f);
    // bufferView 9 @156: Walk translation output.
    buffer.addVec3({1.0f, 0.0f, 0.0f});
    buffer.addVec3({2.0f, 0.0f, 0.0f});
    // bufferView 10 @180: Walk rotation input.
    buffer.addF32(0.0f);
    buffer.addF32(0.25f);
    // bufferView 11 @188: Walk rotation output, authored xyzw.
    buffer.addVec4({0.0f, 0.0f, 0.0f, 1.0f});
    buffer.addVec4({0.0f, 0.0f, kSqrtHalf, kSqrtHalf});
    // bufferView 12 @220: two MAT4 inverse bind matrices, column-major.
    buffer.addMat4Columns(authoredFirstInverseBind());
    buffer.addMat4Columns(authoredSecondInverseBind());
    // bufferView 13 @348: JOINTS_0, 3 * VEC4 UNSIGNED_BYTE. Indices address
    // the two joints of skin 0.
    for (const std::uint8_t index : {0u, 1u, 0u, 0u, 1u, 0u, 0u, 0u, 1u, 1u, 0u, 0u}) {
        buffer.addU8(index);
    }
    // bufferView 14 @360: WEIGHTS_0, 3 * VEC4 FLOAT, normalized per vertex.
    buffer.addVec4({1.0f, 0.0f, 0.0f, 0.0f});
    buffer.addVec4({0.5f, 0.5f, 0.0f, 0.0f});
    buffer.addVec4({0.25f, 0.75f, 0.0f, 0.0f});
    return buffer.bytes();
}

std::string characterSceneJson(SceneSelection selection) {
    std::string json = R"({
  "asset": { "version": "2.0" },
  "buffers": [ { "uri": "character_scene.bin", "byteLength": 408 } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 6, "target": 34963 },
    { "buffer": 0, "byteOffset": 44, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 52, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 76, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 84, "byteLength": 32 },
    { "buffer": 0, "byteOffset": 116, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 124, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 148, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 156, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 180, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 188, "byteLength": 32 },
    { "buffer": 0, "byteOffset": 220, "byteLength": 128 },
    { "buffer": 0, "byteOffset": 348, "byteLength": 12 },
    { "buffer": 0, "byteOffset": 360, "byteLength": 48 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0] },
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" },
    { "bufferView": 2, "componentType": 5126, "count": 2, "type": "SCALAR",
      "min": [0.0], "max": [1.5] },
    { "bufferView": 3, "componentType": 5126, "count": 2, "type": "VEC3" },
    { "bufferView": 4, "componentType": 5126, "count": 2, "type": "SCALAR",
      "min": [0.0], "max": [0.75] },
    { "bufferView": 5, "componentType": 5126, "count": 2, "type": "VEC4" },
    { "bufferView": 6, "componentType": 5126, "count": 2, "type": "SCALAR",
      "min": [0.0], "max": [0.5] },
    { "bufferView": 7, "componentType": 5126, "count": 2, "type": "VEC3" },
    { "bufferView": 8, "componentType": 5126, "count": 2, "type": "SCALAR",
      "min": [0.0], "max": [2.0] },
    { "bufferView": 9, "componentType": 5126, "count": 2, "type": "VEC3" },
    { "bufferView": 10, "componentType": 5126, "count": 2, "type": "SCALAR",
      "min": [0.0], "max": [0.25] },
    { "bufferView": 11, "componentType": 5126, "count": 2, "type": "VEC4" },
    { "bufferView": 12, "componentType": 5126, "count": 2, "type": "MAT4" },
    { "bufferView": 13, "componentType": 5121, "count": 3, "type": "VEC4" },
    { "bufferView": 14, "componentType": 5126, "count": 3, "type": "VEC4" }
  ],
  "meshes": [
    { "name": "triangle",
      "primitives": [
        { "attributes": { "POSITION": 0, "JOINTS_0": 13, "WEIGHTS_0": 14 },
          "indices": 1 } ] }
  ],
  "nodes": [
    { "name": "root", "translation": [1.0, 2.0, 3.0], "children": [1] },
    { "name": "hips", "rotation": [0.0, 0.70710678, 0.0, 0.70710678],
      "scale": [2.0, 3.0, 4.0], "children": [3, 2] },
    { "name": "arm_l", "mesh": 0 },
    { "name": "arm_r", "mesh": 0, "skin": 0 },
    { "name": "spare", "translation": [-1.0, 0.0, 0.5] }
  ],
  "skins": [
    { "name": "rig", "joints": [2, 1], "inverseBindMatrices": 12 },
    { "name": "partial", "joints": [0] }
  ],
  "animations": [
    {
      "name": "Idle",
      "samplers": [
        { "input": 2, "output": 3, "interpolation": "LINEAR" },
        { "input": 4, "output": 5, "interpolation": "STEP" },
        { "input": 6, "output": 7, "interpolation": "LINEAR" }
      ],
      "channels": [
        { "sampler": 0, "target": { "node": 1, "path": "translation" } },
        { "sampler": 1, "target": { "node": 2, "path": "rotation" } },
        { "sampler": 2, "target": { "node": 3, "path": "scale" } }
      ]
    },
    {
      "name": "Walk",
      "samplers": [
        { "input": 8, "output": 9, "interpolation": "STEP" },
        { "input": 10, "output": 11, "interpolation": "LINEAR" }
      ],
      "channels": [
        { "sampler": 0, "target": { "node": 1, "path": "translation" } },
        { "sampler": 1, "target": { "node": 0, "path": "rotation" } }
      ]
    }
  ])";

    if (selection != SceneSelection::NoScenes) {
        json += R"(,
  "scenes": [
    { "name": "primary", "nodes": [0, 4] },
    { "name": "alt", "nodes": [0] }
  ])";
    }
    if (selection == SceneSelection::SelectedSecondScene) {
        json += R"(,
  "scene": 1)";
    }
    json += "\n}\n";
    return json;
}

bool writeCharacterFixture(const FixtureDirectory& directory,
                           SceneSelection selection,
                           std::string& outFail) {
    std::string name = "character_first_scene.gltf";
    if (selection == SceneSelection::SelectedSecondScene) {
        name = "character_selected_scene.gltf";
    } else if (selection == SceneSelection::NoScenes) {
        name = "character_no_scenes.gltf";
    }
    const std::vector<std::uint8_t> binary = characterSceneBinary();
    if (!expect(binary.size() == kFixtureBufferBytes,
                "authored fixture buffer is " + std::to_string(binary.size()) +
                    " bytes, expected " + std::to_string(kFixtureBufferBytes),
                outFail)) {
        return false;
    }
    if (!expect(directory.writeBytes("character_scene.bin", binary),
                "could not write character_scene.bin", outFail)) {
        return false;
    }
    if (!expect(directory.writeText(name, characterSceneJson(selection)),
                "could not write " + name, outFail)) {
        return false;
    }
    return true;
}

// A tiny cubic fixture: one CUBICSPLINE translation, rotation and scale
// sampler over three nodes. The binary layout is explicit little-endian and the
// bufferView offsets in cubicAnimationJson below match it exactly.
constexpr std::size_t kCubicFixtureBufferBytes = 264u;

std::vector<std::uint8_t> cubicAnimationBinary() {
    LittleEndianBuffer buffer;
    // bufferView 0 @0: translation input, 2 * FLOAT.
    buffer.addF32(0.0f);
    buffer.addF32(2.0f);
    // bufferView 1 @8: translation output, 2 keys * [in-tangent, value,
    // out-tangent] as VEC3.
    buffer.addVec3({0.0f, 0.0f, 0.0f});
    buffer.addVec3({1.0f, 2.0f, 3.0f});
    buffer.addVec3({1.0f, 1.5f, 2.0f});
    buffer.addVec3({-0.5f, -0.75f, -1.0f});
    buffer.addVec3({5.0f, 8.0f, 11.0f});
    buffer.addVec3({0.0f, 0.0f, 0.0f});
    // bufferView 2 @80: rotation input, 2 * FLOAT.
    buffer.addF32(0.0f);
    buffer.addF32(2.0f);
    // bufferView 3 @88: rotation output, 2 keys * 3 entries as xyzw VEC4. The
    // out-tangent of key 0 is 3x the chord / interval and the in-tangent of
    // key 1 is -1x, which makes the curve pass through the authored -120 degree
    // rotation at the midpoint.
    buffer.addVec4({0.0f, 0.0f, 0.0f, 0.0f});
    buffer.addVec4({0.0f, 0.0f, 0.0f, 1.0f});
    buffer.addVec4({0.0f, 0.0f, -1.5f * kSqrtThreeOverTwo, -0.75f});
    buffer.addVec4({0.0f, 0.0f, 0.5f * kSqrtThreeOverTwo, 0.25f});
    buffer.addVec4({0.0f, 0.0f, -kSqrtThreeOverTwo, 0.5f});
    buffer.addVec4({0.0f, 0.0f, 0.0f, 0.0f});
    // bufferView 4 @184: scale input, 2 * FLOAT.
    buffer.addF32(0.0f);
    buffer.addF32(1.0f);
    // bufferView 5 @192: scale output, 2 keys * 3 entries as VEC3.
    buffer.addVec3({0.0f, 0.0f, 0.0f});
    buffer.addVec3({1.0f, 2.0f, 3.0f});
    buffer.addVec3({2.0f, 1.0f, -1.0f});
    buffer.addVec3({-1.0f, -0.5f, 0.5f});
    buffer.addVec3({5.0f, 4.0f, 1.0f});
    buffer.addVec3({0.0f, 0.0f, 0.0f});
    return buffer.bytes();
}

std::string cubicAnimationJson() {
    return std::string(R"({
  "asset": { "version": "2.0" },
  "buffers": [ { "uri": "cubic_animation.bin", "byteLength": 264 } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 8, "byteLength": 72 },
    { "buffer": 0, "byteOffset": 80, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 88, "byteLength": 96 },
    { "buffer": 0, "byteOffset": 184, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 192, "byteLength": 72 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR",
      "min": [0.0], "max": [2.0] },
    { "bufferView": 1, "componentType": 5126, "count": 6, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 2, "type": "SCALAR",
      "min": [0.0], "max": [2.0] },
    { "bufferView": 3, "componentType": 5126, "count": 6, "type": "VEC4" },
    { "bufferView": 4, "componentType": 5126, "count": 2, "type": "SCALAR",
      "min": [0.0], "max": [1.0] },
    { "bufferView": 5, "componentType": 5126, "count": 6, "type": "VEC3" }
  ],
  "nodes": [
    { "name": "translation_node" },
    { "name": "rotation_node" },
    { "name": "scale_node" }
  ],
  "animations": [
    {
      "name": "Cubic",
      "samplers": [
        { "input": 0, "output": 1, "interpolation": "CUBICSPLINE" },
        { "input": 2, "output": 3, "interpolation": "CUBICSPLINE" },
        { "input": 4, "output": 5, "interpolation": "CUBICSPLINE" }
      ],
      "channels": [
        { "sampler": 0, "target": { "node": 0, "path": "translation" } },
        { "sampler": 1, "target": { "node": 1, "path": "rotation" } },
        { "sampler": 2, "target": { "node": 2, "path": "scale" } }
      ]
    }
  ],
  "scenes": [ { "name": "cubic", "nodes": [0, 1, 2] } ]
}
)");
}

bool writeCubicAnimationFixture(const FixtureDirectory& directory,
                                std::string& outFail) {
    const std::vector<std::uint8_t> binary = cubicAnimationBinary();
    if (!expect(binary.size() == kCubicFixtureBufferBytes,
                "authored cubic buffer is " + std::to_string(binary.size()) +
                    " bytes, expected " +
                    std::to_string(kCubicFixtureBufferBytes),
                outFail)) {
        return false;
    }
    if (!expect(directory.writeBytes("cubic_animation.bin", binary),
                "could not write cubic_animation.bin", outFail)) {
        return false;
    }
    if (!expect(directory.writeText("cubic_animation.gltf",
                                    cubicAnimationJson()),
                "could not write cubic_animation.gltf", outFail)) {
        return false;
    }
    return true;
}

bool importSceneFile(const std::filesystem::path& path,
                     SceneImport& out,
                     std::string& outFail) {
    const auto loaded = engine::render::gltf::loader::tryLoad(path.string());
    if (!expect(loaded.has_value(),
                "loader::tryLoad rejected the authored fixture " + path.string(),
                outFail)) {
        return false;
    }
    // fastgltf's own stricter structural validation of the parsed asset. It is
    // a parse-level check, not proof of spec conformance or of rendering.
    const fastgltf::Error validation = fastgltf::validate(loaded->asset);
    if (!expect(validation == fastgltf::Error::None,
                "fastgltf::validate rejected the authored fixture " + path.string() +
                    ": " + engine::render::gltf::loader::errorName(validation),
                outFail)) {
        return false;
    }
    fastgltf::DefaultBufferDataAdapter adapter{};
    engine::render::gltf::model::buildSceneData(loaded->asset,
                                                adapter,
                                                out.nodes,
                                                &out.names,
                                                out.children,
                                                out.nodeMesh,
                                                out.nodeSkin,
                                                out.sceneRoots,
                                                out.skins,
                                                out.animations);
    return true;
}

bool caseHierarchyAndSceneSelection(std::string& outFail) {
    FixtureDirectory directory("phlosion-gltf-scene-import-hierarchy");
    if (!expect(directory.ready(), "fixture directory was not created", outFail)) {
        return false;
    }
    if (!writeCharacterFixture(directory, SceneSelection::FirstSceneFallback, outFail) ||
        !writeCharacterFixture(directory, SceneSelection::SelectedSecondScene, outFail) ||
        !writeCharacterFixture(directory, SceneSelection::NoScenes, outFail)) {
        return false;
    }

    SceneImport first;
    if (!importSceneFile(directory.file("character_first_scene.gltf"), first, outFail)) {
        return false;
    }
    if (!expect(first.nodes.size() == 5u,
                "node count " + std::to_string(first.nodes.size()) + " != 5",
                outFail)) {
        return false;
    }
    if (!expect(first.names.size() == 5u && first.names[0] == "root" &&
                    first.names[1] == "hips" && first.names[2] == "arm_l" &&
                    first.names[3] == "arm_r" && first.names[4] == "spare",
                "node names must keep the authored order", outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(first.nodes[0].t, glm::vec3(1.0f, 2.0f, 3.0f)),
                "root translation " + vecText(first.nodes[0].t) + " != (1, 2, 3)",
                outFail)) {
        return false;
    }
    if (!expect(!first.nodes[0].hasMatrix && !first.nodes[1].hasMatrix,
                "authored TRS nodes must not report a matrix", outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(first.nodes[1].r,
                            glm::quat(kSqrtHalf, 0.0f, kSqrtHalf, 0.0f)),
                "hips rotation " + quatXyzwText(first.nodes[1].r) +
                    " != xyzw(0, 0.70710678, 0, 0.70710678)",
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(first.nodes[1].s, glm::vec3(2.0f, 3.0f, 4.0f)),
                "hips scale " + vecText(first.nodes[1].s) + " != (2, 3, 4)",
                outFail)) {
        return false;
    }
    if (!expect(first.children.size() == 5u,
                "child list count must match the node count", outFail)) {
        return false;
    }
    if (!expect(first.children[0] == std::vector<int>({1}),
                "root children must be the authored [1] list", outFail)) {
        return false;
    }
    if (!expect(first.children[1] == std::vector<int>({3, 2}),
                "hips children must keep the authored [3, 2] order", outFail)) {
        return false;
    }
    if (!expect(first.children[2].empty() && first.children[3].empty() &&
                    first.children[4].empty(),
                "leaf nodes must report no children", outFail)) {
        return false;
    }
    if (!expect(first.nodeMesh == std::vector<int>({-1, -1, 0, 0, -1}),
                "nodes 2 and 3 must both reference the skinned mesh 0", outFail)) {
        return false;
    }
    if (!expect(first.nodeSkin == std::vector<int>({-1, -1, -1, 0, -1}),
                "only node 3 must own skin 0", outFail)) {
        return false;
    }
    if (!expect(first.sceneRoots == std::vector<int>({0, 4}),
                "without a \"scene\" member the first scene roots [0, 4] apply",
                outFail)) {
        return false;
    }

    SceneImport selected;
    if (!importSceneFile(directory.file("character_selected_scene.gltf"), selected,
                         outFail)) {
        return false;
    }
    if (!expect(selected.sceneRoots == std::vector<int>({0}),
                "an authored \"scene\": 1 must select the second scene roots [0]",
                outFail)) {
        return false;
    }
    if (!expect(selected.nodes.size() == 5u,
                "a non-first scene still imports every node", outFail)) {
        return false;
    }

    SceneImport noScenes;
    if (!importSceneFile(directory.file("character_no_scenes.gltf"), noScenes,
                         outFail)) {
        return false;
    }
    if (!expect(noScenes.sceneRoots.empty() && noScenes.nodes.size() == 5u,
                "a document without scenes imports nodes with no scene roots",
                outFail)) {
        return false;
    }
    return true;
}

bool caseSkins(std::string& outFail) {
    FixtureDirectory directory("phlosion-gltf-scene-import-skins");
    if (!expect(directory.ready(), "fixture directory was not created", outFail)) {
        return false;
    }
    if (!writeCharacterFixture(directory, SceneSelection::FirstSceneFallback, outFail)) {
        return false;
    }

    SceneImport imported;
    if (!importSceneFile(directory.file("character_first_scene.gltf"), imported,
                         outFail)) {
        return false;
    }
    if (!expect(imported.skins.size() == 2u,
                "skin count " + std::to_string(imported.skins.size()) + " != 2",
                outFail)) {
        return false;
    }

    if (!expect(imported.skins[0].joints == std::vector<int>({2, 1}),
                "skin 0 joints must keep the authored [2, 1] order", outFail)) {
        return false;
    }
    if (!expect(imported.skins[0].inverseBind.size() == 2u,
                "skin 0 inverse bind count " +
                    std::to_string(imported.skins[0].inverseBind.size()) + " != 2",
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(imported.skins[0].inverseBind[0], authoredFirstInverseBind()),
                "skin 0 inverse bind 0 " + matText(imported.skins[0].inverseBind[0]) +
                    " != " + matText(authoredFirstInverseBind()),
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(imported.skins[0].inverseBind[1], authoredSecondInverseBind()),
                "skin 0 inverse bind 1 " + matText(imported.skins[0].inverseBind[1]) +
                    " != " + matText(authoredSecondInverseBind()),
                outFail)) {
        return false;
    }

    if (!expect(imported.skins[1].joints == std::vector<int>({0}),
                "skin 1 joints must be the authored [0]", outFail)) {
        return false;
    }
    if (!expect(imported.skins[1].inverseBind.size() == 1u &&
                    nearlyEqual(imported.skins[1].inverseBind[0], glm::mat4(1.0f)),
                "omitted inverse bind matrices keep the current identity behavior",
                outFail)) {
        return false;
    }
    return true;
}

// The positive fixture must be structurally coherent, not merely parseable:
// the skinned primitive carries JOINTS_0/WEIGHTS_0, joint indices address the
// skin's joints, weights are normalized per vertex, mesh and skin are assigned
// together, and every scene root is a parentless node.
bool caseSkinnedFixtureCoherence(std::string& outFail) {
    FixtureDirectory directory("phlosion-gltf-scene-import-skinned");
    if (!expect(directory.ready(), "fixture directory was not created", outFail)) {
        return false;
    }
    if (!writeCharacterFixture(directory, SceneSelection::FirstSceneFallback, outFail)) {
        return false;
    }
    const auto loaded = engine::render::gltf::loader::tryLoad(
        directory.file("character_first_scene.gltf").string());
    if (!expect(loaded.has_value(), "loading the authored fixture failed", outFail)) {
        return false;
    }
    const fastgltf::Asset& asset = loaded->asset;

    if (!expect(asset.meshes.size() == 1u && asset.skins.size() == 2u,
                "the fixture must declare one mesh and two skins", outFail)) {
        return false;
    }
    const auto& primitive = asset.meshes[0].primitives[0];
    const auto jointsAttribute = primitive.findAttribute("JOINTS_0");
    const auto weightsAttribute = primitive.findAttribute("WEIGHTS_0");
    if (!expect(jointsAttribute != primitive.attributes.end() &&
                    weightsAttribute != primitive.attributes.end(),
                "the skinned primitive must expose JOINTS_0 and WEIGHTS_0",
                outFail)) {
        return false;
    }
    const fastgltf::Accessor& jointsAccessor =
        asset.accessors[jointsAttribute->accessorIndex];
    const fastgltf::Accessor& weightsAccessor =
        asset.accessors[weightsAttribute->accessorIndex];
    if (!expect(jointsAccessor.type == fastgltf::AccessorType::Vec4 &&
                    jointsAccessor.componentType == fastgltf::ComponentType::UnsignedByte &&
                    jointsAccessor.count == 3u,
                "JOINTS_0 must hold three VEC4 UNSIGNED_BYTE values", outFail)) {
        return false;
    }
    if (!expect(weightsAccessor.type == fastgltf::AccessorType::Vec4 &&
                    weightsAccessor.componentType == fastgltf::ComponentType::Float &&
                    weightsAccessor.count == 3u,
                "WEIGHTS_0 must hold three VEC4 float values", outFail)) {
        return false;
    }
    if (!expect(asset.nodes[3].meshIndex.has_value() &&
                    asset.nodes[3].meshIndex.value() == 0u &&
                    asset.nodes[3].skinIndex.has_value() &&
                    asset.nodes[3].skinIndex.value() == 0u,
                "the skinned node must reference mesh 0 and skin 0 together",
                outFail)) {
        return false;
    }

    fastgltf::DefaultBufferDataAdapter adapter{};
    const std::size_t jointCount = asset.skins[0].joints.size();
    bool jointsInRange = true;
    fastgltf::iterateAccessorWithIndex<fastgltf::math::u8vec4>(
        asset,
        jointsAccessor,
        [&](fastgltf::math::u8vec4 value, std::size_t) {
            for (std::size_t component = 0; component < 4u; ++component) {
                if (value[component] >= jointCount) {
                    jointsInRange = false;
                }
            }
        },
        adapter);
    if (!expect(jointCount == 2u && jointsInRange,
                "JOINTS_0 indices must address the two joints of skin 0",
                outFail)) {
        return false;
    }

    float minWeightSum = 2.0f;
    float maxWeightSum = 0.0f;
    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
        asset,
        weightsAccessor,
        [&](fastgltf::math::fvec4 value, std::size_t) {
            const float sum =
                value[0] + value[1] + value[2] + value[3];
            minWeightSum = (std::min)(minWeightSum, sum);
            maxWeightSum = (std::max)(maxWeightSum, sum);
        },
        adapter);
    if (!expect(std::fabs(minWeightSum - 1.0f) <= 1e-5f &&
                    std::fabs(maxWeightSum - 1.0f) <= 1e-5f,
                "WEIGHTS_0 must be normalized per vertex", outFail)) {
        return false;
    }

    std::vector<int> parentOf(asset.nodes.size(), -1);
    for (std::size_t node = 0; node < asset.nodes.size(); ++node) {
        for (const std::size_t child : asset.nodes[node].children) {
            parentOf[child] = static_cast<int>(node);
        }
    }
    for (const auto& scene : asset.scenes) {
        for (const std::size_t root : scene.nodeIndices) {
            if (!expect(parentOf[root] == -1,
                        "scene root node " + std::to_string(root) +
                            " must not be a child of another node",
                        outFail)) {
                return false;
            }
        }
    }
    return true;
}

bool caseAnimations(std::string& outFail) {
    FixtureDirectory directory("phlosion-gltf-scene-import-animations");
    if (!expect(directory.ready(), "fixture directory was not created", outFail)) {
        return false;
    }
    if (!writeCharacterFixture(directory, SceneSelection::FirstSceneFallback, outFail)) {
        return false;
    }

    SceneImport imported;
    if (!importSceneFile(directory.file("character_first_scene.gltf"), imported,
                         outFail)) {
        return false;
    }
    if (!expect(imported.animations.size() == 2u,
                "clip count " + std::to_string(imported.animations.size()) + " != 2",
                outFail)) {
        return false;
    }

    const AnimationClip& idle = imported.animations[0];
    const AnimationClip& walk = imported.animations[1];
    if (!expect(idle.name == "Idle" && walk.name == "Walk",
                "clip names must be \"Idle\" and \"Walk\"", outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(idle.durationSec, 1.5f),
                "Idle duration " + floatText(idle.durationSec) +
                    " != 1.5 (last key across all samplers)",
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(walk.durationSec, 2.0f),
                "Walk duration " + floatText(walk.durationSec) + " != 2.0",
                outFail)) {
        return false;
    }
    if (!expect(idle.samplers.size() == 3u && walk.samplers.size() == 2u,
                "sampler counts must match the authored clips", outFail)) {
        return false;
    }

    if (!expect(idle.samplers[0].interpolation == "LINEAR" &&
                    !idle.samplers[0].isVec4 &&
                    idle.samplers[0].inputs == std::vector<float>({0.0f, 1.5f}),
                "Idle translation sampler must be LINEAR with keys [0, 1.5]",
                outFail)) {
        return false;
    }
    if (!expect(idle.samplers[0].outputs.size() == 2u &&
                    nearlyEqual(idle.samplers[0].outputs[0],
                                glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)) &&
                    nearlyEqual(idle.samplers[0].outputs[1],
                                glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)),
                "Idle translation values must be (0, 0, 0, 0) and (0, 1, 0, 0)",
                outFail)) {
        return false;
    }
    if (!expect(idle.samplers[1].interpolation == "STEP" && idle.samplers[1].isVec4 &&
                    idle.samplers[1].inputs == std::vector<float>({0.0f, 0.75f}),
                "Idle rotation sampler must be STEP with keys [0, 0.75]", outFail)) {
        return false;
    }
    if (!expect(idle.samplers[1].outputs.size() == 2u &&
                    nearlyEqual(idle.samplers[1].outputs[0],
                                glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)) &&
                    nearlyEqual(idle.samplers[1].outputs[1],
                                glm::vec4(0.0f, kSqrtHalf, 0.0f, kSqrtHalf)),
                "Idle rotation values must keep the authored xyzw order", outFail)) {
        return false;
    }
    if (!expect(idle.samplers[2].interpolation == "LINEAR" &&
                    !idle.samplers[2].isVec4 &&
                    idle.samplers[2].inputs == std::vector<float>({0.0f, 0.5f}),
                "Idle scale sampler must be LINEAR with keys [0, 0.5]", outFail)) {
        return false;
    }
    if (!expect(idle.samplers[2].outputs.size() == 2u &&
                    nearlyEqual(idle.samplers[2].outputs[0],
                                glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)) &&
                    nearlyEqual(idle.samplers[2].outputs[1],
                                glm::vec4(2.0f, 2.0f, 2.0f, 0.0f)),
                "Idle scale values must be (1, 1, 1, 0) and (2, 2, 2, 0)", outFail)) {
        return false;
    }
    if (!expect(walk.samplers[0].interpolation == "STEP" &&
                    !walk.samplers[0].isVec4 &&
                    walk.samplers[0].inputs == std::vector<float>({0.0f, 2.0f}) &&
                    walk.samplers[0].outputs.size() == 2u &&
                    nearlyEqual(walk.samplers[0].outputs[0],
                                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)) &&
                    nearlyEqual(walk.samplers[0].outputs[1],
                                glm::vec4(2.0f, 0.0f, 0.0f, 0.0f)),
                "Walk translation sampler must be STEP with keys [0, 2] and those "
                "values",
                outFail)) {
        return false;
    }
    if (!expect(walk.samplers[1].interpolation == "LINEAR" &&
                    walk.samplers[1].isVec4 &&
                    walk.samplers[1].inputs == std::vector<float>({0.0f, 0.25f}) &&
                    walk.samplers[1].outputs.size() == 2u &&
                    nearlyEqual(walk.samplers[1].outputs[1],
                                glm::vec4(0.0f, 0.0f, kSqrtHalf, kSqrtHalf)),
                "Walk rotation sampler must be LINEAR with keys [0, 0.25] and xyzw "
                "values",
                outFail)) {
        return false;
    }

    if (!expect(idle.channels.size() == 3u && walk.channels.size() == 2u,
                "channel counts must match the authored clips", outFail)) {
        return false;
    }
    const bool idleChannelsValid =
        idle.channels[0].samplerIndex == 0 && idle.channels[0].targetNode == 1 &&
        idle.channels[0].path == ChannelPath::Translation &&
        idle.channels[1].samplerIndex == 1 && idle.channels[1].targetNode == 2 &&
        idle.channels[1].path == ChannelPath::Rotation &&
        idle.channels[2].samplerIndex == 2 && idle.channels[2].targetNode == 3 &&
        idle.channels[2].path == ChannelPath::Scale;
    if (!expect(idleChannelsValid,
                "Idle channels must map samplers 0/1/2 to nodes 1/2/3 as "
                "translation/rotation/scale",
                outFail)) {
        return false;
    }
    const bool walkChannelsValid =
        walk.channels[0].samplerIndex == 0 && walk.channels[0].targetNode == 1 &&
        walk.channels[0].path == ChannelPath::Translation &&
        walk.channels[1].samplerIndex == 1 && walk.channels[1].targetNode == 0 &&
        walk.channels[1].path == ChannelPath::Rotation;
    if (!expect(walkChannelsValid,
                "Walk channels must map samplers 0/1 to nodes 1/0 as "
                "translation/rotation",
                outFail)) {
        return false;
    }
    return true;
}

bool caseReimportIntoPrefilledContainers(std::string& outFail) {
    FixtureDirectory directory("phlosion-gltf-scene-import-reimport");
    if (!expect(directory.ready(), "fixture directory was not created", outFail)) {
        return false;
    }
    if (!writeCharacterFixture(directory, SceneSelection::FirstSceneFallback, outFail)) {
        return false;
    }

    const auto loaded = engine::render::gltf::loader::tryLoad(
        directory.file("character_first_scene.gltf").string());
    if (!expect(loaded.has_value(), "loading the authored fixture failed", outFail)) {
        return false;
    }

    // Stale contents and a nullptr name list must not change the result.
    std::vector<NodeTRS> nodes(2);
    std::vector<std::vector<int>> children(3, std::vector<int>{7});
    std::vector<int> nodeMesh(1, 42);
    std::vector<int> nodeSkin(1, 42);
    std::vector<int> sceneRoots(4, 9);
    std::vector<SkinData> skins(1);
    std::vector<AnimationClip> animations(3);
    fastgltf::DefaultBufferDataAdapter adapter{};
    engine::render::gltf::model::buildSceneData(loaded->asset,
                                                adapter,
                                                nodes,
                                                nullptr,
                                                children,
                                                nodeMesh,
                                                nodeSkin,
                                                sceneRoots,
                                                skins,
                                                animations);

    if (!expect(nodes.size() == 5u && children.size() == 5u &&
                    nodeMesh.size() == 5u && nodeSkin.size() == 5u &&
                    sceneRoots.size() == 2u && skins.size() == 2u &&
                    animations.size() == 2u,
                "re-import must replace container sizes exactly", outFail)) {
        return false;
    }
    if (!expect(sceneRoots == std::vector<int>({0, 4}) &&
                    children[1] == std::vector<int>({3, 2}) && nodeMesh[2] == 0 &&
                    nodeSkin[3] == 0 &&
                    nearlyEqual(nodes[1].s, glm::vec3(2.0f, 3.0f, 4.0f)) &&
                    animations[0].name == "Idle" && animations[1].name == "Walk",
                "re-import must replace stale contents, not accumulate them",
                outFail)) {
        return false;
    }
    if (!expect(skins[0].joints == std::vector<int>({2, 1}),
                "re-import must replace stale skin joints", outFail)) {
        return false;
    }
    return true;
}

bool caseLoaderFailures(std::string& outFail) {
    FixtureDirectory directory("phlosion-gltf-scene-import-failures");
    if (!expect(directory.ready(), "fixture directory was not created", outFail)) {
        return false;
    }

    const auto missingFile = engine::render::gltf::loader::tryLoad(
        directory.file("absent_model.gltf").string());
    if (!expect(!missingFile.has_value(), "a missing file must fail at tryLoad",
                outFail)) {
        return false;
    }

    if (!expect(directory.writeText("malformed.gltf",
                                    "{ \"asset\": { \"version\": \"2.0\" }, "
                                    "\"nodes\": [ "),
                "could not write malformed.gltf", outFail)) {
        return false;
    }
    const auto malformed = engine::render::gltf::loader::tryLoad(
        directory.file("malformed.gltf").string());
    if (!expect(!malformed.has_value(), "malformed JSON must fail at tryLoad",
                outFail)) {
        return false;
    }

    // The referenced buffer file is never written.
    if (!expect(directory.writeText(
                    "missing_buffer.gltf",
                    "{\n"
                    "  \"asset\": { \"version\": \"2.0\" },\n"
                    "  \"buffers\": [ { \"uri\": \"absent_buffer.bin\", \"byteLength\": "
                    "4 } ],\n"
                    "  \"bufferViews\": [ { \"buffer\": 0, \"byteOffset\": 0, "
                    "\"byteLength\": 4 } ],\n"
                    "  \"accessors\": [ { \"bufferView\": 0, \"componentType\": 5126, "
                    "\"count\": 1, \"type\": \"SCALAR\" } ]\n"
                    "}\n"),
                "could not write missing_buffer.gltf", outFail)) {
        return false;
    }
    const auto missingBuffer = engine::render::gltf::loader::tryLoad(
        directory.file("missing_buffer.gltf").string());
    if (!expect(!missingBuffer.has_value(),
                "a missing external buffer must fail at tryLoad", outFail)) {
        return false;
    }
    return true;
}

bool caseEmptyDocument(std::string& outFail) {
    FixtureDirectory directory("phlosion-gltf-scene-import-empty");
    if (!expect(directory.ready(), "fixture directory was not created", outFail)) {
        return false;
    }
    if (!expect(directory.writeText("empty.gltf",
                                    "{ \"asset\": { \"version\": \"2.0\" } }\n"),
                "could not write empty.gltf", outFail)) {
        return false;
    }

    SceneImport imported;
    if (!importSceneFile(directory.file("empty.gltf"), imported, outFail)) {
        return false;
    }
    if (!expect(imported.nodes.empty() && imported.names.empty() &&
                    imported.children.empty() && imported.nodeMesh.empty() &&
                    imported.nodeSkin.empty() && imported.sceneRoots.empty() &&
                    imported.skins.empty() && imported.animations.empty(),
                "a document without content must import empty containers", outFail)) {
        return false;
    }
    return true;
}

bool caseCubicAnimationImport(std::string& outFail) {
    FixtureDirectory directory("phlosion-gltf-scene-import-cubic");
    if (!expect(directory.ready(), "fixture directory was not created", outFail)) {
        return false;
    }
    if (!writeCubicAnimationFixture(directory, outFail)) {
        return false;
    }

    SceneImport imported;
    if (!importSceneFile(directory.file("cubic_animation.gltf"), imported,
                         outFail)) {
        return false;
    }
    if (!expect(imported.animations.size() == 1u,
                "cubic fixture must import exactly one clip", outFail)) {
        return false;
    }
    const AnimationClip& clip = imported.animations[0];
    if (!expect(clip.name == "Cubic" && nearlyEqual(clip.durationSec, 2.0f),
                "cubic clip identity must come from the authored document",
                outFail)) {
        return false;
    }
    if (!expect(clip.samplers.size() == 3u && clip.channels.size() == 3u,
                "cubic clip must keep three samplers and three channels",
                outFail)) {
        return false;
    }
    if (!expect(imported.nodes.size() == 3u &&
                    imported.sceneRoots == std::vector<int>({0, 1, 2}),
                "cubic fixture nodes and scene roots changed", outFail)) {
        return false;
    }
    const bool channelMapping =
        clip.channels[0].samplerIndex == 0 && clip.channels[0].targetNode == 0 &&
        clip.channels[0].path == ChannelPath::Translation &&
        clip.channels[1].samplerIndex == 1 && clip.channels[1].targetNode == 1 &&
        clip.channels[1].path == ChannelPath::Rotation &&
        clip.channels[2].samplerIndex == 2 && clip.channels[2].targetNode == 2 &&
        clip.channels[2].path == ChannelPath::Scale;
    if (!expect(channelMapping,
                "cubic channels must map samplers 0/1/2 to nodes 0/1/2 as "
                "translation/rotation/scale",
                outFail)) {
        return false;
    }

    // The importer must keep the raw per-key [in-tangent, value, out-tangent]
    // triplets: three outputs per key, tangents included, and the authored
    // xyzw rotation order.
    const AnimationSampler& translation = clip.samplers[0];
    if (!expect(translation.interpolation == "CUBICSPLINE" &&
                    !translation.isVec4 &&
                    translation.inputs == std::vector<float>({0.0f, 2.0f}),
                "cubic translation sampler identity changed", outFail)) {
        return false;
    }
    if (!expect(translation.outputs.size() == 6u &&
                    nearlyEqual(translation.outputs[0],
                                glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)) &&
                    nearlyEqual(translation.outputs[1],
                                glm::vec4(1.0f, 2.0f, 3.0f, 0.0f)) &&
                    nearlyEqual(translation.outputs[2],
                                glm::vec4(1.0f, 1.5f, 2.0f, 0.0f)) &&
                    nearlyEqual(translation.outputs[3],
                                glm::vec4(-0.5f, -0.75f, -1.0f, 0.0f)) &&
                    nearlyEqual(translation.outputs[4],
                                glm::vec4(5.0f, 8.0f, 11.0f, 0.0f)) &&
                    nearlyEqual(translation.outputs[5],
                                glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)),
                "cubic translation outputs must keep the raw tangent triplets",
                outFail)) {
        return false;
    }

    const AnimationSampler& rotation = clip.samplers[1];
    if (!expect(rotation.interpolation == "CUBICSPLINE" && rotation.isVec4 &&
                    rotation.inputs == std::vector<float>({0.0f, 2.0f}) &&
                    rotation.outputs.size() == 6u &&
                    nearlyEqual(rotation.outputs[1],
                                glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)) &&
                    nearlyEqual(rotation.outputs[2],
                                glm::vec4(0.0f, 0.0f,
                                          -1.5f * kSqrtThreeOverTwo, -0.75f)) &&
                    nearlyEqual(rotation.outputs[3],
                                glm::vec4(0.0f, 0.0f,
                                          0.5f * kSqrtThreeOverTwo, 0.25f)) &&
                    nearlyEqual(rotation.outputs[4],
                                glm::vec4(0.0f, 0.0f,
                                          -kSqrtThreeOverTwo, 0.5f)),
                "cubic rotation outputs must keep the xyzw tangent triplets",
                outFail)) {
        return false;
    }

    const AnimationSampler& scale = clip.samplers[2];
    if (!expect(scale.interpolation == "CUBICSPLINE" && !scale.isVec4 &&
                    scale.inputs == std::vector<float>({0.0f, 1.0f}) &&
                    scale.outputs.size() == 6u &&
                    nearlyEqual(scale.outputs[1],
                                glm::vec4(1.0f, 2.0f, 3.0f, 0.0f)) &&
                    nearlyEqual(scale.outputs[2],
                                glm::vec4(2.0f, 1.0f, -1.0f, 0.0f)) &&
                    nearlyEqual(scale.outputs[3],
                                glm::vec4(-1.0f, -0.5f, 0.5f, 0.0f)) &&
                    nearlyEqual(scale.outputs[4],
                                glm::vec4(5.0f, 4.0f, 1.0f, 0.0f)),
                "cubic scale outputs must keep the raw tangent triplets",
                outFail)) {
        return false;
    }

    // Hand-computed Hermite results from the imported raw tangents.
    //
    // Translation: chord (4,6,8) over 2 s with interval-scaled tangents of
    // +0.5x and -0.25x the chord. At t = 0.5 s (a = 0.25) the weight is
    // h01 + 0.5*h10 - 0.25*h11 = 0.23828125, giving
    // (1,2,3) + 0.23828125 * (4,6,8) = (1.953125, 3.4296875, 4.90625). At
    // t = 1.0 s (a = 0.5) the weight is 0.59375, giving
    // (1,2,3) + 0.59375 * (4,6,8) = (3.375, 5.5625, 7.75).
    //
    // Scale: chord (4,2,-2) over 1 s with +0.5x and -0.25x tangents. At
    // t = 0.5 s (a = 0.5) the weight is h01 + 0.5*h10 - 0.25*h11 = 0.59375,
    // giving (1,2,3) + 0.59375 * (4,2,-2) = (3.375, 3.1875, 1.8125).
    //
    // Rotation: the 3x/-1x tangents put the curve exactly on the authored key
    // value at t = 1.0 s, the -120 degree rotation about +Z in xyzw order.
    const PoseSource source{imported.nodes,
                            imported.children,
                            imported.sceneRoots,
                            imported.skins,
                            imported.animations,
                            imported.names};
    std::vector<NodeTRS> locals;
    std::vector<glm::mat4> globals;

    samplePose(source, 0.5f, 0, locals, globals);
    if (!expect(nearlyEqual(locals[0].t,
                            glm::vec3(1.953125f, 3.4296875f, 4.90625f)),
                "imported cubic translation at 0.5 s did not match the "
                "hand-computed Hermite value",
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(locals[2].s,
                            glm::vec3(3.375f, 3.1875f, 1.8125f)),
                "imported cubic scale at 0.5 s did not match the hand-computed "
                "Hermite value",
                outFail)) {
        return false;
    }

    samplePose(source, 1.0f, 0, locals, globals);
    if (!expect(nearlyEqual(locals[0].t,
                            glm::vec3(3.375f, 5.5625f, 7.75f)),
                "imported cubic translation at 1.0 s did not match the "
                "hand-computed Hermite value",
                outFail)) {
        return false;
    }
    if (!expect(nearlyEqual(locals[1].r,
                            glm::quat(0.5f, 0.0f, 0.0f, -kSqrtThreeOverTwo)),
                "imported cubic rotation at 1.0 s did not match the authored "
                "xyzw quaternion",
                outFail)) {
        return false;
    }
    return true;
}

}  // namespace

bool test_gltf_scene_import_contract(std::string& outFail) {
    outFail.clear();
    if (!caseHierarchyAndSceneSelection(outFail)) {
        return false;
    }
    if (!caseSkins(outFail)) {
        return false;
    }
    if (!caseSkinnedFixtureCoherence(outFail)) {
        return false;
    }
    if (!caseAnimations(outFail)) {
        return false;
    }
    if (!caseCubicAnimationImport(outFail)) {
        return false;
    }
    if (!caseReimportIntoPrefilledContainers(outFail)) {
        return false;
    }
    if (!caseLoaderFailures(outFail)) {
        return false;
    }
    if (!caseEmptyDocument(outFail)) {
        return false;
    }
    return true;
}
