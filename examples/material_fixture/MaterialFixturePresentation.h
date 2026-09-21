#pragma once

#include "MaterialFixtureAssets.h"

#include "engine/render/IRenderBackend.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace engine::material_fixture {

struct FixtureDrawStats {
    int drawCalls = 0;
    int triangles = 0;
    int debugDraws = 0;
    int debugStateAfterReset = 0;
};

// Normalized x/y/width/height inside the host's render surface with a top-left
// origin. The sample derives these from the same authored layout it draws with,
// so the guard's interior regions describe the real presentation.
using FixturePatchRect = std::array<float, 4>;

std::map<std::string, FixturePatchRect> computeFixturePatchRects(
    int surfaceWidth,
    int surfaceHeight);

std::array<float, 16> fixtureViewProjection(int surfaceWidth, int surfaceHeight);

// Draws the shared material fixture: one background band followed by the eight
// patches, each submitted with its authored debug view and the same decoded
// role textures. The renderer's debug view is reset to the ordinary path before
// this function returns.
bool renderMaterialFixture(const FixtureTextureSet& textures,
                           IRenderBackend& renderer,
                           int surfaceWidth,
                           int surfaceHeight,
                           FixtureDrawStats* outStats = nullptr,
                           std::string* outError = nullptr);

struct FixtureRoleRecord {
    std::string role;
    std::string origin;
    int width = 0;
    int height = 0;
    std::uint32_t textureSrgb = 0u;
    std::uint32_t contentHash = 0u;
    std::string key;
    std::string cacheKey;
    int wrapS = 0;
    int wrapT = 0;
    int minFilter = 0;
    int magFilter = 0;
    std::array<std::uint8_t, 4> firstPixel{0u, 0u, 0u, 0u};
    std::size_t pixelBytes = 0u;
};

struct FixturePatchRecord {
    std::string patch;
    int debugView = 0;
    std::string geometryKey;
    FixturePatchRect rect{0.0f, 0.0f, 1.0f, 1.0f};
};

struct FixtureHandshake {
    std::string schema;
    std::string sampleIdentity;
    std::string host;
    std::string caseName;
    std::string requestedBackend;
    std::string actualBackend;
    std::string gpu;
    bool gpuDiscrete = false;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    int logicalFrame = 0;
    std::string assetDirectory;
    std::size_t assetFileCount = 0u;
    std::uint32_t materialMode = 0u;
    std::uint32_t presenceFlags = 0u;
    std::uint8_t alphaMode = 0u;
    FixtureMaterialFactors factors;
    std::vector<FixtureRoleRecord> roles;
    std::vector<FixturePatchRecord> patches;
    bool debugStateReset = false;
    int drawCalls = 0;
};

FixtureHandshake buildFixtureHandshake(std::string host,
                                       std::string requestedBackend,
                                       const FixtureTextureSet& textures,
                                       const IRenderBackend& renderer,
                                       int surfaceWidth,
                                       int surfaceHeight,
                                       const FixtureDrawStats& stats);

bool writeFixtureHandshake(const std::string& path,
                           const FixtureHandshake& handshake,
                           std::string* outError = nullptr);

const char* fixtureBackendName(const IRenderBackend& renderer) noexcept;

} // namespace engine::material_fixture
