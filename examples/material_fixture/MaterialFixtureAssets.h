#pragma once

#include "MaterialFixtureCase.h"

#include "engine/render/Model.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace engine::material_fixture {

// The five glTF material image roles this fixture binds. The order is the
// binding order used by the sample, the handshake and the guard.
enum class FixtureRole {
    BaseColor = 0,
    Emissive = 1,
    Normal = 2,
    MetallicRoughness = 3,
    Occlusion = 4,
};

inline constexpr int kFixtureRoleCount = 5;

const char* fixtureRoleName(FixtureRole role);
std::optional<FixtureRole> parseFixtureRole(const std::string& name);

// One authored role map. The map is eight by eight RGBA texels; the left four
// columns carry `left` and the right four carry `right`, so each drawn patch
// shows a horizontal spatial variation. The authored pixels are the runnable
// input; the guard derives every expectation from them itself.
struct AuthoredRoleMap {
    FixtureRole role = FixtureRole::BaseColor;
    const char* fileName = "";
    int width = 8;
    int height = 8;
    // The texture format the sample submits for this role.
    std::uint32_t textureSrgb = 1u;
    std::array<std::uint8_t, 4> left{0u, 0u, 0u, 255u};
    std::array<std::uint8_t, 4> right{0u, 0u, 0u, 255u};
};

const std::array<AuthoredRoleMap, kFixtureRoleCount>& authoredRoleMaps();
const AuthoredRoleMap& authoredRoleMap(FixtureRole role);

// The full authored RGBA pixel payload of one role map, left half then right.
std::vector<std::uint8_t> authoredRolePixels(FixtureRole role);

// The authored glTF document for a case. The authored-maps document binds the
// five PNG files through relative URIs; the empty-image-bytes document binds
// five zero-length bufferView images through one external buffer file; the
// absent-maps document carries a valid material with no texture at all.
std::string authoredGltfDocument(FixtureCase fixtureCase);

// Writes the case's glTF document and its input files into `directory`, which
// the caller must already own.
bool stageFixtureAssets(FixtureCase fixtureCase,
                        const std::filesystem::path& directory,
                        std::string* outError = nullptr);

// RAII owner of one run-owned staging directory.
//
// The name is reserved with create_directory, which only reports success for
// the caller that actually created the directory, so concurrent hosts and
// repeated opens cannot share or delete each other's inputs. The destructor
// removes only the directory this instance created; nothing is ever
// glob-deleted. Instances are movable and non-copyable.
class FixtureStagingDirectory {
public:
    FixtureStagingDirectory() = default;
    explicit FixtureStagingDirectory(std::string prefix);
    ~FixtureStagingDirectory();

    FixtureStagingDirectory(const FixtureStagingDirectory&) = delete;
    FixtureStagingDirectory& operator=(const FixtureStagingDirectory&) = delete;
    FixtureStagingDirectory(FixtureStagingDirectory&& other) noexcept;
    FixtureStagingDirectory& operator=(FixtureStagingDirectory&& other) noexcept;

    bool valid() const noexcept { return !path_.empty(); }
    const std::filesystem::path& path() const noexcept { return path_; }
    std::filesystem::path file(const std::string& name) const;
    // Component-aware containment: true only for the owned directory itself or
    // a path inside it.
    bool contains(const std::filesystem::path& candidate) const;
    const std::string& error() const noexcept { return error_; }

private:
    void abandonOwnership() noexcept;

    std::filesystem::path path_;
    std::string error_;
};

// Authored scalar material factors. The authored case uses nontrivial values;
// both fallback cases use neutral references with a zero emission factor.
struct FixtureMaterialFactors {
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    std::array<float, 3> emissiveFactor{0.0f, 0.0f, 0.0f};
    std::uint8_t alphaMode = 0u;
};

FixtureMaterialFactors fixtureMaterialFactors(FixtureCase fixtureCase);

// Where a role's submitted pixels came from. Recorded in the handshake so the
// guard can tell an authored map apart from a role fallback.
const char* fixtureRoleOrigin(FixtureCase fixtureCase, FixtureRole role);

// One decoded role input and the identity the renderer receives. `cpu` owns the
// pixels the WorldTextureData points at, so the caller must keep it alive.
struct FixtureRoleTexture {
    FixtureRole role = FixtureRole::BaseColor;
    std::string roleName;
    ::Model::CPUTexture cpu;
    std::uint32_t textureSrgb = 0u;
    std::string key;
    std::string cacheKey;
    std::uint32_t contentHash = 0u;
};

struct FixtureTextureSet {
    FixtureCase fixtureCase = FixtureCase::AuthoredMaps;
    // Owns the staged glTF and payloads for as long as the decoded inputs are
    // used. Copies share the same directory; the last owner removes it.
    std::shared_ptr<const FixtureStagingDirectory> staging;
    std::vector<FixtureRoleTexture> roles;
    // Scalar factors as resolved from the loaded glTF by the existing
    // production material resolver, not a copy of the authored constants.
    FixtureMaterialFactors factors;

    const FixtureRoleTexture& role(FixtureRole role) const;
    // 1 = normal, 2 = metallic-roughness, 4 = occlusion, 8 = emissive, matching
    // the world material debug flags.
    std::uint32_t presenceFlags() const;
    std::filesystem::path assetDirectory() const;
};

// Stages the case's authored glTF into a run-owned temporary directory, loads
// it through the production loader, decodes all five roles with the five
// existing CPU decode helpers and derives content-dependent cache keys. The
// decoder's diagnostic environment flags are scoped off for the duration and
// restored afterwards, so an ambient PHLOSION_GLTF_* setting cannot dump files
// outside the owned staging directory or leak into the caller.
bool decodeFixtureTextures(FixtureCase fixtureCase,
                           FixtureTextureSet& outTextures,
                           std::string* outError = nullptr);

// 32-bit FNV-1a over the case, role and decoded pixels. The guard re-derives the
// same value from its own independently authored pixels.
std::uint32_t fixtureContentHash(FixtureCase fixtureCase,
                                 FixtureRole role,
                                 const ::Model::CPUTexture& texture);

} // namespace engine::material_fixture
