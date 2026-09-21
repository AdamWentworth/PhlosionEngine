#pragma once

// Presentation and lifecycle for the glTF scene fixture. Both hosts drive the
// same session type, so the standalone runtime and the real editor plugin
// exercise identical import, replacement and unload behavior.

#include "SceneFixtureAssets.h"
#include "SceneFixtureCase.h"

#include "engine/render/IRenderBackend.h"
#include "engine/render/gltf/GltfSceneRenderer.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace engine::scene_fixture {

struct FixtureLifecycleState;

// Normalized x/y/width/height inside the host's render surface, top-left origin.
using FixtureRegionRect = std::array<float, 4>;

struct FixtureDrawStats {
    int drawCalls = 0;
    int triangles = 0;
    int paletteMatrices = 0;
    int sceneDrawCalls = 0;
    int probeDrawCalls = 0;
    bool sceneDrawn = false;
};

// One host's live fixture state. The session owns the staging directory, the
// active CPU scene and its pose; nothing borrows the renderer past a draw call.
struct FixtureSession {
    FixtureCase fixtureCase = FixtureCase::ABind;
    SceneVariant variant = SceneVariant::A;
    bool emissionOff = false;
    bool bindPose = false;
    std::shared_ptr<const SceneStagingDirectory> staging;
    std::filesystem::path documentPath;
    std::shared_ptr<const engine::render::gltf::scene::GltfSceneAsset> asset;
    engine::render::gltf::scene::GltfScenePose pose;
    // The authored albedo inspection surface, drawn through the same generic
    // adapter with the shared tint-resolved albedo debug view.
    std::shared_ptr<const engine::render::gltf::scene::GltfSceneAsset> probe;
    engine::render::gltf::scene::GltfScenePose probePose;
    bool probeDrawn = false;
    int probeDebugView = 2;
    bool debugStateReset = false;
    bool poseTransitionApplied = false;
    bool poseBindBeforeTransition = false;
    float poseTimeBeforeTransition = 0.0f;
    std::string poseIdentityBeforeTransition;
    std::uint64_t poseGenerationBeforeTransition = 0u;

    bool sceneDrawn = false;
    bool transitionApplied = false;
    int renderFrame = 0;
    int transitionFrame = 0;
    bool replacementAttempted = false;
    bool replacementFailed = false;
    bool replacementSucceeded = false;
    std::string replacementDiagnostic;
    bool unloaded = false;
    std::shared_ptr<FixtureLifecycleState> lifecycle;
    engine::render::WorldResourceScopeId assetScope = 0u;
    engine::render::WorldResourceScopeId probeScope = 0u;
};

// The frame at which each case applies its authored transition. The capture
// frame is later, so the captured frame always shows the case's final state.
int fixtureTransitionFrame();

// Stages the case's authored content, loads it through the production CPU
// loader and samples the authored pose.
bool prepareFixtureSession(FixtureCase fixtureCase,
                           FixtureSession& outSession,
                           std::string* outError = nullptr);

// Applies the case's authored lifecycle transition once the frame reaches
// fixtureTransitionFrame(). Idempotent for later frames.
bool advanceFixtureSession(FixtureSession& session,
                           int frame,
                           std::string* outError = nullptr);

// Draws the authored background and, while the session owns an active scene,
// the scene through the generic engine adapter.
bool drawFixtureSession(IRenderBackend& renderer,
                        FixtureSession& session,
                        int surfaceWidth,
                        int surfaceHeight,
                        FixtureDrawStats* outStats = nullptr,
                        std::string* outError = nullptr);

// Analytic presentation regions derived from the authored content and camera.
// The verification guard recomputes these independently from the same authored
// source instead of learning them from captures.
std::map<std::string, FixtureRegionRect> computeFixtureRegionRects(
    SceneVariant variant,
    float poseBlend,
    int surfaceWidth,
    int surfaceHeight);

struct FixtureHandshake {
    std::string lifecycleEvidence;
    int renderFrame = 0;
    int transitionFrame = 0;
    std::string schema;
    std::string sampleIdentity;
    std::string host;
    std::string requestedBackend;
    std::string actualBackend;
    std::string gpu;
    bool gpuDiscrete = false;
    std::string caseName;
    std::string variantName;
    int logicalFrame = 0;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    bool bindPose = false;
    bool bindPoseBeforeTransition = false;
    bool poseTransitionApplied = false;
    float poseTimeSec = 0.0f;
    float poseTimeBeforeTransition = 0.0f;
    std::string assetIdentityBeforeTransition;
    std::uint64_t assetGenerationBeforeTransition = 0u;
    bool emissionOff = false;
    bool sceneDrawn = false;
    bool unloaded = false;
    bool replacementAttempted = false;
    bool replacementFailed = false;
    bool replacementSucceeded = false;
    std::string replacementDiagnostic;
    std::string assetIdentity;
    std::uint64_t assetGeneration = 0;
    std::string assetSource;
    std::size_t assetTextureCount = 0u;
    std::size_t assetPrimitiveCount = 0u;
    std::uint64_t retainedTextureBytes = 0u;
    double loadSeconds = 0.0;
    double poseSeconds = 0.0;
    bool probePresent = false;
    bool probeDebugStateReset = false;
    int probeDebugView = 2;
    std::string probeIdentity;
    std::uint64_t probeGeneration = 0u;
    std::array<std::uint8_t, 3> probeExpectedLeft{0u, 0u, 0u};
    std::array<std::uint8_t, 3> probeExpectedRight{0u, 0u, 0u};
    std::array<float, 4> probeBaseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    int drawCalls = 0;
    int triangles = 0;
    int paletteMatrices = 0;
    int probeDrawCalls = 0;
    std::map<std::string, FixtureRegionRect> regions;
};

FixtureHandshake buildFixtureHandshake(std::string host,
                                       std::string requestedBackend,
                                       const FixtureSession& session,
                                       const FixtureDrawStats& stats,
                                       int surfaceWidth,
                                       int surfaceHeight,
                                       const IRenderBackend& renderer);

bool writeFixtureHandshake(const std::string& path,
                           const FixtureHandshake& handshake,
                           std::string* outError = nullptr);

const char* fixtureBackendName(const IRenderBackend& renderer) noexcept;

}  // namespace engine::scene_fixture
