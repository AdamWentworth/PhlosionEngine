#pragma once

#include "engine/core/IAssetStore.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::assets::phlosion {

inline constexpr std::uint32_t kAuthoredSceneSchemaVersion = 1u;
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

    bool folder() const noexcept {
        return !transform && !importedSource && !prefabInstance;
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
