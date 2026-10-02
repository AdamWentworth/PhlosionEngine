#pragma once

#include "engine/render/ModelAnimationTypes.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace engine::models {

struct MeshVertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec4 tangent{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 color{1.0f};
    std::uint16_t j0 = 0u;
    std::uint16_t j1 = 0u;
    std::uint16_t j2 = 0u;
    std::uint16_t j3 = 0u;
    float w0 = 0.0f;
    float w1 = 0.0f;
    float w2 = 0.0f;
    float w3 = 0.0f;
};

struct CachedTextureRgba {
    int width = 0;
    int height = 0;
    int wrapS = 10497;
    int wrapT = 10497;
    int minF = 9729;
    int magF = 9729;
    std::vector<unsigned char> rgba;

    bool hasPixels() const {
        if (width <= 0 || height <= 0) return false;
        const std::uint64_t pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
        const std::uint64_t requiredBytes = pixels * 4ull;
        return requiredBytes > 0ull && requiredBytes <= static_cast<std::uint64_t>(rgba.size());
    }
};

struct MeshVisibilityTrack {
    int nodeIndex = -1;
    float sourceFrameRate = 0.0f;
    std::vector<float> inputs;
    std::vector<std::uint8_t> values;
};

struct MaterialAnimationKey {
    float timeSec = 0.0f;
    float value = 0.0f;
};

struct MaterialAnimationCurve {
    std::vector<MaterialAnimationKey> keys;
};

enum class MaterialAnimationParameter : std::uint8_t {
    UvScaleOffset,
    UvScaleOffset3,
    EmissiveFactor,
};

enum class MaterialAnimationSampling : std::uint8_t {
    Linear,
    HoldSourceFrame,
};

// An always-running source material track, independent of the selected body
// clip. Each component keeps
// its original key times and values instead of being reduced to a guessed
// scroll rate.
struct ContinuousMaterialAnimationTrack {
    std::size_t submeshIndex = 0u;
    MaterialAnimationParameter parameter =
        MaterialAnimationParameter::UvScaleOffset;
    float durationSec = 0.0f;
    float sourceFrameRate = 0.0f;
    bool loop = false;
    MaterialAnimationSampling sampling =
        MaterialAnimationSampling::Linear;
    glm::vec4 defaultValue{1.0f, 1.0f, 0.0f, 0.0f};
    std::array<MaterialAnimationCurve, 4u> components;
};

struct AnimatedBillboard {
    int nodeIndex = -1;
    glm::vec3 offset{0.0f};
    glm::vec3 quadOffset{0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec2 size{0.0f};
    CachedTextureRgba texture;
    // Parallel to body clips; an empty curve means the attachment is hidden.
    std::vector<MaterialAnimationCurve> clipScales;
    std::vector<std::array<MaterialAnimationCurve, 3u>> clipOffsets;
    std::vector<bool> clipLoops;
};

struct MeshData {
    std::string assetCacheIdentity;
    float modelScaleFactor = 1.0f;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint16_t> triangleSubmesh;
    std::vector<glm::vec3> triangleBaseColors;
    std::vector<float> triangleOpacity;
    std::vector<std::uint8_t> triangleDoubleSided;
    std::vector<glm::vec4> submeshBaseColors;
    std::vector<int> submeshMeshIndex;
    std::vector<std::uint32_t> submeshIndexOffset;
    std::vector<std::uint32_t> submeshIndexCount;
    std::vector<CachedTextureRgba> submeshBaseTextures;
    // Optional base surface selected only by a clip-bound emission track.
    std::vector<CachedTextureRgba> submeshEmissionBaseTextures;
    std::vector<CachedTextureRgba> submeshNormalTextures;
    std::vector<CachedTextureRgba> submeshMetallicRoughnessTextures;
    std::vector<CachedTextureRgba> submeshOcclusionTextures;
    std::vector<CachedTextureRgba> submeshEmissiveTextures;
    std::vector<CachedTextureRgba> submeshEnvironmentTextures;
    std::vector<std::uint8_t> submeshAlphaMode;
    std::vector<float> submeshAlphaCutoff;
    std::vector<float> submeshNormalScale;
    std::vector<float> submeshMetallicFactor;
    std::vector<float> submeshRoughnessFactor;
    std::vector<float> submeshOcclusionStrength;
    std::vector<glm::vec3> submeshEmissiveFactors;
    std::vector<std::uint8_t> submeshMaterialModes;
    std::vector<float> submeshMaterialFlags;
    std::vector<glm::vec4> submeshMaterialParams0;
    std::vector<glm::vec4> submeshMaterialParams1;
    std::vector<glm::vec4> submeshMaterialParams2;
    std::vector<glm::vec4> submeshMaterialParams3;
    std::vector<int> meshIndexToNode;
    std::vector<int> triangleNodeIndex;
    std::vector<int> triangleSkinIndex;
    std::vector<glm::vec3> vertexBaseColors;
    std::vector<engine::render::model_types::NodeTRS> nodesDefault;
    std::vector<std::string> nodeNames;
    std::vector<std::vector<int>> nodeChildren;
    std::vector<int> nodeParent;
    std::vector<int> nodeMesh;
    std::vector<int> nodeSkin;
    std::vector<int> sceneRoots;
    std::vector<glm::mat4> bindNodeGlobals;
    std::vector<engine::render::model_types::SkinData> skins;
    std::vector<engine::render::model_types::AnimationClip> animations;
    // Parallel to animations. These source-authored step tracks control
    // renderable mesh nodes without coercing visibility into skeletal TRS.
    std::vector<std::vector<MeshVisibilityTrack>> animationMeshVisibility;
    // Parallel to animations. Unlike continuousMaterialAnimations, these
    // tracks are sampled with the selected skeletal clip and its clip time.
    // These may drive any clip-bound surface parameter.
    std::vector<std::vector<ContinuousMaterialAnimationTrack>>
        animationMaterialParameters;
    std::vector<ContinuousMaterialAnimationTrack>
        continuousMaterialAnimations;
    std::vector<AnimatedBillboard> animatedBillboards;
    bool hasVertexColor = false;
    bool hasVertexBaseColor = false;
};

} // namespace engine::models
