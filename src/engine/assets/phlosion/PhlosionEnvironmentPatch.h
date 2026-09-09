#pragma once

#include "engine/core/IAssetStore.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace engine::assets::phlosion {

inline constexpr std::uint32_t kEnvironmentPatchSchemaVersion = 1u;
inline constexpr char kEnvironmentPatchKind[] =
    "phlosion_environment_patch";
inline constexpr std::array<std::uint8_t, 8> kEnvironmentPatchBinaryMagic{
    'P', 'H', 'L', 'O', 'E', 'P', 'A', 'T'};

struct EnvironmentPatchSourceLock {
    std::string profileId;
    std::string modelSha256;
    std::string geometrySha256;
    std::string coordinateSystem;
};

struct EnvironmentPatchVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 4> tangent{};
    std::array<float, 4> bitangent{};
    std::array<std::array<float, 2>, 4> texcoords{};
    std::array<std::array<float, 4>, 4> colors{};
    float normalW = 1.0f;
    std::array<std::int32_t, 4> joints{};
    std::array<float, 4> weights{};
    std::int32_t sourceVertexIndex = -1;
};

struct EnvironmentPatchMaterialGroup {
    std::uint32_t materialIndex = 0u;
    std::vector<std::uint32_t> indices;
};

struct EnvironmentPatchMesh {
    std::string id;
    std::string displayName;
    std::vector<EnvironmentPatchVertex> vertices;
    std::vector<EnvironmentPatchMaterialGroup> materialGroups;
};

struct EnvironmentPatchTerrainReplacement {
    float tileSizeCm = 100.0f;
    std::vector<std::array<std::int32_t, 2>> cells;
};

struct EnvironmentPatchDocument {
    EnvironmentPatchSourceLock source;
    std::vector<EnvironmentPatchMesh> meshes;
    std::optional<EnvironmentPatchTerrainReplacement> terrainReplacement;
};

bool validateEnvironmentPatchDocument(
    const EnvironmentPatchDocument& document,
    std::string* outError = nullptr);

bool parseEnvironmentPatchDocument(
    const std::string& jsonText,
    EnvironmentPatchDocument& out,
    std::string* outError = nullptr);

bool parseEnvironmentPatchBinary(
    std::span<const std::uint8_t> bytes,
    EnvironmentPatchDocument& out,
    std::string* outError = nullptr);

bool loadEnvironmentPatchDocument(
    const IAssetStore& store,
    const std::string& virtualPath,
    EnvironmentPatchDocument& out,
    std::string* outError = nullptr);

std::string serializeEnvironmentPatchDocument(
    const EnvironmentPatchDocument& document);

std::vector<std::uint8_t> serializeEnvironmentPatchBinary(
    const EnvironmentPatchDocument& document);

} // namespace engine::assets::phlosion
