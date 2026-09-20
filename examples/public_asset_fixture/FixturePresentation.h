#pragma once

#include "FixtureSession.h"
#include "engine/render/IRenderBackend.h"

#include <array>
#include <map>
#include <string>

namespace engine::fixture {

struct FixtureDrawStats {
    int drawCalls = 0;
    int blendDraws = 0;
    int triangles = 0;
};

// Normalized x/y/width/height inside the host's render surface, top-left
// origin. The fixture derives these from the same camera and motion used for
// drawing so the image guards validate real presentation instead of a copy of
// hardcoded screen coordinates.
using FixtureRoleRect = std::array<float, 4>;

struct FixtureHandshake {
    std::string host;
    std::string caseName;
    std::string requestedBackend;
    std::string actualBackend;
    std::string gpu;
    bool gpuDiscrete = false;
    int logicalFrame = 0;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    std::string activeRoot;
    std::string activeContentId;
    std::string activeContentHash;
    std::size_t archiveFileCount = 0u;
    int activationCount = 0;
    std::size_t triangleCount = 0u;
    bool preparationComplete = false;
    FixtureTransition transition;
    std::map<std::string, FixtureRoleRect> roleRects;
};

std::array<float, 16> fixtureViewProjection(
    const FixtureCamera& camera,
    int surfaceWidth,
    int surfaceHeight);

std::array<float, 16> fixtureModelMatrix(
    const FixtureObject& object,
    int logicalFrame);

std::map<std::string, FixtureRoleRect> computeFixtureRoleRects(
    const FixtureContent& content,
    int logicalFrame,
    int surfaceWidth,
    int surfaceHeight);

bool renderFixture(
    const FixtureContent& content,
    IRenderBackend& renderer,
    int logicalFrame,
    int surfaceWidth,
    int surfaceHeight,
    FixtureDrawStats* outStats = nullptr,
    std::string* outError = nullptr);

FixtureHandshake buildFixtureHandshake(
    std::string host,
    std::string requestedBackend,
    const IRenderBackend& renderer,
    const FixtureSession& session,
    int logicalFrame,
    int surfaceWidth,
    int surfaceHeight);

bool writeFixtureHandshake(
    const std::string& path,
    const FixtureHandshake& handshake,
    std::string* outError = nullptr);

const char* fixtureBackendName(const IRenderBackend& renderer) noexcept;

} // namespace engine::fixture
