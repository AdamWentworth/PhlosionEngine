#pragma once

#include "engine/core/IAssetStore.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::assets::phlosion {

inline constexpr std::uint32_t kAuthoredSceneSchemaVersion = 5u;
inline constexpr std::uint32_t kMinimumAuthoredSceneSchemaVersion = 1u;
inline constexpr char kAuthoredSceneKind[] =
    "phlosion_authored_scene";

struct AuthoredSceneTransform {
    std::array<float, 3> translation{};
    std::array<float, 3> rotationDegrees{};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
};

struct ImportedSourceBinding {
    std::string targetKind;
    std::string logicalName;
    std::uint32_t recordIndex = 0u;
    AuthoredSceneTransform expectedSourceTransform;
};

struct PrefabInstanceBinding {
    std::string prototypeNodeId;
    std::string prefabAssetId;
    AuthoredSceneTransform creationTransform;
};

// Identifies one immutable cell in the tile set's source environment. The
// owning runtime may reuse that cell's exact source geometry and material
// carriers at a different authored coordinate instead of reducing an
// irregular source feature to a generic flat/ramp primitive.
struct TerrainTileSourceReference {
    std::int32_t gridX = 0;
    std::int32_t gridZ = 0;
};

// A compact, grid-authored terrain cell. Geometry at seams is deliberately
// derived by the owning tile-set runtime so top surfaces, ramps, ledge walls,
// transition strips, collision, and navigation cannot drift apart.
struct TerrainTileBinding {
    std::string tileSetAssetId;
    std::int32_t gridX = 0;
    std::int32_t gridZ = 0;
    std::int32_t elevationLevel = 0;
    std::string surface;
    std::string shape = "flat";
    std::string visualVariant = "auto";
    std::optional<TerrainTileSourceReference> sourceReference;
    bool receivesProjectedShadow = true;
};

struct AuthoredSceneNode {
    std::string id;
    std::string displayName;
    std::string parentId;
    std::uint32_t siblingOrder = 0u;
    bool enabled = true;
    std::string reason;
    std::optional<AuthoredSceneTransform> transform;
    std::optional<ImportedSourceBinding> importedSource;
    std::optional<PrefabInstanceBinding> prefabInstance;
    std::optional<TerrainTileBinding> terrainTile;

    bool folder() const noexcept {
        return !transform && !importedSource && !prefabInstance &&
            !terrainTile;
    }
};

// Project-owned source document composed over an immutable cooked scene.
// This is deliberately distinct from the runtime .phscene archive.
struct AuthoredSceneDocument {
    std::string sceneId;
    std::string baseEnvironmentAssetId;
    std::string coordinateSystem;
    std::vector<AuthoredSceneNode> nodes;
};

bool validateAuthoredSceneDocument(
    const AuthoredSceneDocument& document,
    std::string* outError = nullptr);

bool parseAuthoredSceneDocument(
    const std::string& jsonText,
    AuthoredSceneDocument& out,
    std::string* outError = nullptr);

bool loadAuthoredSceneDocument(
    const IAssetStore& store,
    const std::string& virtualPath,
    AuthoredSceneDocument& out,
    std::string* outError = nullptr);

std::string serializeAuthoredSceneDocument(
    const AuthoredSceneDocument& document);

} // namespace engine::assets::phlosion
