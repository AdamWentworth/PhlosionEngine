#pragma once

// Shared authoring helper for the CPU glTF scene-asset contracts.
//
// The helper writes a small public glTF document plus explicitly little-endian
// binary payloads into a uniquely owned temporary directory. Nothing here
// reimplements glTF parsing or importer math; the tests load the authored file
// through the production loadGltfSceneAsset boundary and compare the result
// against these authored values.

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "GltfImportTestSupport.h"
#include "GltfTextureFixtures.h"
#include "engine/core/Environment.h"
#include <array>
#include <cstdlib>
#include <optional>

namespace gltf_scene_contract {

class ScopedDecoderDiagnostics {
public:
    ScopedDecoderDiagnostics() {
        for (std::size_t i = 0; i < names.size(); ++i) {
            previous[i] = engine::env::get(names[i]);
            set(names[i], i == 1u ? nullptr : "0");
        }
    }
    ~ScopedDecoderDiagnostics() {
        for (std::size_t i = 0; i < names.size(); ++i)
            set(names[i], previous[i] ? previous[i]->c_str() : nullptr);
    }
    ScopedDecoderDiagnostics(const ScopedDecoderDiagnostics&) = delete;
    ScopedDecoderDiagnostics& operator=(const ScopedDecoderDiagnostics&) = delete;
private:
    static void set(const char* name, const char* value) {
#ifdef _MSC_VER
        _putenv_s(name, value ? value : "");
#else
        if (value) ::setenv(name, value, 1); else ::unsetenv(name);
#endif
    }
    static constexpr std::array<const char*, 3> names{
        "PHLOSION_GLTF_DEBUG_ALL", "PHLOSION_GLTF_DEBUG_MATCH", "PHLOSION_GLTF_DUMP_TEXTURES"};
    std::array<std::optional<std::string>, names.size()> previous;
};

// The authored rig. Node indices are fixed so the analytic expectations in the
// tests can name them directly.
struct AuthoredRig {
    static constexpr int kArmatureNode = 0;
    static constexpr int kShoulderNode = 1;
    static constexpr int kElbowNode = 2;
    static constexpr int kBlockMeshNode = 3;
    static constexpr int kPropNode = 4;

    static constexpr float kShoulderHeight = 0.5f;
    static constexpr float kElbowHeight = 0.5f;
    // Non-identity mesh-node transform, exercised by the palette composition.
    static constexpr float kMeshNodeOffsetX = 0.25f;
    static constexpr float kAnimatedTimeSec = 1.0f;
};

// The authored elbow rotation at the animated key. Half of a 90 degree turn
// keeps the bent block visibly different from the bind pose while staying easy
// to reason about analytically.
inline glm::quat authoredElbowRotation() {
    const float halfAngleRadians = 45.0f * 3.14159265358979323846f / 180.0f;
    const float s = std::sin(halfAngleRadians);
    return glm::quat(std::cos(halfAngleRadians), 0.0f, 0.0f, s);
}

// Authored local TRS for the animated elbow node at a sample time.
inline glm::mat4 authoredElbowLocal(float timeSec) {
    const float blend = timeSec <= 0.0f ? 0.0f : (timeSec >= 1.0f ? 1.0f : timeSec);
    const glm::quat rotation =
        glm::normalize(glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                  authoredElbowRotation(),
                                  blend));
    const glm::mat4 translation = glm::translate(
        glm::mat4(1.0f), glm::vec3(0.0f, AuthoredRig::kElbowHeight, 0.0f));
    return translation * glm::toMat4(rotation);
}

// Authored world matrices for the rig at a sample time, derived from the
// authored TRS rather than from imported output.
inline void authoredGlobals(float timeSec,
                            glm::mat4& outArmature,
                            glm::mat4& outShoulder,
                            glm::mat4& outElbow,
                            glm::mat4& outBlockMesh,
                            glm::mat4& outProp) {
    outArmature = glm::mat4(1.0f);
    outShoulder = glm::translate(
        glm::mat4(1.0f), glm::vec3(0.0f, AuthoredRig::kShoulderHeight, 0.0f));
    outElbow = outShoulder * authoredElbowLocal(timeSec);
    outBlockMesh = glm::translate(
        glm::mat4(1.0f), glm::vec3(AuthoredRig::kMeshNodeOffsetX, 0.0f, 0.0f));
    outProp = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f));
}

inline glm::mat4 authoredInverseBind(int joint) {
    if (joint == 0) {
        return glm::translate(
            glm::mat4(1.0f), glm::vec3(0.0f, -AuthoredRig::kShoulderHeight, 0.0f));
    }
    return glm::translate(
        glm::mat4(1.0f),
        glm::vec3(
            0.0f, -(AuthoredRig::kShoulderHeight + AuthoredRig::kElbowHeight), 0.0f));
}

// Authored palette for one joint: inverse(meshGlobal) * jointGlobal * inverseBind.
inline glm::mat4 authoredPalette(int joint, float timeSec) {
    glm::mat4 armature;
    glm::mat4 shoulder;
    glm::mat4 elbow;
    glm::mat4 blockMesh;
    glm::mat4 prop;
    authoredGlobals(timeSec, armature, shoulder, elbow, blockMesh, prop);
    (void)armature;
    (void)prop;
    const glm::mat4 jointGlobal = joint == 0 ? shoulder : elbow;
    return glm::inverse(blockMesh) * jointGlobal * authoredInverseBind(joint);
}

// Authored block geometry. The lower two vertices follow the shoulder joint and
// the upper two follow the elbow joint, so the animated pose bends the block.
inline std::vector<glm::vec3> authoredBlockPositions() {
    return {{-0.25f, 0.0f, 0.0f},
            {0.25f, 0.0f, 0.0f},
            {0.25f, 1.0f, 0.0f},
            {-0.25f, 1.0f, 0.0f}};
}

inline std::vector<glm::vec2> authoredBlockUvs() {
    return {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
}

inline std::vector<glm::vec3> authoredBlockNormals() {
    return {{0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f}};
}

inline std::vector<std::uint16_t> authoredBlockIndices() {
    return {0u, 1u, 2u, 0u, 2u, 3u};
}

// The authored base-color map. The encoded bytes are an independent encoder's
// output already used by the texture-import contracts; the literal decoded
// pixels below are the authored expectation, not decoder output.
inline const std::vector<std::uint8_t>& authoredBaseColorPngBytes() {
    return gltf_texture_contract::kPngABytes;
}

inline std::vector<std::uint8_t> authoredBaseColorDecodedPixels() {
    return {255u, 0u,   0u,   255u,
            0u,   255u, 0u,   192u,
            0u,   0u,   255u, 96u,
            255u, 255u, 0u,   0u};
}

inline constexpr float kAuthoredBaseColorFactor[4] = {0.5f, 0.25f, 0.75f, 1.0f};
inline constexpr float kAuthoredEmissiveFactor[3] = {0.2f, 0.1f, 0.05f};
inline constexpr float kAuthoredMetallicFactor = 0.25f;
inline constexpr float kAuthoredRoughnessFactor = 0.5f;

// One authored scene document plus its binary buffer.
struct SceneSource {
    nlohmann::json document;
    std::vector<std::uint8_t> binary;
};

class BinaryWriter {
public:
    std::size_t alignTo4() {
        while (bytes_.size() % 4u != 0u) {
            bytes_.push_back(0u);
        }
        return bytes_.size();
    }

    void addU16(std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
    }

    void addF32(float value) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        for (int shift = 0; shift < 32; shift += 8) {
            bytes_.push_back(static_cast<std::uint8_t>((bits >> shift) & 0xFFu));
        }
    }

    void addVec2(const glm::vec2& value) {
        addF32(value.x);
        addF32(value.y);
    }

    void addVec3(const glm::vec3& value) {
        addF32(value.x);
        addF32(value.y);
        addF32(value.z);
    }

    void addVec4(const glm::vec4& value) {
        addF32(value.x);
        addF32(value.y);
        addF32(value.z);
        addF32(value.w);
    }

    void addU8Vec4(const glm::u8vec4& value) {
        bytes_.push_back(value.x);
        bytes_.push_back(value.y);
        bytes_.push_back(value.z);
        bytes_.push_back(value.w);
    }

    void addMat4Columns(const glm::mat4& value) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                addF32(value[column][row]);
            }
        }
    }

    std::size_t size() const { return bytes_.size(); }
    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

// Builds the bufferViews and accessors the authored document refers to.
struct BufferViewBuilder {
    nlohmann::json bufferViews = nlohmann::json::array();
    nlohmann::json accessors = nlohmann::json::array();
    BinaryWriter binary;

    std::size_t addView(std::size_t byteOffset, std::size_t byteLength) {
        bufferViews.push_back({{"buffer", 0},
                               {"byteOffset", byteOffset},
                               {"byteLength", byteLength}});
        return bufferViews.size() - 1u;
    }

    std::size_t addAccessor(std::size_t view,
                            int componentType,
                            std::size_t count,
                            const char* type) {
        accessors.push_back({{"bufferView", view},
                             {"componentType", componentType},
                             {"count", count},
                             {"type", type}});
        return accessors.size() - 1u;
    }
};

inline SceneSource makeValidSceneSource() {
    SceneSource source;
    BufferViewBuilder builder;

    const std::size_t positionsOffset = builder.binary.alignTo4();
    for (const glm::vec3& value : authoredBlockPositions()) {
        builder.binary.addVec3(value);
    }
    const std::size_t positionsView =
        builder.addView(positionsOffset, builder.binary.size() - positionsOffset);
    const std::size_t positionsAccessor = builder.addAccessor(
        positionsView, 5126, authoredBlockPositions().size(), "VEC3");

    const std::size_t normalsOffset = builder.binary.alignTo4();
    for (const glm::vec3& value : authoredBlockNormals()) {
        builder.binary.addVec3(value);
    }
    const std::size_t normalsView =
        builder.addView(normalsOffset, builder.binary.size() - normalsOffset);
    const std::size_t normalsAccessor = builder.addAccessor(
        normalsView, 5126, authoredBlockNormals().size(), "VEC3");

    const std::size_t uvOffset = builder.binary.alignTo4();
    for (const glm::vec2& value : authoredBlockUvs()) {
        builder.binary.addVec2(value);
    }
    const std::size_t uvView =
        builder.addView(uvOffset, builder.binary.size() - uvOffset);
    const std::size_t uvAccessor =
        builder.addAccessor(uvView, 5126, authoredBlockUvs().size(), "VEC2");

    const std::size_t jointOffset = builder.binary.alignTo4();
    builder.binary.addU8Vec4(glm::u8vec4(0u, 0u, 0u, 0u));
    builder.binary.addU8Vec4(glm::u8vec4(0u, 0u, 0u, 0u));
    builder.binary.addU8Vec4(glm::u8vec4(1u, 0u, 0u, 0u));
    builder.binary.addU8Vec4(glm::u8vec4(1u, 0u, 0u, 0u));
    const std::size_t jointView =
        builder.addView(jointOffset, builder.binary.size() - jointOffset);
    const std::size_t jointAccessor =
        builder.addAccessor(jointView, 5121, 4u, "VEC4");

    const std::size_t weightOffset = builder.binary.alignTo4();
    for (int index = 0; index < 4; ++index) {
        builder.binary.addVec4(glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    }
    const std::size_t weightView =
        builder.addView(weightOffset, builder.binary.size() - weightOffset);
    const std::size_t weightAccessor =
        builder.addAccessor(weightView, 5126, 4u, "VEC4");

    const std::size_t indexOffset = builder.binary.alignTo4();
    for (const std::uint16_t value : authoredBlockIndices()) {
        builder.binary.addU16(value);
    }
    const std::size_t indexView =
        builder.addView(indexOffset, builder.binary.size() - indexOffset);
    const std::size_t indexAccessor = builder.addAccessor(
        indexView, 5123, authoredBlockIndices().size(), "SCALAR");

    const std::size_t inverseBindOffset = builder.binary.alignTo4();
    builder.binary.addMat4Columns(authoredInverseBind(0));
    builder.binary.addMat4Columns(authoredInverseBind(1));
    const std::size_t inverseBindView = builder.addView(
        inverseBindOffset, builder.binary.size() - inverseBindOffset);
    const std::size_t inverseBindAccessor =
        builder.addAccessor(inverseBindView, 5126, 2u, "MAT4");

    const std::size_t timeOffset = builder.binary.alignTo4();
    builder.binary.addF32(0.0f);
    builder.binary.addF32(AuthoredRig::kAnimatedTimeSec);
    const std::size_t timeView =
        builder.addView(timeOffset, builder.binary.size() - timeOffset);
    const std::size_t timeAccessor =
        builder.addAccessor(timeView, 5126, 2u, "SCALAR");

    const std::size_t rotationOffset = builder.binary.alignTo4();
    builder.binary.addVec4(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    const glm::quat rotation = authoredElbowRotation();
    builder.binary.addVec4(
        glm::vec4(rotation.x, rotation.y, rotation.z, rotation.w));
    const std::size_t rotationView =
        builder.addView(rotationOffset, builder.binary.size() - rotationOffset);
    const std::size_t rotationAccessor =
        builder.addAccessor(rotationView, 5126, 2u, "VEC4");

    const std::size_t propOffset = builder.binary.alignTo4();
    for (const glm::vec3& value : authoredBlockPositions()) {
        builder.binary.addVec3(value + glm::vec3(2.0f, 0.0f, 0.0f));
    }
    const std::size_t propView =
        builder.addView(propOffset, builder.binary.size() - propOffset);
    const std::size_t propAccessor = builder.addAccessor(
        propView, 5126, authoredBlockPositions().size(), "VEC3");

    const std::size_t propIndexOffset = builder.binary.alignTo4();
    for (const std::uint16_t value : authoredBlockIndices()) {
        builder.binary.addU16(value);
    }
    const std::size_t propIndexView = builder.addView(
        propIndexOffset, builder.binary.size() - propIndexOffset);
    const std::size_t propIndexAccessor = builder.addAccessor(
        propIndexView, 5123, authoredBlockIndices().size(), "SCALAR");

    // glTF requires POSITION and animation-input accessors to declare bounds.
    builder.accessors[positionsAccessor]["min"] = {-0.25, 0.0, 0.0};
    builder.accessors[positionsAccessor]["max"] = {0.25, 1.0, 0.0};
    builder.accessors[propAccessor]["min"] = {1.75, 0.0, 0.0};
    builder.accessors[propAccessor]["max"] = {2.25, 1.0, 0.0};
    builder.accessors[timeAccessor]["min"] = {0.0};
    builder.accessors[timeAccessor]["max"] = {AuthoredRig::kAnimatedTimeSec};

    source.binary = builder.binary.bytes();

    nlohmann::json document;
    document["asset"] = {{"version", "2.0"},
                         {"generator", "phlosion-scene-contract"}};
    document["buffers"] = nlohmann::json::array({nlohmann::json{
        {"uri", "scene.bin"}, {"byteLength", source.binary.size()}}});
    document["bufferViews"] = builder.bufferViews;
    document["accessors"] = builder.accessors;
    document["images"] = nlohmann::json::array(
        {nlohmann::json{{"name", "base"}, {"uri", "base_color.png"}}});
    document["samplers"] = nlohmann::json::array({nlohmann::json{
        {"magFilter", 9729},
        {"wrapS", 10497},
        {"wrapT", 10497}}});
    document["textures"] =
        nlohmann::json::array({nlohmann::json{{"source", 0}, {"sampler", 0}}});
    document["materials"] = nlohmann::json::array(
        {nlohmann::json{
             {"name", "BlockSkin"},
             {"pbrMetallicRoughness",
              {{"baseColorTexture", {{"index", 0}}},
               {"baseColorFactor",
                {kAuthoredBaseColorFactor[0],
                 kAuthoredBaseColorFactor[1],
                 kAuthoredBaseColorFactor[2],
                 kAuthoredBaseColorFactor[3]}},
               {"metallicFactor", kAuthoredMetallicFactor},
               {"roughnessFactor", kAuthoredRoughnessFactor}}},
             {"emissiveFactor",
              {kAuthoredEmissiveFactor[0],
               kAuthoredEmissiveFactor[1],
               kAuthoredEmissiveFactor[2]}}},
         nlohmann::json{{"name", "PropSkin"}}});
    document["meshes"] = nlohmann::json::array(
        {nlohmann::json{
             {"name", "Block"},
             {"primitives",
              nlohmann::json::array({nlohmann::json{
                  {"attributes",
                   {{"POSITION", positionsAccessor},
                    {"NORMAL", normalsAccessor},
                    {"TEXCOORD_0", uvAccessor},
                    {"JOINTS_0", jointAccessor},
                    {"WEIGHTS_0", weightAccessor}}},
                  {"indices", indexAccessor},
                  {"material", 0}}})}},
         nlohmann::json{
             {"name", "Prop"},
             {"primitives",
              nlohmann::json::array({nlohmann::json{
                  {"attributes", {{"POSITION", propAccessor}}},
                  {"indices", propIndexAccessor},
                  {"material", 1}}})}}});
    document["skins"] = nlohmann::json::array({nlohmann::json{
        {"name", "BlockSkin"},
        {"joints", {AuthoredRig::kShoulderNode, AuthoredRig::kElbowNode}},
        {"inverseBindMatrices", inverseBindAccessor}}});
    document["animations"] = nlohmann::json::array({nlohmann::json{
        {"name", "BendLimb"},
        {"samplers",
         nlohmann::json::array({nlohmann::json{{"input", timeAccessor},
                                               {"interpolation", "LINEAR"},
                                               {"output", rotationAccessor}}})},
        {"channels", nlohmann::json::array({nlohmann::json{
             {"sampler", 0},
             {"target",
              {{"node", AuthoredRig::kElbowNode}, {"path", "rotation"}}}}})}}});
    document["nodes"] = nlohmann::json::array(
        {nlohmann::json{{"name", "Armature"},
                        {"children", {AuthoredRig::kShoulderNode}}},
         nlohmann::json{{"name", "Shoulder"},
                        {"translation", {0.0, AuthoredRig::kShoulderHeight, 0.0}},
                        {"children", {AuthoredRig::kElbowNode}}},
         nlohmann::json{{"name", "Elbow"},
                        {"translation", {0.0, AuthoredRig::kElbowHeight, 0.0}}},
         nlohmann::json{{"name", "Block"},
                        {"mesh", 0},
                        {"skin", 0},
                        {"translation", {AuthoredRig::kMeshNodeOffsetX, 0.0, 0.0}}},
         nlohmann::json{{"name", "Prop"},
                        {"mesh", 1},
                        {"translation", {2.0, 0.0, 0.0}}}});
    document["scenes"] = nlohmann::json::array({nlohmann::json{
        {"name", "Scene"},
        {"nodes",
         {AuthoredRig::kArmatureNode,
          AuthoredRig::kBlockMeshNode,
          AuthoredRig::kPropNode}}}});
    document["scene"] = 0;

    source.document = std::move(document);
    return source;
}

}  // namespace gltf_scene_contract
