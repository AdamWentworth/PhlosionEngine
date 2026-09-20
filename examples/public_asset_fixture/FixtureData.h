#pragma once

#include "engine/assets/phlosion/PhlosionArchiveSession.h"
#include "engine/core/IAssetStore.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::fixture {

inline constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
inline constexpr std::uint64_t kFnvPrime = 1099511628211ull;

// Synthetic fixture scene schema. One archive embeds a versioned JSON document
// plus one geometry and one texture file per object. Files carry explicit
// content hashes so the consumer can validate decoded content before it
// activates a candidate.
inline constexpr char kFixtureSceneSchema[] =
    "phlosion.public-asset-fixture-scene";
inline constexpr std::uint32_t kFixtureSceneSchemaVersion = 1u;
inline constexpr char kFixtureSceneId[] = "fixture/scene";
inline constexpr char kFixtureRootA[] = "fixture/scene-a";
inline constexpr char kFixtureRootB[] = "fixture/scene-b";
inline constexpr char kFixtureRootMissing[] = "fixture/scene-missing";
inline constexpr char kFixtureDependencyId[] = "fixture/leaf/core";
inline constexpr char kFixtureMissingDependencyId[] =
    "fixture/leaf/missing";

struct FixtureVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    float nx = 0.0f;
    float ny = 0.0f;
    float nz = 1.0f;
};

struct FixtureMaterial {
    std::uint8_t alphaMode = 0u;
    std::uint8_t blendMode = 0u;
    std::uint8_t depthTest = 1u;
    std::uint8_t materialMode = 0u;
    float alphaCutoff = 0.5f;
};

struct FixtureMotion {
    bool enabled = false;
    int fromFrame = 0;
    int toFrame = 0;
    std::array<float, 3> from{};
    std::array<float, 3> to{};
};

struct FixtureObject {
    std::string id;
    std::string role;
    std::vector<FixtureVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint8_t> textureRgba;
    int textureWidth = 0;
    int textureHeight = 0;
    FixtureMaterial material;
    FixtureMotion motion;
    std::string geometryFile;
    std::string textureFile;
    std::string geometryCacheKey;
    std::string textureKey;
    std::string textureCacheKey;

    std::size_t triangleCount() const noexcept {
        return indices.size() / 3u;
    }
};

struct FixtureCamera {
    std::array<float, 3> position{0.0f, 0.0f, 6.0f};
    std::array<float, 3> target{0.0f, 0.0f, 0.0f};
    float fovDegrees = 35.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

struct FixtureContent {
    std::string sceneId;
    std::string contentId;
    std::string contentHash;
    std::string rootAssetId;
    FixtureCamera camera;
    std::vector<FixtureObject> objects;
    std::size_t archiveFileCount = 0u;

    const FixtureObject* findRole(std::string_view role) const noexcept;
    std::size_t triangleCount() const noexcept;
};

enum class FixtureContentVariant {
    A,
    B,
    MissingDependency,
};

struct FixtureBundle {
    FixtureContentVariant variant = FixtureContentVariant::A;
    std::string rootAssetId;
    std::string virtualPath;
    std::string contentId;
    std::vector<std::uint8_t> archiveBytes;
    std::string dependencyAssetId;
    std::string dependencyVirtualPath;
    std::uint64_t dependencyHash = 0u;
    bool dependencyPresent = true;
};

// Immutable in-memory provider shared by the runtime host, the editor plugin
// and the CPU contract tests.
class FixtureAssetStore final : public engine::IAssetStore {
public:
    void add(std::string virtualPath, std::vector<std::uint8_t> bytes);

    std::size_t assetCount() const noexcept {
        return assets_.size();
    }

    bool readText(
        const std::string& virtualPath,
        std::string& outText,
        std::string* outError = nullptr) const override;
    bool readBytes(
        const std::string& virtualPath,
        std::vector<std::uint8_t>& outBytes,
        std::string* outError = nullptr) const override;
    bool exists(const std::string& virtualPath) const override;

private:
    struct Entry {
        std::string virtualPath;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<Entry> assets_;
};

std::uint64_t fnv1a64(
    const void* data,
    std::size_t byteCount,
    std::uint64_t seed = kFnvOffset) noexcept;
std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes) noexcept;
std::uint64_t fnv1a64(
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t seed) noexcept;
std::string hashHex(std::uint64_t hash);

std::vector<std::uint8_t> fixtureDependencyBytes();
std::uint64_t fixtureDependencyHash();

// Builds one deterministic archive through the real PHSC/PHRC encoders. The
// missing-dependency variant carries the same decodable scene document as A
// but declares a required edge whose target is not present in the store.
FixtureBundle buildFixtureBundle(FixtureContentVariant variant);

// Adds the present required leaf to the store. Missing-dependency bundles
// deliberately leave it absent.
void addFixtureDependency(FixtureAssetStore& store);

bool fixtureAssetResolver(
    const std::string& assetId,
    engine::assets::phlosion::AssetLocation& outLocation,
    std::string* outError);

// Validates and decodes one staged snapshot. Nothing may activate before this
// succeeds: geometry ranges, texture dimensions, per-file hashes, object roles
// and the document-level content hash are all checked.
bool validateFixtureSnapshot(
    const engine::assets::phlosion::ArchiveSnapshot& snapshot,
    FixtureContent& outContent,
    std::string* outError);

enum class FixtureCase {
    AFrame0,
    AFrame30,
    FailedReplacement,
    ValidReplacementB,
};

std::optional<FixtureCase> parseFixtureCase(std::string_view name);
std::string_view fixtureCaseName(FixtureCase value);
int fixtureCaseLogicalFrame(FixtureCase value);
bool fixtureCaseReplaces(FixtureCase value);
std::string fixtureCaseReplacementRoot(FixtureCase value);
std::string fixtureCaseExpectedActiveRoot(FixtureCase value);
std::string fixtureCaseExpectedContentId(FixtureCase value);
bool fixtureCaseExpectsReplacementFailure(FixtureCase value);

} // namespace engine::fixture
