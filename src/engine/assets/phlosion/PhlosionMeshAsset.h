#pragma once

#include "engine/core/IAssetStore.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::assets::phlosion {

// Cooked static geometry for the first public `.phmesh` contract (schema v1),
// carried inside the shared PHRC container with magic `PHMS`.
//
// Static v1 is one geometry level of indexed triangles plus material slot
// labels. Positions are model-local and right-handed Y-up; consumers supply
// transforms, culling and slot bindings. Skinning, skeletons, animation, LODs,
// compression and material payloads are separate schemas.
struct MeshVertex {
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> normal{0.0f, 0.0f, 1.0f};
    std::array<float, 4> tangent{1.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 2> texcoord{0.0f, 0.0f};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct MeshBounds {
    std::array<float, 3> min{0.0f, 0.0f, 0.0f};
    std::array<float, 3> max{0.0f, 0.0f, 0.0f};
};

// A contiguous triangle-aligned range of the asset's index list. `materialSlot`
// indexes `MeshAsset::materialSlots`; slots are labels, not asset IDs.
struct MeshSubmesh {
    std::uint32_t firstIndex = 0u;
    std::uint32_t indexCount = 0u;
    std::uint32_t materialSlot = 0u;
};

struct MeshAsset {
    std::string meshId;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshSubmesh> submeshes;
    std::vector<std::string> materialSlots;
    MeshBounds bounds;
};

struct MeshAssetLimits {
    std::size_t maxEncodedBytes = 64u * 1024u * 1024u;
    std::size_t maxVertices = 1'048'576u;
    std::size_t maxIndices = 3'145'728u;
    std::size_t maxSubmeshes = 65'536u;
    std::size_t maxMaterialSlots = 4'096u;
};

// Static mesh v1 constants. `kMeshContainerMagic` is the PHRC document magic,
// `kMeshVertexLayout` the only accepted attribute layout and
// `kMeshCoordinateSystem` the only accepted space.
inline constexpr char kMeshContainerMagic[] = "PHMS";
inline constexpr char kMeshRootType[] = "Mesh";
inline constexpr std::uint32_t kMeshSchemaVersion = 1u;
inline constexpr char kMeshVertexLayout[] = "pntc-f32-v1";
inline constexpr char kMeshCoordinateSystem[] = "right-handed-y-up";
inline constexpr char kMeshTopology[] = "triangles";
inline constexpr char kMeshCooker[] = "phlosion-mesh-v1";
inline constexpr char kMeshTargetProfile[] = "portable";
// position[3], normal[3], tangent[4], texcoord[2], color[4], each float32.
inline constexpr std::size_t kMeshVertexFloatCount = 16u;
inline constexpr std::size_t kMeshVertexBytes = 64u;
// Bounded mesh id and material slot label length in UTF-8 bytes.
inline constexpr std::size_t kMeshMaxNameBytes = 1024u;

// Validates authored data only: bounded identity, real triangles, finite and
// range-checked attributes, exact bounds, index ranges, unique slot labels and
// an ordered submesh partition of the index list. Nothing is repaired or
// normalized. On failure the diagnostic explains the first violation.
bool validateMeshAsset(
    const MeshAsset& asset,
    const MeshAssetLimits& limits = {},
    std::string* outError = nullptr);

// Encodes through the shared PHRC container with explicit little-endian
// component writes. The output is left unchanged on failure and diagnostics are
// cleared on success.
bool encodeMeshAsset(
    const MeshAsset& asset,
    std::vector<std::uint8_t>& outBytes,
    const MeshAssetLimits& limits = {},
    std::string* outError = nullptr);

// Decodes and validates one `PHMS` document. Container integrity, header and
// manifest agreement, chunk types/sizes and the semantic rules above are all
// checked. The output is left unchanged on failure.
bool decodeMeshAsset(
    const std::vector<std::uint8_t>& bytes,
    MeshAsset& outAsset,
    const MeshAssetLimits& limits = {},
    std::string* outError = nullptr);

// Synchronous read plus decode through an existing store. This is a convenience
// for callers that already own a staged document, not an asynchronous session.
bool loadMeshAsset(
    const IAssetStore& store,
    const std::string& virtualPath,
    MeshAsset& outAsset,
    const MeshAssetLimits& limits = {},
    std::string* outError = nullptr);

} // namespace engine::assets::phlosion
