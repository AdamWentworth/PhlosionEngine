#pragma once

#include "engine/assets/phlosion/PhlosionResourceContainer.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::assets::phlosion {

// The source-neutral PHLO prefab graph produced by the current Forge cooker.
// Payload decoding and source-specific material interpretation are separate
// steps; this type owns the four validated resource documents and their counts.
struct CookedModelClip {
    std::string name;
    float durationSeconds = 0.0f;
};

struct CookedModelPackage {
    std::string objectId;
    std::string prefabKind;
    std::uint32_t vertexCount = 0u;
    std::uint32_t indexCount = 0u;
    std::uint32_t submeshCount = 0u;
    std::uint32_t nodeCount = 0u;
    std::uint32_t skinCount = 0u;
    std::uint32_t animationCount = 0u;
    std::vector<CookedModelClip> clips;
    phrc::Document root;
    phrc::Document mesh;
    phrc::Document skeleton;
    phrc::Document animations;
    phrc::Document materials;
};

// Validates the PHRC containers, model schema, child paths and dependency
// hashes. Leaves outPackage unchanged on failure.
bool loadCookedModelPackage(
    const std::filesystem::path& phloPath,
    CookedModelPackage& outPackage,
    std::string* outError = nullptr);

} // namespace engine::assets::phlosion
