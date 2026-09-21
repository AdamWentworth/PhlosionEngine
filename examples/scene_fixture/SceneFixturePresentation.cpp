#include "SceneFixturePresentation.h"

#include "SceneFixtureCase.h"
#include "SceneFixtureLifecycle.h"

#include "engine/core/Environment.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace engine::scene_fixture {
namespace {

using engine::render::backend::WorldMeshVertex;
using engine::render::backend::WorldTextureData;

constexpr int kTransitionFrame = 3;
constexpr uint8_t kSceneMaterialMode = 2u;

// Process-local scoped override for the shared decoder's diagnostic flags. The
// decoders can write debug PNGs relative to the process working directory and
// add logging when these are set, so the sample keeps them off for the duration
// of a load and restores whatever the caller had on every exit path. Only this
// process's environment is touched, and only for the scope below.
class ScopedDecoderDiagnostics {
public:
    ScopedDecoderDiagnostics() {
        for (std::size_t index = 0u; index < kNames.size(); ++index) {
            const std::optional<std::string> previous =
                engine::env::get(kNames[index]);
            previous_[index] = previous;
            applied_[index] = set(kNames[index], index == 1u ? nullptr : "0");
        }
    }

    ScopedDecoderDiagnostics(const ScopedDecoderDiagnostics&) = delete;
    ScopedDecoderDiagnostics& operator=(const ScopedDecoderDiagnostics&) = delete;

    ~ScopedDecoderDiagnostics() {
        for (std::size_t index = 0u; index < kNames.size(); ++index) {
            if (!applied_[index]) {
                continue;
            }
            if (previous_[index].has_value()) {
                set(kNames[index], previous_[index]->c_str());
            } else {
                set(kNames[index], nullptr);
            }
        }
    }

private:
    static bool set(const char* name, const char* value) {
#if defined(_MSC_VER)
        return _putenv_s(name, value != nullptr ? value : "") == 0;
#else
        if (value == nullptr) {
            return ::unsetenv(name) == 0;
        }
        return ::setenv(name, value, 1) == 0;
#endif
    }

    static constexpr std::array<const char*, 3> kNames{
        "PHLOSION_GLTF_DEBUG_ALL",
        "PHLOSION_GLTF_DEBUG_MATCH",
        "PHLOSION_GLTF_DUMP_TEXTURES"};

    std::array<std::optional<std::string>, kNames.size()> previous_{};
    std::array<bool, kNames.size()> applied_{};
};

bool fail(std::string* outError, std::string message) {
    if (outError != nullptr) {
        *outError = std::move(message);
    }
    return false;
}

// A single opaque white texel owned by this translation unit. It is only ever
// borrowed for the duration of a draw call.
const unsigned char* backgroundWhitePixel() {
    static const unsigned char kPixel[4] = {255u, 255u, 255u, 255u};
    return kPixel;
}

void drawBackground(IRenderBackend& renderer, int surfaceWidth, int surfaceHeight) {
    const std::array<float, 3> color = fixtureBackgroundLinearColor();
    const std::array<glm::vec3, 4> corners{{
        {-40.0f, -24.0f, -6.0f},
        {40.0f, -24.0f, -6.0f},
        {40.0f, 24.0f, -6.0f},
        {-40.0f, 24.0f, -6.0f},
    }};
    const std::array<glm::vec2, 4> uvs{{
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
    std::vector<WorldMeshVertex> vertices;
    vertices.reserve(4u);
    for (std::size_t index = 0u; index < corners.size(); ++index) {
        WorldMeshVertex vertex;
        vertex.x = corners[index].x;
        vertex.y = corners[index].y;
        vertex.z = corners[index].z;
        vertex.u = uvs[index].x;
        vertex.v = uvs[index].y;
        vertex.r = color[0];
        vertex.g = color[1];
        vertex.b = color[2];
        vertex.a = 1.0f;
        vertex.nx = 0.0f;
        vertex.ny = 0.0f;
        vertex.nz = 1.0f;
        vertex.tx = 1.0f;
        vertex.ty = 0.0f;
        vertex.tz = 0.0f;
        vertex.tw = 1.0f;
        vertices.push_back(vertex);
    }
    static const std::uint32_t kQuadIndices[6] = {0u, 1u, 2u, 0u, 2u, 3u};
    const glm::mat4 viewProjection =
        fixtureViewProjection(surfaceWidth, surfaceHeight);
    WorldTextureData texture;
    texture.key = "scene-fixture/background";
    texture.cacheKey = texture.key;
    texture.rgba = backgroundWhitePixel();
    texture.width = 1;
    texture.height = 1;
    texture.textureSrgb = 1u;
    texture.materialMode = 0u;
    texture.alphaMode = 0u;
    // The authored background is submitted first and must always cover the
    // viewport, including on frames where the scene content is smaller than the
    // previous frame's (for example after unload).
    texture.depthTestEnabled = 0u;
    renderer.drawWorldIndexedMeshTexturedCached(
        "scene-fixture/background-quad",
        vertices.data(),
        vertices.size(),
        kQuadIndices,
        6u,
        &texture,
        glm::value_ptr(viewProjection),
        surfaceWidth,
        surfaceHeight);
}

FixtureRegionRect makeRegion(glm::vec2 minimum,
                             glm::vec2 maximum,
                             int surfaceWidth,
                             int surfaceHeight) {
    const float marginX = 0.02f * static_cast<float>(surfaceWidth);
    const float marginY = 0.02f * static_cast<float>(surfaceHeight);
    const float x0 = std::clamp(minimum.x - marginX, 0.0f,
                                static_cast<float>(surfaceWidth));
    const float y0 = std::clamp(minimum.y - marginY, 0.0f,
                                static_cast<float>(surfaceHeight));
    const float x1 = std::clamp(maximum.x + marginX, 0.0f,
                                static_cast<float>(surfaceWidth));
    const float y1 = std::clamp(maximum.y + marginY, 0.0f,
                                static_cast<float>(surfaceHeight));
    if (x1 <= x0 || y1 <= y0) {
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }
    return {x0 / static_cast<float>(surfaceWidth),
            y0 / static_cast<float>(surfaceHeight),
            (x1 - x0) / static_cast<float>(surfaceWidth),
            (y1 - y0) / static_cast<float>(surfaceHeight)};
}

std::pair<glm::vec2, glm::vec2> projectBounds(
    const std::vector<glm::vec3>& points,
    const glm::mat4& viewProjection,
    int surfaceWidth,
    int surfaceHeight) {
    glm::vec2 minimum(std::numeric_limits<float>::max());
    glm::vec2 maximum(-std::numeric_limits<float>::max());
    for (const glm::vec3& point : points) {
        const glm::vec2 projected = fixtureProjectToPixels(
            viewProjection, point, surfaceWidth, surfaceHeight);
        minimum = glm::min(minimum, projected);
        maximum = glm::max(maximum, projected);
    }
    return {minimum, maximum};
}

}  // namespace

int fixtureTransitionFrame() {
    return kTransitionFrame;
}

bool prepareFixtureSession(FixtureCase fixtureCase,
                           FixtureSession& outSession,
                           std::string* outError) {
    FixtureSession session;
    session.fixtureCase = fixtureCase;
    session.variant = SceneVariant::A;
    session.emissionOff = fixtureCaseInfo(fixtureCase).emissionOff;
    session.bindPose = fixtureCaseInfo(fixtureCase).bindPose;

    auto staging = std::make_shared<SceneStagingDirectory>(
        std::string("phlosion-scene-fixture-") + fixtureCaseName(fixtureCase));
    if (!staging->valid()) {
        return fail(outError, "could not reserve a staging directory: " +
                                  staging->error());
    }
    if (!stageSceneDocument(session.variant, session.emissionOff, staging->path(),
                            outError)) {
        return false;
    }
    if (!stageProbeDocument(staging->path(), outError)) {
        return false;
    }
    const std::filesystem::path document =
        staging->file(sceneDocumentFileName());
    if (document.empty()) {
        return fail(outError, "the staging directory has no fixture document");
    }

    std::shared_ptr<const engine::render::gltf::scene::GltfSceneAsset> asset;
    std::string loadError;
    bool loaded = false;
    {
        const ScopedDecoderDiagnostics diagnostics;
        if (engine::env::get("PHLOSION_SCENE_FIXTURE_LIFECYCLE").value_or("0") == "1") {
            loaded = prepareFixtureLifecycle(session, staging->path().string(), &loadError);
            asset = session.asset;
        } else {
            loaded = engine::render::gltf::scene::loadGltfSceneAsset(
                document.string(), asset, &loadError);
        }
    }
    if (!loaded) {
        return fail(outError, "the fixture scene did not load: " + loadError);
    }
    const int clip = session.bindPose
                         ? engine::render::gltf::scene::kBindPoseClip
                         : 0;
    const float time =
        session.bindPose ? 0.0f : fixturePoseTimeSec();
    if (!engine::render::gltf::scene::sampleGltfScenePose(
            asset, time, clip, session.pose, &loadError)) {
        return fail(outError, "the fixture pose did not sample: " + loadError);
    }

    // The authored albedo inspection surface is an independent small document
    // presented through the same generic adapter.
    std::shared_ptr<const engine::render::gltf::scene::GltfSceneAsset> probe;
    bool probeLoaded = false;
    {
        const ScopedDecoderDiagnostics diagnostics;
        probeLoaded = engine::render::gltf::scene::loadGltfSceneAsset(
            (staging->file("probe_fixture.gltf")).string(), probe, &loadError);
    }
    if (!probeLoaded) {
        return fail(outError,
                    "the albedo inspection surface did not load: " + loadError);
    }
    if (!engine::render::gltf::scene::sampleGltfScenePose(
            probe, 0.0f, engine::render::gltf::scene::kBindPoseClip,
            session.probePose, &loadError)) {
        return fail(outError, "the inspection surface pose did not sample: " +
                                  loadError);
    }

    session.staging = std::move(staging);
    session.documentPath = document;
    session.asset = std::move(asset);
    session.probe = std::move(probe);
    session.poseIdentityBeforeTransition = session.asset->sourceIdentity;
    session.poseGenerationBeforeTransition = session.asset->generation;
    session.poseBindBeforeTransition = session.bindPose;
    session.poseTimeBeforeTransition = session.pose.timeSec;
    // `a-pose` starts in the bind pose and is animated by the same asset at the
    // authored transition frame; the other cases present a single state.
    if (fixtureCase == FixtureCase::APose) {
        if (!engine::render::gltf::scene::sampleGltfScenePose(
                session.asset, 0.0f,
                engine::render::gltf::scene::kBindPoseClip, session.pose,
                &loadError)) {
            return fail(outError, "the bind start pose did not sample: " +
                                      loadError);
        }
        // The presented start state is the explicit bind pose of the same
        // asset, so the transition records a real before/after pair.
        session.poseBindBeforeTransition = true;
        session.poseTimeBeforeTransition = session.pose.timeSec;
    }
    outSession = std::move(session);
    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

bool advanceFixtureSession(FixtureSession& session,
                           int frame,
                           std::string* outError) {
    if (session.lifecycle) {
        const ScopedDecoderDiagnostics diagnostics;
        return advanceFixtureLifecycle(session, frame, outError);
    }
    session.renderFrame = frame;
    if (frame < kTransitionFrame || session.transitionApplied) {
        return true;
    }
    session.transitionApplied = true;
    session.transitionFrame = frame;
    switch (session.fixtureCase) {
        case FixtureCase::APose: {
            // Same asset, bind pose to animated pose in process.
            session.poseBindBeforeTransition = true;
            std::string poseError;
            if (!engine::render::gltf::scene::sampleGltfScenePose(
                    session.asset, fixturePoseTimeSec(), 0, session.pose,
                    &poseError)) {
                return fail(outError,
                            "the pose transition did not sample: " + poseError);
            }
            session.poseTransitionApplied = true;
            return true;
        }
        case FixtureCase::FailedReplacement: {
            session.replacementAttempted = true;
            std::string stageError;
            if (!stageMalformedDocument(session.staging->path(), &stageError)) {
                return fail(outError,
                            "could not stage the malformed replacement: " +
                                stageError);
            }
            std::shared_ptr<const engine::render::gltf::scene::GltfSceneAsset>
                previous = session.asset;
            std::string loadError;
            bool loaded = false;
            {
                const ScopedDecoderDiagnostics diagnostics;
                loaded = engine::render::gltf::scene::loadGltfSceneAsset(
                    session.documentPath.string(), previous, &loadError);
            }
            session.replacementFailed = !loaded && !loadError.empty();
            session.replacementDiagnostic = loadError;
            if (loaded) {
                return fail(outError,
                            "the malformed replacement must not load");
            }
            if (previous.get() != session.asset.get()) {
                return fail(outError,
                            "a failed replacement must preserve the previous "
                            "asset");
            }
            return true;
        }
        case FixtureCase::ReplacementB: {
            session.replacementAttempted = true;
            std::string stageError;
            if (!stageSceneDocument(SceneVariant::B, false,
                                    session.staging->path(), &stageError)) {
                return fail(outError,
                            "could not stage the replacement content: " +
                                stageError);
            }
            std::shared_ptr<const engine::render::gltf::scene::GltfSceneAsset>
                replacement;
            std::string loadError;
            bool loaded = false;
            {
                const ScopedDecoderDiagnostics diagnostics;
                loaded = engine::render::gltf::scene::loadGltfSceneAsset(
                    session.documentPath.string(), replacement, &loadError);
            }
            if (!loaded) {
                return fail(outError,
                            "the replacement scene did not load: " + loadError);
            }
            if (!engine::render::gltf::scene::sampleGltfScenePose(
                    replacement, fixturePoseTimeSec(), 0, session.pose,
                    &loadError)) {
                return fail(outError,
                            "the replacement pose did not sample: " + loadError);
            }
            session.asset = std::move(replacement);
            session.variant = SceneVariant::B;
            session.replacementSucceeded = true;
            return true;
        }
        case FixtureCase::Unloaded: {
            // Release the active CPU scene; the remaining frames draw only the
            // authored background.
            session.asset.reset();
            session.pose = engine::render::gltf::scene::GltfScenePose{};
            session.probe.reset();
            session.probePose = engine::render::gltf::scene::GltfScenePose{};
            session.probeDrawn = false;
            session.debugStateReset = false;
            session.unloaded = true;
            return true;
        }
        default:
            return true;
    }
}

bool drawFixtureSession(IRenderBackend& renderer,
                        FixtureSession& session,
                        int surfaceWidth,
                        int surfaceHeight,
                        FixtureDrawStats* outStats,
                        std::string* outError) {
    if (session.lifecycle && !beginFixtureLifecycleDraw(renderer, session, outError)) return false;
    const int width = std::max(1, surfaceWidth);
    const int height = std::max(1, surfaceHeight);
    FixtureDrawStats stats;
    drawBackground(renderer, width, height);
    ++stats.drawCalls;
    stats.triangles += 2;

    if (session.asset) {
        engine::render::gltf::scene::GltfSceneDrawOptions options;
        const glm::mat4 viewProjection = fixtureViewProjection(width, height);
        std::copy(glm::value_ptr(viewProjection),
                  glm::value_ptr(viewProjection) + 16,
                  options.viewProjectionMatrix.begin());
        options.surfaceWidth = width;
        options.surfaceHeight = height;
        options.cameraPosition = fixtureCameraPosition();
        options.cameraTarget = fixtureCameraTarget();
        const std::array<float, 3> position = fixtureCameraPosition();
        const std::array<float, 3> target = fixtureCameraTarget();
        const glm::vec3 forward = glm::normalize(glm::vec3(
            target[0] - position[0], target[1] - position[1],
            target[2] - position[2]));
        options.cameraForward = {forward.x, forward.y, forward.z};
        options.materialMode = kSceneMaterialMode;
        options.resourceScope = session.assetScope;

        engine::render::gltf::scene::GltfSceneDrawStats sceneStats;
        if (!engine::render::gltf::scene::drawGltfSceneAsset(
                renderer, *session.asset, session.pose, options, sceneStats,
                outError)) {
            return false;
        }
        stats.drawCalls += static_cast<int>(sceneStats.drawCalls);
        stats.triangles += static_cast<int>(sceneStats.triangles);
        stats.paletteMatrices += static_cast<int>(sceneStats.paletteMatrices);
        stats.sceneDrawCalls += static_cast<int>(sceneStats.drawCalls);
        stats.sceneDrawn = true;
        session.sceneDrawn = true;

        // Independent albedo inspection: the same generic adapter submits the
        // authored inspection surface with the shared tint-resolved albedo
        // debug view, which is reset before this call returns.
        if (session.probe) {
            renderer.setWorldMaterialDebugView(session.probeDebugView);
            engine::render::gltf::scene::GltfSceneDrawOptions probeOptions =
                options;
            probeOptions.resourceScope = session.probeScope;
            engine::render::gltf::scene::GltfSceneDrawStats probeStats;
            if (!engine::render::gltf::scene::drawGltfSceneAsset(
                    renderer, *session.probe, session.probePose, probeOptions,
                    probeStats, outError)) {
                renderer.setWorldMaterialDebugView(0);
                return false;
            }
            renderer.setWorldMaterialDebugView(0);
            stats.drawCalls += static_cast<int>(probeStats.drawCalls);
            stats.triangles += static_cast<int>(probeStats.triangles);
            stats.probeDrawCalls += static_cast<int>(probeStats.drawCalls);
            session.probeDrawn = true;
            session.debugStateReset = renderer.worldMaterialDebugView() == 0;
        }
    }

    if (session.lifecycle && !finishFixtureLifecycleDraw(renderer, session, outError)) return false;
    if (outStats != nullptr) {
        *outStats = stats;
    }
    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

std::map<std::string, FixtureRegionRect> computeFixtureRegionRects(
    SceneVariant variant,
    float poseBlend,
    int surfaceWidth,
    int surfaceHeight) {
    std::map<std::string, FixtureRegionRect> regions;
    const glm::mat4 viewProjection =
        fixtureViewProjection(surfaceWidth, surfaceHeight);

    const auto& character = authoredCharacterPoints(variant, poseBlend);
    const auto characterBounds =
        projectBounds(character, viewProjection, surfaceWidth, surfaceHeight);
    regions["character"] = makeRegion(characterBounds.first,
                                      characterBounds.second, surfaceWidth,
                                      surfaceHeight);

    const auto limbBounds = projectBounds(
        authoredLimbPoints(variant, poseBlend), viewProjection, surfaceWidth,
        surfaceHeight);
    regions["limb"] = makeRegion(limbBounds.first, limbBounds.second,
                                 surfaceWidth, surfaceHeight);

    const auto headBounds = projectBounds(authoredHeadPoints(variant),
                                          viewProjection, surfaceWidth,
                                          surfaceHeight);
    regions["head"] = makeRegion(headBounds.first, headBounds.second,
                                 surfaceWidth, surfaceHeight);

    const auto torsoBounds = projectBounds(authoredTorsoPoints(variant),
                                           viewProjection, surfaceWidth,
                                           surfaceHeight);
    regions["torso"] = makeRegion(torsoBounds.first, torsoBounds.second,
                                  surfaceWidth, surfaceHeight);

    // The floor region covers only the authored floor below the character so a
    // missing environment cannot be masked by character pixels.
    const auto floorBounds = projectBounds(authoredFloorPoints(), viewProjection,
                                           surfaceWidth, surfaceHeight);
    const float floorTop = std::max(
        floorBounds.first.y,
        characterBounds.second.y + 0.02f * static_cast<float>(surfaceHeight));
    if (floorTop < floorBounds.second.y) {
        regions["floor"] = makeRegion({floorBounds.first.x, floorTop},
                                      floorBounds.second, surfaceWidth,
                                      surfaceHeight);
    } else {
        regions["floor"] = {0.0f, 0.0f, 0.0f, 0.0f};
    }

    // The wall region covers only the authored wall left of the character.
    const auto wallBounds = projectBounds(authoredWallPoints(variant),
                                          viewProjection, surfaceWidth,
                                          surfaceHeight);
    const float wallRight = std::min(
        wallBounds.second.x,
        characterBounds.first.x - 0.02f * static_cast<float>(surfaceWidth));
    if (wallBounds.first.x < wallRight) {
        regions["wall"] = makeRegion({wallBounds.first.x, wallBounds.first.y},
                                     {wallRight, wallBounds.second.y},
                                     surfaceWidth, surfaceHeight);
    } else {
        regions["wall"] = {0.0f, 0.0f, 0.0f, 0.0f};
    }

    // The authored albedo inspection surface is variant-independent.
    const auto probeBounds = projectBounds(authoredProbePoints(), viewProjection,
                                           surfaceWidth, surfaceHeight);
    regions["probe"] = makeRegion(probeBounds.first, probeBounds.second,
                                  surfaceWidth, surfaceHeight);

    return regions;
}

FixtureHandshake buildFixtureHandshake(std::string host,
                                       std::string requestedBackend,
                                       const FixtureSession& session,
                                       const FixtureDrawStats& stats,
                                       int surfaceWidth,
                                       int surfaceHeight,
                                       const IRenderBackend& renderer) {
    FixtureHandshake handshake;
    if (session.lifecycle) handshake.lifecycleEvidence = fixtureLifecycleEvidence(session, renderer).dump();
    handshake.schema = "phlosion-scene-fixture-handshake-v1";
    handshake.sampleIdentity =
        std::string("phlosion-scene-fixture-v1/") +
        fixtureCaseName(session.fixtureCase);
    handshake.host = std::move(host);
    handshake.caseName = fixtureCaseName(session.fixtureCase);
    handshake.requestedBackend = std::move(requestedBackend);
    handshake.actualBackend = fixtureBackendName(renderer);
    handshake.gpu = renderer.activeGpuName();
    handshake.gpuDiscrete = renderer.activeGpuIsDiscrete();
    handshake.variantName = sceneVariantName(session.variant);
    handshake.logicalFrame = fixtureCaseLogicalFrame(session.fixtureCase);
    handshake.surfaceWidth = surfaceWidth;
    handshake.surfaceHeight = surfaceHeight;
    handshake.renderFrame = session.renderFrame;
    handshake.transitionFrame = session.transitionFrame;
    handshake.bindPose = session.bindPose;
    handshake.bindPoseBeforeTransition = session.poseBindBeforeTransition;
    handshake.poseTransitionApplied = session.poseTransitionApplied;
    handshake.poseTimeSec = session.pose.timeSec;
    handshake.poseTimeBeforeTransition = session.poseTimeBeforeTransition;
    handshake.assetIdentityBeforeTransition = session.poseIdentityBeforeTransition;
    handshake.assetGenerationBeforeTransition =
        session.poseGenerationBeforeTransition;
    handshake.emissionOff = session.emissionOff;
    handshake.sceneDrawn = session.sceneDrawn;
    handshake.unloaded = session.unloaded;
    handshake.replacementAttempted = session.replacementAttempted;
    handshake.replacementFailed = session.replacementFailed;
    handshake.replacementSucceeded = session.replacementSucceeded;
    handshake.replacementDiagnostic = session.replacementDiagnostic;
    handshake.drawCalls = stats.drawCalls;
    handshake.triangles = stats.triangles;
    handshake.paletteMatrices = stats.paletteMatrices;
    if (session.asset) {
        handshake.assetIdentity = session.asset->sourceIdentity;
        handshake.assetGeneration = session.asset->generation;
        handshake.assetSource = session.asset->sourcePath;
        handshake.assetTextureCount = session.asset->stats.textureCount;
        handshake.assetPrimitiveCount = session.asset->stats.primitiveCount;
        handshake.retainedTextureBytes =
            session.asset->stats.retainedTextureBytes;
        handshake.loadSeconds = session.asset->stats.loadSeconds;
    }
    handshake.poseSeconds = session.pose.sampleSeconds;
    handshake.probePresent = session.probeDrawn;
    handshake.probeDebugStateReset = session.debugStateReset;
    handshake.probeDebugView = session.probeDebugView;
    handshake.probeDrawCalls = stats.probeDrawCalls;
    if (session.probe) {
        handshake.probeIdentity = session.probe->sourceIdentity;
        handshake.probeGeneration = session.probe->generation;
    }
    {
        const AuthoredProbe& probe = authoredProbe();
        handshake.probeBaseColorFactor = probe.baseColorFactor;
        handshake.probeExpectedLeft = authoredProbeExpectedPixel(
            probe.map.left[0], probe.map.left[1], probe.map.left[2]);
        handshake.probeExpectedRight = authoredProbeExpectedPixel(
            probe.map.right[0], probe.map.right[1], probe.map.right[2]);
    }
    handshake.regions = computeFixtureRegionRects(
        session.variant, fixturePoseBlend(session.fixtureCase), surfaceWidth,
        surfaceHeight);
    return handshake;
}

bool writeFixtureHandshake(const std::string& path,
                           const FixtureHandshake& handshake,
                           std::string* outError) {
    if (path.empty()) {
        return fail(outError, "scene fixture handshake path must not be empty");
    }
    nlohmann::json document;
    document["schema"] = handshake.schema;
    document["sample_identity"] = handshake.sampleIdentity;
    document["host"] = handshake.host;
    document["case"] = handshake.caseName;
    document["variant"] = handshake.variantName;
    document["requested_backend"] = handshake.requestedBackend;
    document["actual_backend"] = handshake.actualBackend;
    document["gpu"] = handshake.gpu;
    document["gpu_discrete"] = handshake.gpuDiscrete;
    document["logical_frame"] = handshake.logicalFrame;
    document["render_frame"] = handshake.renderFrame;
    document["transition_frame"] = handshake.transitionFrame;
    if (!handshake.lifecycleEvidence.empty())
        document["lifecycle"] = nlohmann::json::parse(handshake.lifecycleEvidence);
    document["surface"] = {{"width", handshake.surfaceWidth},
                           {"height", handshake.surfaceHeight}};
    document["pose"] = {{"bind", handshake.bindPose},
                        {"bind_before_transition",
                         handshake.bindPoseBeforeTransition},
                        {"transition_applied", handshake.poseTransitionApplied},
                        {"time_sec", handshake.poseTimeSec},
                        {"time_before_transition",
                         handshake.poseTimeBeforeTransition},
                        {"identity_before_transition",
                         handshake.assetIdentityBeforeTransition},
                        {"generation_before_transition",
                         handshake.assetGenerationBeforeTransition},
                        {"emission_off", handshake.emissionOff}};
    document["scene"] = {{"drawn", handshake.sceneDrawn},
                         {"unloaded", handshake.unloaded},
                         {"identity", handshake.assetIdentity},
                         {"generation", handshake.assetGeneration},
                         {"source", handshake.assetSource},
                         {"texture_count", handshake.assetTextureCount},
                         {"primitive_count", handshake.assetPrimitiveCount},
                         {"retained_texture_bytes",
                          handshake.retainedTextureBytes},
                         {"load_seconds", handshake.loadSeconds},
                         {"pose_seconds", handshake.poseSeconds}};
    document["replacement"] = {
        {"attempted", handshake.replacementAttempted},
        {"failed", handshake.replacementFailed},
        {"succeeded", handshake.replacementSucceeded},
        {"diagnostic", handshake.replacementDiagnostic}};
    document["stats"] = {{"draw_calls", handshake.drawCalls},
                         {"triangles", handshake.triangles},
                         {"palette_matrices", handshake.paletteMatrices},
                         {"probe_draw_calls", handshake.probeDrawCalls}};
    document["probe"] = {
        {"present", handshake.probePresent},
        {"identity", handshake.probeIdentity},
        {"generation", handshake.probeGeneration},
        {"debug_view", handshake.probeDebugView},
        {"debug_state_reset", handshake.probeDebugStateReset},
        {"base_color_factor", handshake.probeBaseColorFactor},
        {"expected_left_rgb", handshake.probeExpectedLeft},
        {"expected_right_rgb", handshake.probeExpectedRight}};
    nlohmann::json regions = nlohmann::json::object();
    for (const auto& entry : handshake.regions) {
        regions[entry.first] = entry.second;
    }
    document["regions"] = std::move(regions);

    try {
        const std::filesystem::path outputPath(path);
        if (!outputPath.parent_path().empty()) {
            std::filesystem::create_directories(outputPath.parent_path());
        }
        std::ofstream output(outputPath, std::ios::trunc);
        if (!output) {
            return fail(outError,
                        "could not open scene fixture handshake path: " + path);
        }
        output << document.dump(2) << '\n';
        if (!output) {
            return fail(outError,
                        "could not write scene fixture handshake path: " + path);
        }
    } catch (const std::exception& exception) {
        return fail(outError,
                    std::string("scene fixture handshake write failed: ") +
                        exception.what());
    }
    if (outError != nullptr) {
        outError->clear();
    }
    return true;
}

const char* fixtureBackendName(const IRenderBackend& renderer) noexcept {
    const char* backend = renderer.backendId();
    return backend != nullptr && backend[0] != '\0' ? backend : "unknown";
}

}  // namespace engine::scene_fixture
