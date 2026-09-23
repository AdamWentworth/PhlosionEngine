#pragma once

// CPU-owned glTF scene snapshot.
//
// This is the generic, graphics-context-free import boundary for consumers that
// cannot construct Model (whose constructor uploads OpenGL resources). A
// published asset owns decoded geometry, indices, node/skin/clip arrays,
// materials and pixels; it retains no live fastgltf view and no file
// dependency. It reuses the existing fastgltf parse, scene-data build, pose
// sampler and texture decoders rather than adding a second representation.
//
// The import is a bounded subset, not general glTF support. Unsupported
// topologies, morph targets, extra joint sets, non-default UV sets or texture
// transforms, blend/mask/double-sided materials and unsupported material
// extensions are rejected with an explicit diagnostic.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/render/ModelAnimationTypes.h"
#include "engine/render/AnimationBlend.h"
#include "engine/render/RenderBackendTypes.h"

namespace engine { class IAssetStore; }

namespace engine::render::gltf::scene {

using engine::render::backend::WorldMeshVertex;

// Explicit bind-pose clip index for sampleGltfScenePose.
inline constexpr int kBindPoseClip = -1;

// Bounded subset limit: four weights per vertex and at most 64 joints per skin.
inline constexpr std::size_t kMaxSkinJoints = 64u;

struct GltfSceneLoadOptions {
    std::size_t maxNodes = 8192u;
    std::size_t maxPrimitives = 16384u;
    std::size_t maxVertices = 1u << 21;
    std::size_t maxIndices = 1u << 22;
    // Retained decoded payload only. Parser and decoder temporaries and
    // filesystem reads are outside this accounting.
    std::size_t maxTextureBytes = 128u << 20;
};

// One owned decoded image role. `authored` distinguishes a map the document
// actually bound from the implicit white this path supplies for an absent
// emissive map.
struct GltfSceneTexture {
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
    int wrapS = 10497;
    int wrapT = 10497;
    std::uint8_t textureSrgb = 1u;
    bool authored = false;
    std::string key;
    std::string cacheKey;
    std::uint64_t contentHash = 0u;

    bool empty() const noexcept {
        return width <= 0 || height <= 0 ||
               rgba.size() != static_cast<std::size_t>(width) *
                                  static_cast<std::size_t>(height) * 4u;
    }
};

struct GltfSceneMaterial {
    std::string name;
    GltfSceneTexture baseColor;
    GltfSceneTexture normal;
    GltfSceneTexture metallicRoughness;
    GltfSceneTexture occlusion;
    GltfSceneTexture emissive;
    std::array<float, 4> baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 3> emissiveFactor{0.0f, 0.0f, 0.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    bool hasBaseColorMap = false;
    bool hasNormalMap = false;
    bool hasMetallicRoughnessMap = false;
    bool hasOcclusionMap = false;
    bool hasEmissiveMap = false;
    std::string bindKey;
};

struct GltfScenePrimitive {
    int nodeIndex = -1;
    int meshIndex = -1;
    int primitiveIndex = -1;
    int materialIndex = -1;
    int skinIndex = -1;
    std::vector<WorldMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    // Content-dependent cache identity for the renderer's
    // cached indexed geometry.
    std::string geometryKey;
    std::size_t triangleCount = 0u;
};

struct GltfSceneNode {
    std::string name;
    int parent = -1;
    int meshIndex = -1;
    int skinIndex = -1;
    model_types::NodeTRS local{};
    glm::mat4 defaultGlobal{1.0f};
    std::vector<int> children;
};

struct GltfSceneStats {
    std::size_t nodeCount = 0u;
    std::size_t primitiveCount = 0u;
    std::size_t vertexCount = 0u;
    std::size_t indexCount = 0u;
    std::size_t materialCount = 0u;
    std::size_t textureCount = 0u;
    std::size_t skinCount = 0u;
    std::size_t clipCount = 0u;
    std::size_t sourceFileBytes = 0u;
    std::uint64_t retainedGeometryBytes = 0u;
    std::uint64_t retainedTextureBytes = 0u;
    std::uint64_t retainedAssetBytes = 0u;
    double loadSeconds = 0.0;
};

struct GltfSceneAsset {
    std::string sourcePath;
    // Hex digest of the main document bytes. A reload creates a new generation;
    // content-dependent geometry/texture keys also cover external payloads.
    // Failed reloads leave the published snapshot untouched.
    std::string sourceIdentity;
    std::uint64_t generation = 0u;
    std::vector<GltfSceneNode> nodes;
    std::vector<int> sceneRoots;
    std::vector<GltfScenePrimitive> primitives;
    std::vector<GltfSceneMaterial> materials;
    std::vector<model_types::SkinData> skins;
    std::vector<model_types::AnimationClip> animations;
    // Shared sampler layout (index-compatible with nodes).
    std::vector<model_types::NodeTRS> nodesDefault;
    std::vector<std::string> nodeNames;
    std::vector<std::vector<int>> nodeChildren;
    GltfSceneStats stats;
};

// Result of one pose sample. Owns its arrays; the asset must outlive the pose.
struct GltfScenePose {
    std::string assetIdentity;
    std::uint64_t assetGeneration = 0u;
    int clipIndex = kBindPoseClip;
    float timeSec = 0.0f;
    std::vector<model_types::NodeTRS> local;
    std::vector<glm::mat4> global;
    // One entry per asset primitive; empty for static (unskinned) primitives.
    // Each palette is inverse(meshGlobal) * jointGlobal * inverseBind, so the
    // renderer's model matrix (the mesh node's global transform) restores the
    // authored jointGlobal * inverseBind result.
    std::vector<std::vector<glm::mat4>> palettes;
    double sampleSeconds = 0.0;
};

// Synchronous, CPU-only load. On success `out` is replaced with a fully owned
// snapshot. On failure `out` is left exactly as the caller passed it and
// `error` describes the rejection.
bool loadGltfSceneAsset(const std::string& path,
                        std::shared_ptr<const GltfSceneAsset>& out,
                        std::string* error = nullptr,
                        const GltfSceneLoadOptions& options = {});

// Store-only import: all document/buffer/image reads use this store. Relative
// resource paths may traverse within its virtual root, never outside it.
// Absolute paths, URI schemes, encoded path characters and query/fragment
// components are outside this bounded subset. No filesystem fallback occurs.
bool loadGltfSceneAsset(const engine::IAssetStore& store,
                        const std::string& virtualPath,
                        std::shared_ptr<const GltfSceneAsset>& out,
                        std::string* error = nullptr,
                        const GltfSceneLoadOptions& options = {});

// Samples a clip (or kBindPoseClip for the bind pose) and forms every skinned
// mesh-node palette. A pose records the asset identity and generation, so a
// pose from an older snapshot is rejected by drawGltfSceneAsset. On failure the
// caller's previous pose is unchanged.
bool sampleGltfScenePose(const std::shared_ptr<const GltfSceneAsset>& asset,
                         float timeSec,
                         int clipIndex,
                         GltfScenePose& out,
                         std::string* error = nullptr);

// Blend sampled local poses, then rebuild the same mesh-relative skin palettes
// consumed by drawGltfSceneAsset. A failed blend leaves `out` unchanged.
bool sampleGltfSceneBlendedPose(
    const std::shared_ptr<const GltfSceneAsset>& asset,
    const engine::render::animation::PoseBlendRequest& request,
    GltfScenePose& out,
    std::string* error = nullptr);

// True when the pose belongs to the given asset generation.
bool gltfScenePoseMatchesAsset(const GltfSceneAsset& asset,
                               const GltfScenePose& pose) noexcept;

// Deterministic 64-bit content hash used for cache identities and diagnostics.
std::uint64_t gltfSceneContentHash(const void* data, std::size_t bytes) noexcept;

}  // namespace engine::render::gltf::scene
