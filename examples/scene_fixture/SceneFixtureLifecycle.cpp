#include "SceneFixtureLifecycle.h"
#include "SceneFixturePresentation.h"
#include "engine/render/gltf/GltfSceneSession.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <thread>

namespace engine::scene_fixture {
namespace sc = engine::render::gltf::scene;
namespace ph = engine::assets::phlosion;
namespace {
using namespace std::chrono_literals;
using Json = nlohmann::json;

class FixtureArchiveStore final : public IAssetStore {
public:
    std::map<std::string, std::vector<std::uint8_t>> files;
    bool readBytes(const std::string& path, std::vector<std::uint8_t>& out, std::string* error) const override {
        const auto found = files.find(path);
        if (found == files.end()) { if (error) *error = "missing fixture archive " + path; return false; }
        out = found->second;
        return true;
    }
    bool readText(const std::string& path, std::string& out, std::string* error) const override {
        std::vector<std::uint8_t> bytes;
        if (!readBytes(path, bytes, error)) return false;
        out.assign(bytes.begin(), bytes.end());
        return true;
    }
    bool exists(const std::string& path) const override { return files.contains(path); }
};

void require(bool condition, const std::string& error) {
    if (!condition) throw std::runtime_error(error);
}

// Explicit bounded test barrier: the engine session itself never waits in pump.
// Native capture frames are deterministic; this is not a production frame loop.
void settle(sc::GltfSceneSession& session) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (session.state() == sc::GltfSceneSessionState::Loading ||
           session.state() == sc::GltfSceneSessionState::Decoding) {
        require(std::chrono::steady_clock::now() < deadline, "fixture archive decode timed out");
        session.pump();
        std::this_thread::yield();
    }
}

Json counts(const engine::render::WorldResourceCounts& c) {
    return {{"geometry_buffers", c.geometryBuffers}, {"textures", c.textures},
            {"material_descriptors", c.materialDescriptors}, {"texture_descriptors", c.textureDescriptors},
            {"payload_bytes", c.payloadBytes}, {"api_allocation_bytes", c.apiAllocationBytes},
            {"api_allocation_estimated", c.apiAllocationBytesEstimated}};
}

void makeArchive(FixtureArchiveStore& store, const std::string& id,
                 const std::filesystem::path& directory) {
    std::vector<ph::SceneArchiveFile> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream input(entry.path(), std::ios::binary);
        require(static_cast<bool>(input), "could not read fixture archive input");
        files.push_back({entry.path().filename().string(),
            {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()}});
    }
    std::string error;
    require(ph::encodeSceneArchive(id, std::move(files), store.files[id], &error), error);
}
}

struct FixtureLifecycleState {
    std::unique_ptr<sc::GltfSceneSession> loader;
    std::uint64_t boundGeneration = 0u;
    std::vector<std::pair<engine::render::WorldResourceScopeId, int>> retiring;
    Json frames = Json::array();
    Json releases = Json::array();
    Json loads = Json::array();
    int cancelled = 0;
    int retainedDraws = 0;
    bool pendingObserved = false;
    bool releaseAfterDraw = false;
};

bool prepareFixtureLifecycle(FixtureSession& session, const std::string& directory,
                              std::string* error) {
    try {
        auto store = std::make_shared<FixtureArchiveStore>();
        for (const auto& id : {"a", "off", "b"}) {
            require(stageSceneDocument(std::string(id) == "b" ? SceneVariant::B : SceneVariant::A,
                std::string(id) == "off", directory, error), error ? *error : "stage failed");
            makeArchive(*store, id, directory);
        }
        require(stageMalformedDocument(directory, error), error ? *error : "bad stage failed");
        makeArchive(*store, "bad", directory);
        for (int cycle = 0; cycle < kLifecycleCycles; ++cycle) {
            require(stageSceneDocument(cycle % 2 == 0 ? SceneVariant::A : SceneVariant::B,
                false, directory, error), error ? *error : "cycle stage failed");
            const auto path = std::filesystem::path(directory) / sceneDocumentFileName();
            Json document;
            { std::ifstream input(path); input >> document; }
            // Distinct authored transforms and source identities per cycle.
            document["nodes"][0]["translation"] = {0.001f * static_cast<float>(cycle + 1), 0.f, 0.f};
            { std::ofstream output(path); output << document; }
            makeArchive(*store, "cycle-" + std::to_string(cycle), directory);
        }
        auto state = std::make_shared<FixtureLifecycleState>();
        state->loader = std::make_unique<sc::GltfSceneSession>(store,
            [](const std::string& id, ph::AssetLocation& location, std::string*) {
                location = {id, ph::AssetKind::Phrc}; return true;
            });
        require(state->loader->begin("a", ph::ArchiveKind::Scene, sceneDocumentFileName(), error),
                error ? *error : "initial archive begin failed");
        settle(*state->loader);
        require(state->loader->state() == sc::GltfSceneSessionState::Staged && state->loader->activate(),
                state->loader->error());
        session.asset = state->loader->active()->scene;
        session.lifecycle = std::move(state);
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

bool advanceFixtureLifecycle(FixtureSession& session, int frame, std::string* error) {
    try {
        auto& life = *session.lifecycle;
        auto& loader = *life.loader;
        session.renderFrame = frame;
        const auto load = [&](const std::string& id, bool bind) {
            loader.discard();
            require(loader.begin(id, ph::ArchiveKind::Scene, sceneDocumentFileName(), error),
                    error ? *error : "begin failed");
            settle(loader);
            require(loader.state() == sc::GltfSceneSessionState::Staged && loader.activate(), loader.error());
            session.asset = loader.active()->scene;
            require(sc::sampleGltfScenePose(session.asset, bind ? 0.f : fixturePoseTimeSec(),
                    bind ? sc::kBindPoseClip : 0, session.pose, error), error ? *error : "pose failed");
            life.loads.push_back({{"id", id}, {"frame", frame}, {"identity", session.asset->sourceIdentity},
                {"encoded_bytes", loader.active()->encodedGraphBytes},
                {"decoded_bytes", session.asset->stats.retainedAssetBytes},
                {"decode_seconds", loader.active()->decodeSeconds}});
        };
        if (frame <= kLifecycleWarmupFrames) {
            const int phase = (frame - 1) % 3;
            if (phase == 0) {
                const int cycle = (frame - 1) / 3;
                load("cycle-" + std::to_string(cycle), false);
                session.variant = cycle % 2 == 0 ? SceneVariant::A : SceneVariant::B;
            } else if (phase == 1) {
                require(loader.begin("b", ph::ArchiveKind::Scene, sceneDocumentFileName(), error),
                        error ? *error : "pending begin failed");
                loader.pump();
                require(loader.active() && loader.active()->scene == session.asset,
                        "pending archive replaced the active scene");
                life.pendingObserved = true;
            } else {
                require(loader.cancel(), "fixture could not cancel pending replacement");
                ++life.cancelled;
                // The independently retained asset is sampled and drawn once
                // more after its session relinquishes it, before GPU retirement.
                loader.unload();
                require(sc::sampleGltfScenePose(session.asset, fixturePoseTimeSec(), 0, session.pose, error),
                        error ? *error : "retained pose failed");
                life.releaseAfterDraw = true;
                ++life.retainedDraws;
            }
            return true;
        }
        const int logical = frame - kLifecycleWarmupFrames;
        if (logical == 1) {
            load(session.emissionOff ? "off" : "a", session.bindPose || session.fixtureCase == FixtureCase::APose);
            session.variant = SceneVariant::A;
            session.poseIdentityBeforeTransition = session.asset->sourceIdentity;
            session.poseGenerationBeforeTransition = session.asset->generation;
            session.poseBindBeforeTransition = session.bindPose || session.fixtureCase == FixtureCase::APose;
            session.poseTimeBeforeTransition = session.pose.timeSec;
            if (session.fixtureCase == FixtureCase::FailedReplacement || session.fixtureCase == FixtureCase::ReplacementB) {
                require(loader.begin(session.fixtureCase == FixtureCase::FailedReplacement ? "bad" : "b",
                    ph::ArchiveKind::Scene, sceneDocumentFileName(), error), error ? *error : "begin failed");
                loader.pump();
            }
        }
        if (logical < fixtureTransitionFrame() || session.transitionApplied) return true;
        session.transitionApplied = true;
        session.transitionFrame = frame;
        switch (session.fixtureCase) {
        case FixtureCase::APose:
            require(sc::sampleGltfScenePose(session.asset, fixturePoseTimeSec(), 0, session.pose, error),
                    error ? *error : "transition pose failed");
            session.poseTransitionApplied = true;
            break;
        case FixtureCase::FailedReplacement:
            settle(loader);
            require(loader.state() == sc::GltfSceneSessionState::Failed &&
                    loader.active()->scene == session.asset, "bad replacement did not preserve active");
            session.replacementAttempted = session.replacementFailed = true;
            session.replacementDiagnostic = loader.error();
            break;
        case FixtureCase::ReplacementB:
            settle(loader);
            require(loader.state() == sc::GltfSceneSessionState::Staged && loader.activate(), loader.error());
            session.asset = loader.active()->scene;
            require(sc::sampleGltfScenePose(session.asset, fixturePoseTimeSec(), 0, session.pose, error),
                    error ? *error : "replacement pose failed");
            session.variant = SceneVariant::B;
            session.replacementAttempted = session.replacementSucceeded = true;
            break;
        case FixtureCase::Unloaded:
            loader.unload();
            session.asset.reset();
            session.probe.reset();
            session.pose = {};
            session.probePose = {};
            session.probeDrawn = session.debugStateReset = false;
            session.unloaded = true;
            break;
        default: break;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

bool beginFixtureLifecycleDraw(IRenderBackend& renderer, FixtureSession& session, std::string* error) {
    try {
        auto* api = dynamic_cast<IWorldResourceScope*>(&renderer);
        require(api != nullptr, "native renderer has no managed resource scopes");
        auto& life = *session.lifecycle;
        for (const auto& [id, retiredFrame] : life.retiring) {
            engine::render::WorldResourceScopeStats scope;
            require(!api->getWorldResourceScopeStats(id, scope), "GPU scope survived the next frame maintenance");
            life.releases.push_back({{"scope", id}, {"requested_frame", retiredFrame},
                                     {"collected_frame", session.renderFrame}});
        }
        life.retiring.clear();
        const auto retire = [&](engine::render::WorldResourceScopeId& id) {
            if (id == 0u) return;
            require(api->requestWorldResourceScopeRelease(id), "could not retire fixture scope");
            life.retiring.emplace_back(id, session.renderFrame);
            id = 0u;
        };
        if (!session.asset || session.asset->generation != life.boundGeneration) retire(session.assetScope);
        if (!session.probe) retire(session.probeScope);
        if (session.asset && session.assetScope == 0u) {
            session.assetScope = api->createWorldResourceScope(64ull << 20);
            require(session.assetScope != 0u, "scene GPU scope capacity exhausted");
            life.boundGeneration = session.asset->generation;
        }
        if (session.probe && session.probeScope == 0u) {
            session.probeScope = api->createWorldResourceScope(1ull << 20);
            require(session.probeScope != 0u, "probe GPU scope capacity exhausted");
        }
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

bool finishFixtureLifecycleDraw(IRenderBackend& renderer, FixtureSession& session, std::string* error) {
    try {
        auto& api = *dynamic_cast<IWorldResourceScope*>(&renderer);
        auto& life = *session.lifecycle;
        const auto aggregate = api.getWorldResourceStats();
        Json row{{"frame", session.renderFrame}, {"live_scopes", aggregate.liveScopes},
            {"pending_scopes", aggregate.pendingReleaseScopes}, {"retained", counts(aggregate.retained)},
            {"allocated", counts(aggregate.allocated)}, {"reclaimed", counts(aggregate.reclaimed)},
            {"allocation_failures", aggregate.allocationFailures},
            {"candidate_state", static_cast<int>(life.loader->state())},
            {"session_decoded_bytes", life.loader->decodedResidentBytes()}};
        if (session.asset) {
            engine::render::WorldResourceScopeStats scope;
            require(api.getWorldResourceScopeStats(session.assetScope, scope) &&
                    scope.status == engine::render::WorldResourceScopeStatus::Ready &&
                    scope.retained.geometryBuffers > 0u && scope.retained.textures > 0u,
                    "managed scene did not retain native geometry/textures");
            row["asset_scope"] = session.assetScope;
            row["asset_resources"] = counts(scope.retained);
            row["asset_identity"] = session.asset->sourceIdentity;
        }
        if (session.probe) {
            engine::render::WorldResourceScopeStats scope;
            require(api.getWorldResourceScopeStats(session.probeScope, scope) &&
                    scope.status == engine::render::WorldResourceScopeStatus::Ready && scope.retained.textures > 0u,
                    "independent live probe scope lost its texture");
            row["probe_scope"] = session.probeScope;
            row["probe_resources"] = counts(scope.retained);
        }
        life.frames.push_back(std::move(row));
        if (life.releaseAfterDraw) {
            require(api.requestWorldResourceScopeRelease(session.assetScope), "cycle release failed");
            life.retiring.emplace_back(session.assetScope, session.renderFrame);
            session.assetScope = 0u;
            session.asset.reset();
            session.pose = {};
            life.releaseAfterDraw = false;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

nlohmann::json fixtureLifecycleEvidence(const FixtureSession& session, const IRenderBackend& renderer) {
    if (!session.lifecycle) return nullptr;
    const auto& life = *session.lifecycle;
    const auto* api = dynamic_cast<const IWorldResourceScope*>(&renderer);
    const auto aggregate = api->getWorldResourceStats();
    return {{"schema", "phlosion-scene-lifecycle-v1"}, {"cycles", kLifecycleCycles},
        {"warmup_frames", kLifecycleWarmupFrames}, {"cancelled_candidates", life.cancelled},
        {"retained_draws", life.retainedDraws}, {"pending_observed", life.pendingObserved},
        {"loads", life.loads}, {"frame_samples", life.frames}, {"releases", life.releases},
        {"final_live_scopes", aggregate.liveScopes}, {"final_retained", counts(aggregate.retained)},
        {"final_decoded_bytes", life.loader->decodedResidentBytes()}};
}
} // namespace engine::scene_fixture
