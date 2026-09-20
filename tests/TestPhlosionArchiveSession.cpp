#include "engine/assets/phlosion/PhlosionArchiveSession.h"

#include "engine/assets/phlosion/PhlosionResourceContainer.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using engine::assets::phlosion::ArchiveKind;
using engine::assets::phlosion::ArchiveSessionState;
using engine::assets::phlosion::ArchiveSnapshot;
using engine::assets::phlosion::AssetGraphOptions;
using engine::assets::phlosion::AssetKind;
using engine::assets::phlosion::AssetLocation;
using engine::assets::phlosion::AssetResolver;
using engine::assets::phlosion::LoadedAsset;
using engine::assets::phlosion::PhlosionArchiveSession;
using engine::assets::phlosion::PrefabArchiveStore;
using engine::assets::phlosion::SceneArchiveFile;
using engine::assets::phlosion::SceneArchiveStore;
using engine::assets::phrc::Dependency;

constexpr std::chrono::milliseconds kWaitTimeout{15000};
constexpr std::chrono::milliseconds kPollInterval{1};

std::vector<std::uint8_t> bytesOf(std::string_view text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

Dependency requiredDep(std::string assetId, std::uint64_t expectedHash = 0u) {
    Dependency dependency;
    dependency.assetId = std::move(assetId);
    dependency.expectedContentHash = expectedHash;
    dependency.flags = engine::assets::phrc::kDependencyRequired;
    return dependency;
}

Dependency optionalDep(std::string assetId) {
    Dependency dependency;
    dependency.assetId = std::move(assetId);
    dependency.expectedContentHash = 0u;
    dependency.flags = 0u;
    return dependency;
}

std::uint64_t phrcHash(const std::vector<std::uint8_t>& bytes) {
    engine::assets::phrc::Document document;
    if (!engine::assets::phrc::decode(bytes, document, nullptr)) {
        return 0u;
    }
    return document.contentHash;
}

// Immutable in-memory provider. A read can be paused on a gate so a test can
// observe an in-flight read deterministically; every wait is bounded and a
// timed-out gate is recorded so an ordering claim cannot be satisfied by the
// timeout instead of the gate.
class SyntheticStore final : public engine::IAssetStore {
public:
    void add(std::string virtualPath, std::vector<std::uint8_t> bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        assets_.insert_or_assign(std::move(virtualPath), std::move(bytes));
    }

    void gate(std::string virtualPath) {
        std::lock_guard<std::mutex> lock(mutex_);
        gated_.insert(std::move(virtualPath));
    }

    void openGate() {
        std::lock_guard<std::mutex> lock(mutex_);
        gateOpen_ = true;
        changed_.notify_all();
    }

    bool waitForReadStarted(
        const std::string& virtualPath,
        std::string& outFail) const {
        return waitUntil(
            [this, &virtualPath] { return readCounts_.contains(virtualPath); },
            "read start for '" + virtualPath + "'",
            outFail);
    }

    // A path that was already read in an earlier candidate cannot prove that a
    // new read started, so ordering claims wait for a captured count plus one.
    bool waitForReadCountAtLeast(
        const std::string& virtualPath,
        const std::size_t minimum,
        std::string& outFail) const {
        return waitUntil(
            [this, &virtualPath, minimum] {
                const auto found = readCounts_.find(virtualPath);
                return found != readCounts_.end() &&
                    found->second >= minimum;
            },
            "read count " + std::to_string(minimum) + " for '" +
                virtualPath + "'",
            outFail);
    }

    std::size_t readCount(const std::string& virtualPath) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = readCounts_.find(virtualPath);
        return found == readCounts_.end() ? 0u : found->second;
    }

    bool sawReadOf(const std::string& virtualPath) const {
        return readCount(virtualPath) > 0u;
    }

    std::vector<std::string> gateTimeouts() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return gateTimeouts_;
    }

    std::vector<std::string> readOrder() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return readOrder_;
    }

    bool readText(
        const std::string& virtualPath,
        std::string& outText,
        std::string* outError = nullptr) const override {
        std::vector<std::uint8_t> bytes;
        if (!readBytes(virtualPath, bytes, outError)) {
            return false;
        }
        outText.assign(
            reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    }

    bool readBytes(
        const std::string& virtualPath,
        std::vector<std::uint8_t>& outBytes,
        std::string* outError = nullptr) const override {
        std::unique_lock<std::mutex> lock(mutex_);
        ++readCounts_[virtualPath];
        readOrder_.push_back(virtualPath);
        changed_.notify_all();
        if (gated_.contains(virtualPath)) {
            if (!changed_.wait_for(
                    lock, kWaitTimeout, [this] { return gateOpen_; })) {
                gateTimeouts_.push_back(virtualPath);
            }
        }
        const auto found = assets_.find(virtualPath);
        if (found == assets_.end()) {
            if (outError) {
                *outError =
                    "synthetic provider could not read '" + virtualPath + "'";
            }
            return false;
        }
        outBytes = found->second;
        return true;
    }

    bool exists(const std::string& virtualPath) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return assets_.contains(virtualPath);
    }

private:
    template <typename Predicate>
    bool waitUntil(
        Predicate predicate,
        std::string description,
        std::string& outFail) const {
        const auto deadline = std::chrono::steady_clock::now() + kWaitTimeout;
        std::unique_lock<std::mutex> lock(mutex_);
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate()) {
                return true;
            }
            changed_.wait_for(lock, kPollInterval);
        }
        outFail = "timed out waiting for " + description;
        return false;
    }

    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    mutable std::map<std::string, std::vector<std::uint8_t>> assets_;
    mutable std::set<std::string> gated_;
    mutable bool gateOpen_ = false;
    mutable std::map<std::string, std::size_t> readCounts_;
    mutable std::vector<std::string> readOrder_;
    mutable std::vector<std::string> gateTimeouts_;
};

// Opens every gate on destruction. Declare it after a session so the gate is
// always open before the session destructor joins a paused read.
class GateCleanup {
public:
    explicit GateCleanup(std::shared_ptr<SyntheticStore> store)
        : store_(std::move(store)) {}

    ~GateCleanup() {
        store_->openGate();
    }

    GateCleanup(const GateCleanup&) = delete;
    GateCleanup& operator=(const GateCleanup&) = delete;

private:
    std::shared_ptr<SyntheticStore> store_;
};

class ResolverFixture {
public:
    void map(
        std::string assetId,
        std::string virtualPath,
        AssetKind kind = AssetKind::Phrc) {
        locations_.insert_or_assign(
            std::move(assetId),
            AssetLocation{std::move(virtualPath), kind});
    }

    std::size_t resolveCount(const std::string& assetId) const {
        const auto found = resolveCounts_.find(assetId);
        return found == resolveCounts_.end() ? 0u : found->second;
    }

    AssetResolver resolver() {
        return [this](
                   const std::string& assetId,
                   AssetLocation& outLocation,
                   std::string* outError) {
            ++resolveCounts_[assetId];
            const auto found = locations_.find(assetId);
            if (found == locations_.end()) {
                if (outError) {
                    *outError = "no mapping for asset '" + assetId + "'";
                }
                return false;
            }
            outLocation = found->second;
            return true;
        };
    }

private:
    std::map<std::string, AssetLocation> locations_;
    mutable std::map<std::string, std::size_t> resolveCounts_;
};

struct ArchiveFixture {
    std::vector<std::uint8_t> bytes;
    std::uint64_t contentHash = 0u;
};

bool registerPhrcAsset(
    const std::shared_ptr<SyntheticStore>& store,
    ResolverFixture& resolver,
    const std::string& assetId,
    const std::string& virtualPath,
    std::vector<std::uint8_t> bytes,
    ArchiveFixture& outFixture,
    std::string& outFail) {
    outFixture.contentHash = phrcHash(bytes);
    if (outFixture.contentHash == 0u) {
        outFail = "synthetic asset '" + assetId + "' is not a valid PHRC";
        return false;
    }
    outFixture.bytes = bytes;
    store->add(virtualPath, std::move(bytes));
    resolver.map(assetId, virtualPath, AssetKind::Phrc);
    return true;
}

bool buildSceneBytes(
    const std::string& sceneId,
    std::vector<SceneArchiveFile> files,
    std::vector<Dependency> dependencies,
    std::vector<std::uint8_t>& outBytes,
    std::string& outFail) {
    if (!engine::assets::phlosion::encodeSceneArchive(
            sceneId, std::move(files), outBytes, &outFail)) {
        return false;
    }
    if (dependencies.empty()) {
        return true;
    }
    engine::assets::phrc::Document document;
    if (!engine::assets::phrc::decode(outBytes, document, &outFail)) {
        return false;
    }
    document.dependencies = std::move(dependencies);
    return engine::assets::phrc::encode(document, outBytes, &outFail);
}

const char* stateName(ArchiveSessionState state) {
    switch (state) {
    case ArchiveSessionState::Idle: return "Idle";
    case ArchiveSessionState::Loading: return "Loading";
    case ArchiveSessionState::Staged: return "Staged";
    case ArchiveSessionState::Failed: return "Failed";
    case ArchiveSessionState::Cancelled: return "Cancelled";
    }
    return "invalid";
}

// Bounded pump loop. The session itself never waits for I/O.
bool pumpToTerminal(
    PhlosionArchiveSession& session,
    ArchiveSessionState& outState,
    std::string& outFail) {
    const auto deadline = std::chrono::steady_clock::now() + kWaitTimeout;
    for (;;) {
        session.pump();
        const ArchiveSessionState state = session.state();
        if (state != ArchiveSessionState::Loading) {
            outState = state;
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            outFail = "timed out pumping the archive session while Loading";
            return false;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
}

bool expectStaged(PhlosionArchiveSession& session, std::string& outFail) {
    ArchiveSessionState state = ArchiveSessionState::Idle;
    if (!pumpToTerminal(session, state, outFail)) {
        return false;
    }
    if (state != ArchiveSessionState::Staged) {
        outFail = std::string("the candidate is ") + stateName(state) +
            " instead of Staged: " + session.error();
        return false;
    }
    return true;
}

bool expectFailed(
    PhlosionArchiveSession& session,
    const std::string& expectText,
    const std::string& alternativeText,
    std::string& outFail) {
    ArchiveSessionState state = ArchiveSessionState::Idle;
    if (!pumpToTerminal(session, state, outFail)) {
        return false;
    }
    if (state != ArchiveSessionState::Failed) {
        outFail = std::string("the candidate is ") + stateName(state) +
            " instead of Failed";
        return false;
    }
    if (session.staged()) {
        outFail = "a failed candidate exposed a staged snapshot";
        return false;
    }
    if (session.error().find(expectText) == std::string::npos &&
        session.error().find(alternativeText) == std::string::npos) {
        outFail = "the candidate diagnostic '" + session.error() +
            "' does not contain '" + expectText + "' or '" +
            alternativeText + "'";
        return false;
    }
    return true;
}

bool expectNoGateTimeout(
    const std::shared_ptr<SyntheticStore>& store,
    std::string& outFail) {
    const std::vector<std::string> timeouts = store->gateTimeouts();
    if (timeouts.empty()) {
        return true;
    }
    outFail = "the provider gate wait timed out for '" + timeouts.front() +
        "', so the paused-read ordering claim is not valid";
    return false;
}

const LoadedAsset* findAsset(
    const ArchiveSnapshot& snapshot,
    const std::string& assetId) {
    const auto found = snapshot.graph.assets.find(assetId);
    return found == snapshot.graph.assets.end() ? nullptr : &found->second;
}

std::vector<std::uint8_t> sceneGeometry() {
    return std::vector<std::uint8_t>{0u, 1u, 2u, 3u, 255u};
}

const std::string kSceneJsonText =
    R"({"schema_version":1,"kind":"archive_session_fixture"})";

// A real PHSC archive with two embedded files, one required raw dependency,
// one skipped optional edge and a registry ID that differs from the embedded
// scene ID.
bool caseSceneStagingAndActivation(std::string& outFail) {
    const std::shared_ptr<SyntheticStore> store =
        std::make_shared<SyntheticStore>();
    ResolverFixture resolver;
    std::string error;

    const std::vector<std::uint8_t> rawPayload = bytesOf("raw-leaf-bytes");
    const std::uint64_t rawHash =
        engine::assets::phrc::contentHash64(rawPayload);
    store->add("session/raw.bin", rawPayload);
    resolver.map("session/raw-leaf", "session/raw.bin", AssetKind::Bytes);

    std::vector<std::uint8_t> sceneBytes;
    if (!buildSceneBytes(
            "canonical/route-scene",
            {SceneArchiveFile{"canonical/scene.json", bytesOf(kSceneJsonText)},
             SceneArchiveFile{"canonical/geometry.bin", sceneGeometry()}},
            {requiredDep("session/raw-leaf", rawHash),
             optionalDep("session/optional-visual")},
            sceneBytes,
            outFail)) {
        return false;
    }
    ArchiveFixture scene;
    if (!registerPhrcAsset(
            store, resolver, "registry/opaque-alias",
            "session/opaque-alias.phscene", std::move(sceneBytes), scene,
            outFail)) {
        return false;
    }

    PhlosionArchiveSession session(store, resolver.resolver());
    if (session.state() != ArchiveSessionState::Idle ||
        session.staged() || session.active() ||
        !session.error().empty()) {
        outFail = "a fresh session is not idle";
        return false;
    }
    if (!session.begin("registry/opaque-alias", ArchiveKind::Scene, &error)) {
        outFail = "a valid scene root was rejected: " + error;
        return false;
    }
    if (session.state() != ArchiveSessionState::Loading ||
        resolver.resolveCount("registry/opaque-alias") != 0u ||
        store->sawReadOf("session/opaque-alias.phscene")) {
        outFail = "begin resolved, read or left Idle";
        return false;
    }
    if (session.staged()) {
        outFail = "staged() is non-null before the candidate is Staged";
        return false;
    }
    if (!expectStaged(session, outFail)) {
        return false;
    }
    const std::shared_ptr<const ArchiveSnapshot> staged = session.staged();
    if (!staged || !staged->scene || staged->prefab ||
        staged->kind != ArchiveKind::Scene) {
        outFail = "the staged snapshot type contract is wrong";
        return false;
    }
    if (staged->scene->sceneId() != "canonical/route-scene" ||
        staged->scene->fileCount() != 2u) {
        outFail = "the staged scene identity does not match the archive";
        return false;
    }
    std::string decodedText;
    std::vector<std::uint8_t> decodedGeometry;
    if (!staged->scene->readText("canonical/scene.json", decodedText, &error) ||
        decodedText != kSceneJsonText ||
        !staged->scene->readBytes(
            "canonical/geometry.bin", decodedGeometry, &error) ||
        decodedGeometry != sceneGeometry()) {
        outFail =
            "the staged scene did not expose the exact embedded bytes: " +
            error;
        return false;
    }
    const LoadedAsset* graphRoot = findAsset(*staged, "registry/opaque-alias");
    const LoadedAsset* graphDep = findAsset(*staged, "session/raw-leaf");
    if (!graphRoot || !graphDep ||
        graphRoot->kind != AssetKind::Phrc ||
        graphDep->kind != AssetKind::Bytes ||
        graphDep->bytes != rawPayload ||
        graphDep->contentHash != rawHash ||
        graphRoot->contentHash != scene.contentHash) {
        outFail = "the staged graph did not retain the required closure";
        return false;
    }
    if (staged->graph.skippedOptionalDependencies.size() != 1u ||
        staged->graph.skippedOptionalDependencies[0].parentAssetId !=
            "registry/opaque-alias" ||
        staged->graph.skippedOptionalDependencies[0].dependency.assetId !=
            "session/optional-visual") {
        outFail = "the skipped optional edge was not retained";
        return false;
    }
    if (resolver.resolveCount("session/optional-visual") != 0u ||
        store->sawReadOf("session/optional-visual")) {
        outFail = "an optional dependency was resolved or read";
        return false;
    }
    if (!session.activate() ||
        session.state() != ArchiveSessionState::Idle ||
        session.staged() || !session.error().empty() ||
        session.active() != staged) {
        outFail = "activate did not publish the staged snapshot";
        return false;
    }
    if (store->readCount("session/opaque-alias.phscene") != 1u ||
        store->readCount("session/raw.bin") != 1u ||
        resolver.resolveCount("registry/opaque-alias") != 1u) {
        outFail = "the candidate re-resolved or re-read an asset";
        return false;
    }
    return true;
}

bool nextSceneFixture(
    const std::shared_ptr<SyntheticStore>& store,
    ResolverFixture& resolver,
    const std::string& assetId,
    const std::string& virtualPath,
    const std::string& sceneId,
    const std::string& filePath,
    const std::string& fileText,
    std::string& outFail) {
    std::vector<std::uint8_t> bytes;
    if (!buildSceneBytes(
            sceneId,
            {SceneArchiveFile{filePath, bytesOf(fileText)}},
            {},
            bytes,
            outFail)) {
        return false;
    }
    ArchiveFixture fixture;
    return registerPhrcAsset(
        store, resolver, assetId, virtualPath, std::move(bytes), fixture,
        outFail);
}

// Prefab root -> required scene -> required raw leaf. `dependencyOnly` covers
// the prefab archive with no embedded file at all, the other variant a real
// embedded file on the prefab.
bool casePrefabGraphStaging(
    const bool dependencyOnly,
    std::string& outFail) {
    const std::shared_ptr<SyntheticStore> store =
        std::make_shared<SyntheticStore>();
    ResolverFixture resolver;
    std::string error;

    const std::vector<std::uint8_t> rawPayload = bytesOf("prefab-raw-leaf");
    const std::uint64_t rawHash =
        engine::assets::phrc::contentHash64(rawPayload);
    store->add("session/prefab-raw.bin", rawPayload);
    resolver.map(
        "session/prefab-raw", "session/prefab-raw.bin", AssetKind::Bytes);

    std::vector<std::uint8_t> sceneBytes;
    if (!buildSceneBytes(
            "session/dependency-scene",
            {SceneArchiveFile{"canonical/scene.json", bytesOf(kSceneJsonText)}},
            {requiredDep("session/prefab-raw", rawHash)},
            sceneBytes,
            outFail)) {
        return false;
    }
    ArchiveFixture scene;
    if (!registerPhrcAsset(
            store, resolver, "session/dependency-scene",
            "session/dependency-scene.phscene", std::move(sceneBytes), scene,
            outFail)) {
        return false;
    }

    std::vector<SceneArchiveFile> prefabFiles;
    if (!dependencyOnly) {
        prefabFiles.push_back(
            SceneArchiveFile{
                "canonical/prefab.json",
                bytesOf(R"({"kind":"fixture_prop"})")});
    }
    std::vector<std::uint8_t> prefabBytes;
    if (!engine::assets::phlosion::encodePrefabArchive(
            "session/lamp-prefab",
            "FixtureProp",
            R"({"canonical_root":"stage"})",
            std::move(prefabFiles),
            {requiredDep("session/dependency-scene", scene.contentHash)},
            prefabBytes,
            &error)) {
        outFail = "prefab fixture encode failed: " + error;
        return false;
    }
    ArchiveFixture prefab;
    if (!registerPhrcAsset(
            store, resolver, "registry/lamp", "session/lamp.phlo",
            std::move(prefabBytes), prefab, outFail)) {
        return false;
    }

    PhlosionArchiveSession session(store, resolver.resolver());
    if (!session.begin("registry/lamp", ArchiveKind::Prefab, &error) ||
        !expectStaged(session, outFail)) {
        return false;
    }
    const std::shared_ptr<const ArchiveSnapshot> staged = session.staged();
    if (!staged || staged->scene || !staged->prefab ||
        staged->kind != ArchiveKind::Prefab ||
        staged->graph.assets.size() != 3u) {
        outFail = "the staged prefab snapshot is not the expected closure";
        return false;
    }
    if (staged->prefab->prefabId() != "session/lamp-prefab" ||
        staged->prefab->prefabKind() != "FixtureProp" ||
        staged->prefab->fileCount() != (dependencyOnly ? 0u : 1u)) {
        outFail = "the staged prefab identity does not match the archive";
        return false;
    }
    if (dependencyOnly) {
        if (staged->prefab->exists("canonical/prefab.json")) {
            outFail = "a dependency-only prefab exposed a file";
            return false;
        }
    } else {
        std::string prefabFile;
        if (!staged->prefab->readText(
                "canonical/prefab.json", prefabFile, &error) ||
            prefabFile != R"({"kind":"fixture_prop"})") {
            outFail = "the staged prefab did not expose its embedded bytes";
            return false;
        }
    }
    const std::vector<std::string> order = store->readOrder();
    if (order.size() != 3u ||
        order[0] != "session/lamp.phlo" ||
        order[1] != "session/dependency-scene.phscene" ||
        order[2] != "session/prefab-raw.bin") {
        outFail = "the prefab graph did not read the closure in order";
        return false;
    }
    if (!session.activate() || session.active() != staged) {
        outFail = "the prefab snapshot was not activated";
        return false;
    }
    // The full prefab -> scene -> raw closure is retained either way, and the
    // required scene dependency is the decoded scene archive rather than a
    // loose copy.
    const LoadedAsset* sceneAsset = findAsset(*staged, "session/dependency-scene");
    if (!sceneAsset || sceneAsset->contentHash != scene.contentHash) {
        outFail = "the activated prefab snapshot lost its scene dependency";
        return false;
    }
    SceneArchiveStore reopenedScene;
    if (!reopenedScene.loadBytes(sceneAsset->bytes, &error) ||
        reopenedScene.sceneId() != "session/dependency-scene") {
        outFail = "the retained scene dependency is not a decodable archive";
        return false;
    }
    return true;
}

// Scene A is active while candidate B is staged; activation switches the
// active pointer without copying payloads, and retained handles stay readable.
bool caseActivationSwitchKeepsSnapshots(std::string& outFail) {
    const std::shared_ptr<SyntheticStore> store =
        std::make_shared<SyntheticStore>();
    ResolverFixture resolver;
    std::string error;
    if (!nextSceneFixture(
            store, resolver, "registry/scene-a", "session/scene-a.phscene",
            "session/scene-a", "canonical/a.json", R"({"scene":"a"})",
            outFail) ||
        !nextSceneFixture(
            store, resolver, "registry/scene-b", "session/scene-b.phscene",
            "session/scene-b", "canonical/b.json", R"({"scene":"b"})",
            outFail)) {
        return false;
    }

    PhlosionArchiveSession session(store, resolver.resolver());
    if (!session.begin("registry/scene-a", ArchiveKind::Scene, &error) ||
        !expectStaged(session, outFail) || !session.activate()) {
        outFail = "scene A did not activate: " + session.error();
        return false;
    }
    const std::shared_ptr<const ArchiveSnapshot> activeA = session.active();
    if (!activeA || activeA->scene->sceneId() != "session/scene-a") {
        outFail = "scene A is not the active snapshot";
        return false;
    }

    if (!session.begin("registry/scene-b", ArchiveKind::Scene, &error)) {
        outFail = "scene B was rejected: " + error;
        return false;
    }
    if (session.active() != activeA) {
        outFail = "begin replaced the active snapshot";
        return false;
    }
    if (!expectStaged(session, outFail)) {
        return false;
    }
    const std::shared_ptr<const ArchiveSnapshot> stagedB = session.staged();
    if (!stagedB || !stagedB->scene ||
        stagedB->scene->sceneId() != "session/scene-b" ||
        session.active() != activeA) {
        outFail = "the staged candidate replaced or hid scene A";
        return false;
    }
    std::string activeText;
    if (!activeA->scene->readText("canonical/a.json", activeText, &error) ||
        activeText != R"({"scene":"a"})") {
        outFail = "scene A became unreadable while B was staged";
        return false;
    }
    if (!session.activate() || session.active() != stagedB ||
        session.state() != ArchiveSessionState::Idle || session.staged()) {
        outFail = "activating scene B did not transfer the staged pointer";
        return false;
    }
    std::string retainedB;
    if (!session.active()->scene->readText(
            "canonical/b.json", retainedB, &error) ||
        retainedB != R"({"scene":"b"})" ||
        !activeA->scene->readText("canonical/a.json", activeText, &error) ||
        activeText != R"({"scene":"a"})") {
        outFail = "a retained snapshot changed after activation";
        return false;
    }
    return true;
}

// Every decode, kind and limit failure keeps the active snapshot and exposes no
// staged snapshot; discard() then admits a retry.
bool caseCandidateFailuresPreserveActive(std::string& outFail) {
    const std::shared_ptr<SyntheticStore> store =
        std::make_shared<SyntheticStore>();
    ResolverFixture resolver;
    std::string error;

    // Active scene A. Its closure is the root plus one raw leaf, so the byte
    // cap below admits exactly this candidate.
    const std::vector<std::uint8_t> rawPayload = bytesOf("failure-raw-leaf");
    const std::uint64_t rawHash =
        engine::assets::phrc::contentHash64(rawPayload);
    store->add("session/failure-raw.bin", rawPayload);
    resolver.map("session/failure-raw", "session/failure-raw.bin",
        AssetKind::Bytes);
    std::vector<std::uint8_t> sceneABytes;
    if (!buildSceneBytes(
            "session/failure-scene-a",
            {SceneArchiveFile{"canonical/a.json", bytesOf(R"({"scene":"a"})")}},
            {requiredDep("session/failure-raw", rawHash)},
            sceneABytes,
            outFail)) {
        return false;
    }
    ArchiveFixture sceneA;
    if (!registerPhrcAsset(
            store, resolver, "registry/failure-a",
            "session/failure-a.phscene", std::move(sceneABytes), sceneA,
            outFail)) {
        return false;
    }

    // A prefab archive used where a scene is requested.
    std::vector<std::uint8_t> prefabBytes;
    if (!engine::assets::phlosion::encodePrefabArchive(
            "session/wrong-type",
            "FixtureProp",
            "{}",
            {SceneArchiveFile{"canonical/p.json", bytesOf("{}")}},
            prefabBytes,
            &error)) {
        outFail = "wrong-type prefab fixture encode failed: " + error;
        return false;
    }
    ArchiveFixture wrongType;
    if (!registerPhrcAsset(
            store, resolver, "registry/wrong-type", "session/wrong-type.phlo",
            std::move(prefabBytes), wrongType, outFail)) {
        return false;
    }

    // A valid PHRC envelope with a PHSC magic but an unsupported schema.
    std::vector<std::uint8_t> unsupportedSchema = sceneA.bytes;
    {
        engine::assets::phrc::Document document;
        if (!engine::assets::phrc::decode(
                unsupportedSchema, document, &error)) {
            outFail = "schema fixture decode failed: " + error;
            return false;
        }
        document.schemaVersion = 2u;
        if (!engine::assets::phrc::encode(
                document, unsupportedSchema, &error)) {
            outFail = "schema fixture encode failed: " + error;
            return false;
        }
    }
    ArchiveFixture badSchema;
    if (!registerPhrcAsset(
            store, resolver, "registry/bad-schema",
            "session/bad-schema.phscene", std::move(unsupportedSchema),
            badSchema, outFail)) {
        return false;
    }

    // A valid PHRC envelope whose PHSC file record points outside the chunk
    // table, so the root decode itself has to reject it.
    std::vector<std::uint8_t> badRecordBytes;
    {
        engine::assets::phrc::Document document;
        document.magic = engine::assets::phrc::magic("PHSC");
        document.schemaVersion = 1u;
        document.manifestJson =
            "{\"schema_version\":1,\"root_type\":\"Scene\","
            "\"scene_id\":\"session\\/bad-record\",\"files\":[{\"path\":"
            "\"canonical\\/a.json\",\"chunk\":4,\"bytes\":2,"
            "\"content_hash\":1}]}";
        engine::assets::phrc::Chunk chunk;
        chunk.type = engine::assets::phrc::magic("FILE");
        chunk.bytes = bytesOf("{}");
        document.chunks.push_back(std::move(chunk));
        if (!engine::assets::phrc::encode(document, badRecordBytes, &error)) {
            outFail = "bad-record fixture encode failed: " + error;
            return false;
        }
    }
    ArchiveFixture badRecord;
    if (!registerPhrcAsset(
            store, resolver, "registry/bad-record",
            "session/bad-record.phscene", std::move(badRecordBytes),
            badRecord, outFail)) {
        return false;
    }

    // A valid archive payload that the resolver maps as an opaque byte leaf.
    store->add("session/raw-kind.phscene", sceneA.bytes);
    resolver.map(
        "registry/raw-kind", "session/raw-kind.phscene", AssetKind::Bytes);

    // A required dependency whose content is missing from the provider.
    std::vector<std::uint8_t> missingDepBytes;
    if (!buildSceneBytes(
            "session/missing-dep",
            {SceneArchiveFile{"canonical/m.json", bytesOf("{}")}},
            {requiredDep("session/absent-child", 0u)},
            missingDepBytes,
            outFail)) {
        return false;
    }
    ArchiveFixture missingDep;
    if (!registerPhrcAsset(
            store, resolver, "registry/missing-dep",
            "session/missing-dep.phscene", std::move(missingDepBytes),
            missingDep, outFail)) {
        return false;
    }
    resolver.map(
        "session/absent-child", "session/absent-child.bin", AssetKind::Bytes);

    // A required dependency whose content is present but corrupt: the stored
    // bytes cannot satisfy the required hash.
    store->add("session/corrupt-child.bin", bytesOf("failure-raw-CORRUPT"));
    resolver.map(
        "session/corrupt-child", "session/corrupt-child.bin",
        AssetKind::Bytes);
    std::vector<std::uint8_t> corruptDepBytes;
    if (!buildSceneBytes(
            "session/corrupt-dep",
            {SceneArchiveFile{"canonical/x.json", bytesOf("{}")}},
            {requiredDep("session/corrupt-child", rawHash)},
            corruptDepBytes,
            outFail)) {
        return false;
    }
    ArchiveFixture corruptDep;
    if (!registerPhrcAsset(
            store, resolver, "registry/corrupt-dep",
            "session/corrupt-dep.phscene", std::move(corruptDepBytes),
            corruptDep, outFail)) {
        return false;
    }

    // A candidate that exceeds the graph byte cap accepted for scene A.
    std::vector<std::uint8_t> oversizedBytes;
    if (!buildSceneBytes(
            "session/oversized",
            {SceneArchiveFile{
                "canonical/big.bin", std::vector<std::uint8_t>(4096u, 7u)}},
            {},
            oversizedBytes,
            outFail)) {
        return false;
    }
    ArchiveFixture oversized;
    if (!registerPhrcAsset(
            store, resolver, "registry/oversized", "session/oversized.phscene",
            std::move(oversizedBytes), oversized, outFail)) {
        return false;
    }

    AssetGraphOptions options;
    // Enough headroom for every small fixture closure, far below the oversized
    // candidate; the cap itself stays covered by the M1-B equality cases.
    options.maxEncodedBytes = sceneA.bytes.size() + rawPayload.size() + 64u;
    PhlosionArchiveSession session(store, resolver.resolver(), options);
    if (!session.begin("registry/failure-a", ArchiveKind::Scene, &error) ||
        !expectStaged(session, outFail) || !session.activate()) {
        outFail = "scene A did not stage under the exact byte cap: " +
            session.error();
        return false;
    }
    const std::shared_ptr<const ArchiveSnapshot> activeA = session.active();

    struct FailureCase {
        const char* rootAssetId;
        const char* expectText;
        const char* alternativeText;
    };
    const FailureCase failures[] = {
        {"registry/wrong-type",
            "Scene archive root 'registry/wrong-type' could not be decoded",
            "unsupported"},
        {"registry/bad-schema", "unsupported", "unsupported"},
        {"registry/bad-record", "invalid file record", "invalid file record"},
        {"registry/raw-kind", "must be a Phrc container", "Phrc container"},
        {"registry/missing-dep", "session/absent-child", "absent-child"},
        {"registry/corrupt-dep",
            "Dependency hash mismatch: 'registry/corrupt-dep' requires "
            "'session/corrupt-child'",
            "Dependency hash mismatch"},
        // The cumulative cap is enforced by the graph and, earlier, by the
        // reader's retained-resident budget; either diagnostic is correct.
        {"registry/oversized", "encoded byte limit",
            "retained resident byte budget"},
    };
    for (const FailureCase& failure : failures) {
        if (!session.begin(
                failure.rootAssetId, ArchiveKind::Scene, &error)) {
            outFail = std::string("the failure fixture '") +
                failure.rootAssetId + "' was rejected at begin: " + error;
            return false;
        }
        if (!expectFailed(
                session, failure.expectText, failure.alternativeText,
                outFail)) {
            outFail = std::string("fixture '") + failure.rootAssetId + "': " +
                outFail;
            return false;
        }
        if (session.error().find(failure.rootAssetId) == std::string::npos) {
            outFail = "the diagnostic does not name the requested root: " +
                session.error();
            return false;
        }
        if (session.active() != activeA || session.activate() ||
            session.cancel() ||
            session.state() != ArchiveSessionState::Failed) {
            outFail = std::string("fixture '") + failure.rootAssetId +
                "' mutated the active snapshot or accepted activate/cancel";
            return false;
        }
        session.discard();
        if (session.state() != ArchiveSessionState::Idle ||
            session.staged() || !session.error().empty() ||
            session.active() != activeA) {
            outFail = "discard did not reset the candidate and keep scene A";
            return false;
        }
    }

    if (!session.begin("registry/failure-a", ArchiveKind::Scene, &error) ||
        !expectStaged(session, outFail) || !session.activate() ||
        session.active()->scene->sceneId() != "session/failure-scene-a") {
        outFail = "a retry after candidate failures did not stage";
        return false;
    }
    return true;
}

bool caseContractViolations(std::string& outFail) {
    const std::shared_ptr<SyntheticStore> store =
        std::make_shared<SyntheticStore>();
    ResolverFixture resolver;
    std::string error;
    if (!nextSceneFixture(
            store, resolver, "registry/contract", "session/contract.phscene",
            "session/contract", "canonical/c.json", "{}", outFail)) {
        return false;
    }

    int constructorFailures = 0;
    try {
        PhlosionArchiveSession rejected(nullptr, resolver.resolver());
        (void)rejected;
    } catch (const std::invalid_argument&) {
        ++constructorFailures;
    }
    try {
        PhlosionArchiveSession rejected(store, AssetResolver{});
        (void)rejected;
    } catch (const std::invalid_argument&) {
        ++constructorFailures;
    }
    try {
        AssetGraphOptions options;
        options.maxAssets = 0u;
        PhlosionArchiveSession rejected(store, resolver.resolver(), options);
        (void)rejected;
    } catch (const std::invalid_argument&) {
        ++constructorFailures;
    }
    if (constructorFailures != 3) {
        outFail = "the session did not forward every constructor rejection";
        return false;
    }

    PhlosionArchiveSession session(store, resolver.resolver());
    if (session.cancel() || session.activate() ||
        session.state() != ArchiveSessionState::Idle) {
        outFail = "cancel/activate succeeded without a candidate";
        return false;
    }
    error.clear();
    if (session.begin("", ArchiveKind::Scene, &error) || error.empty() ||
        session.state() != ArchiveSessionState::Idle) {
        outFail = "an empty root was admitted";
        return false;
    }
    error.clear();
    if (session.begin(
            "registry/contract", static_cast<ArchiveKind>(99), &error) ||
        error.empty() || session.state() != ArchiveSessionState::Idle) {
        outFail = "an unknown archive kind was admitted";
        return false;
    }
    if (!session.begin("registry/contract", ArchiveKind::Scene, &error)) {
        outFail = "the valid root was rejected: " + error;
        return false;
    }
    if (session.activate() ||
        session.state() != ArchiveSessionState::Loading) {
        outFail = "activate succeeded while Loading";
        return false;
    }
    if (!expectStaged(session, outFail)) {
        return false;
    }
    const std::shared_ptr<const ArchiveSnapshot> staged = session.staged();
    // A rejected begin leaves the candidate, error and active snapshot intact.
    error.clear();
    if (session.begin("registry/contract", ArchiveKind::Scene, &error) ||
        error.empty() || session.state() != ArchiveSessionState::Staged ||
        session.staged() != staged || session.active()) {
        outFail = "begin was admitted outside Idle or mutated the candidate";
        return false;
    }
    if (session.cancel() || session.state() != ArchiveSessionState::Staged ||
        session.staged() != staged) {
        outFail = "cancel succeeded outside Loading";
        return false;
    }
    if (!session.activate() || session.active() != staged ||
        session.staged() || session.state() != ArchiveSessionState::Idle) {
        outFail = "activate did not publish the staged snapshot";
        return false;
    }
    if (session.activate() || session.cancel() ||
        session.active() != staged) {
        outFail = "activate/cancel mutated an idle session";
        return false;
    }
    // A graph failure with no mapping: begin is refused until discard(), the
    // failure diagnostic is preserved, and the active snapshot survives.
    if (!session.begin("registry/unmapped", ArchiveKind::Scene, &error) ||
        !expectFailed(session, "registry/unmapped", "no mapping", outFail)) {
        return false;
    }
    const std::string failedError = session.error();
    error.clear();
    if (session.begin("registry/contract", ArchiveKind::Scene, &error) ||
        error.empty() || session.error() != failedError ||
        session.state() != ArchiveSessionState::Failed ||
        session.active() != staged || session.activate()) {
        outFail = "a failed candidate was mutated by a rejected begin";
        return false;
    }
    session.discard();
    if (session.state() != ArchiveSessionState::Idle || !session.error().empty() ||
        session.active() != staged) {
        outFail = "discard did not clear the failed candidate";
        return false;
    }
    // Cancel from Loading: the active snapshot survives and a cancelled
    // candidate still has to be discarded before the next begin.
    if (!session.begin("registry/contract", ArchiveKind::Scene, &error) ||
        !session.cancel() ||
        session.state() != ArchiveSessionState::Cancelled ||
        session.error().find("registry/contract") == std::string::npos ||
        session.staged() || session.active() != staged || session.cancel()) {
        outFail = "cancel did not produce the documented candidate state";
        return false;
    }
    error.clear();
    if (session.begin("registry/contract", ArchiveKind::Scene, &error) ||
        error.empty()) {
        outFail = "begin was admitted from Cancelled";
        return false;
    }
    session.discard();
    if (session.state() != ArchiveSessionState::Idle || !session.error().empty() ||
        session.active() != staged) {
        outFail = "discard did not clear the cancelled candidate";
        return false;
    }
    return true;
}

// Gate a candidate read while scene A stays readable, cancel or discard it,
// admit a replacement, let the released read finish, and prove the released
// completion cannot stage or activate anything.
bool caseGatedCancelAndReuse(const bool useCancel, std::string& outFail) {
    const std::shared_ptr<SyntheticStore> store =
        std::make_shared<SyntheticStore>();
    ResolverFixture resolver;
    std::string error;
    if (!nextSceneFixture(
            store, resolver, "registry/gate-active",
            "session/gate-active.phscene", "session/gate-active",
            "canonical/a.json", R"({"scene":"a"})", outFail) ||
        !nextSceneFixture(
            store, resolver, "registry/gate-blocked",
            "session/gate-blocked.phscene", "session/gate-blocked",
            "canonical/b.json", R"({"scene":"b"})", outFail) ||
        !nextSceneFixture(
            store, resolver, "registry/gate-replacement",
            "session/gate-replacement.phscene", "session/gate-replacement",
            "canonical/c.json", R"({"scene":"c"})", outFail)) {
        return false;
    }

    store->gate("session/gate-blocked.phscene");
    {
        PhlosionArchiveSession session(store, resolver.resolver());
        GateCleanup cleanup(store);
        if (!session.begin("registry/gate-active", ArchiveKind::Scene, &error) ||
            !expectStaged(session, outFail) || !session.activate()) {
            outFail = "the active scene did not stage: " + session.error();
            return false;
        }
        const std::shared_ptr<const ArchiveSnapshot> active = session.active();

        if (!session.begin(
                "registry/gate-blocked", ArchiveKind::Scene, &error)) {
            outFail = "the gated candidate was rejected: " + error;
            return false;
        }
        // begin schedules nothing; the first pump starts the provider read that
        // the gate holds open.
        session.pump();
        if (!store->waitForReadStarted(
                "session/gate-blocked.phscene", outFail)) {
            return false;
        }
        std::string activeText;
        if (!active->scene->readText(
                "canonical/a.json", activeText, &error) ||
            activeText != R"({"scene":"a"})") {
            outFail = "the active scene was unreadable during the gated read";
            return false;
        }
        if (useCancel) {
            if (!session.cancel() ||
                session.state() != ArchiveSessionState::Cancelled ||
                session.active() != active) {
                outFail = "cancel did not release the gated candidate";
                return false;
            }
            // A cancelled candidate still has to be discarded before the next
            // begin, and that clear must not wait for the released read.
            session.discard();
            if (session.state() != ArchiveSessionState::Idle ||
                session.active() != active || !session.error().empty()) {
                outFail = "discard did not clear the cancelled candidate";
                return false;
            }
        } else {
            session.discard();
            if (session.state() != ArchiveSessionState::Idle ||
                session.active() != active || !session.error().empty()) {
                outFail = "discard did not reset the gated candidate";
                return false;
            }
        }

        // The replacement is admitted while the released read is still blocked
        // at the provider.
        if (!session.begin(
                "registry/gate-replacement", ArchiveKind::Scene, &error)) {
            outFail =
                "the replacement was rejected while a read was running: " +
                error;
            return false;
        }
        if (store->readCount("session/gate-replacement.phscene") != 0u) {
            outFail = "the replacement was read before its pump";
            return false;
        }
        store->openGate();
        if (!expectStaged(session, outFail)) {
            return false;
        }
        const std::shared_ptr<const ArchiveSnapshot> staged = session.staged();
        if (!staged ||
            staged->graph.rootAssetId != "registry/gate-replacement" ||
            staged->scene->sceneId() != "session/gate-replacement" ||
            session.active() != active) {
            outFail = "the released read leaked into the replacement candidate";
            return false;
        }
        if (store->readCount("session/gate-blocked.phscene") != 1u) {
            outFail = "the released read did not still reach the provider";
            return false;
        }
        if (!session.activate() || session.active() != staged) {
            outFail = "the replacement did not activate";
            return false;
        }
        if (!expectNoGateTimeout(store, outFail)) {
            return false;
        }
    }
    return true;
}

bool caseUnloadAndSnapshotLifetime(std::string& outFail) {
    const std::shared_ptr<SyntheticStore> store =
        std::make_shared<SyntheticStore>();
    ResolverFixture resolver;
    std::string error;
    if (!nextSceneFixture(
            store, resolver, "registry/lifetime", "session/lifetime.phscene",
            "session/lifetime", "canonical/l.json", R"({"l":1})", outFail) ||
        !nextSceneFixture(
            store, resolver, "registry/lifetime-other",
            "session/lifetime-other.phscene", "session/lifetime-other",
            "canonical/o.json", R"({"o":1})", outFail)) {
        return false;
    }

    std::shared_ptr<const ArchiveSnapshot> retained;
    std::weak_ptr<const ArchiveSnapshot> retainedWatch;
    {
        PhlosionArchiveSession session(store, resolver.resolver());
        if (!session.begin("registry/lifetime", ArchiveKind::Scene, &error) ||
            !expectStaged(session, outFail) || !session.activate()) {
            outFail = "the lifetime scene did not activate: " + session.error();
            return false;
        }
        retained = session.active();
        retainedWatch = retained;
        // unload from Idle drops the session reference and keeps the caller's
        // handle readable.
        session.unload();
        if (session.state() != ArchiveSessionState::Idle ||
            session.active() || session.staged() || !session.error().empty()) {
            outFail = "unload left candidate or active state behind";
            return false;
        }
        std::string text;
        if (!retained->scene->readText("canonical/l.json", text, &error) ||
            text != R"({"l":1})") {
            outFail = "a retained snapshot was unusable after unload";
            return false;
        }
        // unload from Staged drops the staged candidate.
        if (!session.begin("registry/lifetime-other", ArchiveKind::Scene,
                &error) ||
            !expectStaged(session, outFail)) {
            return false;
        }
        const std::shared_ptr<const ArchiveSnapshot> staged = session.staged();
        session.unload();
        if (session.staged() || session.active() ||
            session.state() != ArchiveSessionState::Idle) {
            outFail = "unload did not drop the session's staged reference";
            return false;
        }
        if (!staged->scene->readText("canonical/o.json", text, &error) ||
            text != R"({"o":1})") {
            outFail = "a discarded staged snapshot was unusable";
            return false;
        }
    }
    // Session destroyed: the caller's handle still owns the data, and the
    // session's own reference is gone.
    std::string text;
    if (retainedWatch.expired() ||
        !retained->scene->readText("canonical/l.json", text, &error) ||
        text != R"({"l":1})") {
        outFail = "a snapshot did not outlive its session";
        return false;
    }
    retained.reset();
    if (!retainedWatch.expired()) {
        outFail = "dropping the caller handle did not release the snapshot";
        return false;
    }

    // Provider lifetime: the session's reader owns the provider, a snapshot
    // does not. Drop every external strong reference while the session lives.
    std::weak_ptr<SyntheticStore> providerWatch;
    std::shared_ptr<const ArchiveSnapshot> providerSnapshot;
    {
        std::shared_ptr<SyntheticStore> provider =
            std::make_shared<SyntheticStore>();
        if (!nextSceneFixture(
                provider, resolver, "registry/provider",
                "session/provider.phscene", "session/provider",
                "canonical/p.json", R"({"p":1})", outFail)) {
            return false;
        }
        PhlosionArchiveSession session(provider, resolver.resolver());
        if (!session.begin("registry/provider", ArchiveKind::Scene, &error) ||
            !expectStaged(session, outFail) || !session.activate()) {
            outFail = "the provider-lifetime scene did not activate: " +
                session.error();
            return false;
        }
        providerSnapshot = session.active();
        providerWatch = provider;
        provider.reset();
        // Declared after the session so it is released first; the session's
        // reader must still own the provider here.
        const std::shared_ptr<SyntheticStore> stillAlive = providerWatch.lock();
        if (!stillAlive) {
            outFail = "the session did not keep its provider alive";
            return false;
        }
        std::string providerText;
        if (!providerSnapshot->scene->readText(
                "canonical/p.json", providerText, &error) ||
            providerText != R"({"p":1})") {
            outFail = "the active snapshot was unreadable without an external "
                "provider reference";
            return false;
        }
    }
    if (!providerWatch.expired()) {
        outFail = "the provider outlived the session reader";
        return false;
    }
    std::string retainedText;
    if (!providerSnapshot->scene->readText(
            "canonical/p.json", retainedText, &error) ||
        retainedText != R"({"p":1})") {
        outFail = "a retained snapshot needed the destroyed provider";
        return false;
    }
    providerSnapshot.reset();

    // unload during candidate work: the released read cannot publish, and the
    // session is reusable afterwards.
    store->gate("session/lifetime-other.phscene");
    {
        // An earlier candidate already read this path, so only a captured
        // count plus one proves that a fresh gated read started.
        const std::size_t alreadyRead =
            store->readCount("session/lifetime-other.phscene");
        const std::size_t alreadyReadReplacement =
            store->readCount("session/lifetime.phscene");
        PhlosionArchiveSession session(store, resolver.resolver());
        GateCleanup cleanup(store);
        if (!session.begin(
                "registry/lifetime-other", ArchiveKind::Scene, &error)) {
            outFail = "the gated lifetime candidate was rejected: " + error;
            return false;
        }
        session.pump();
        if (!store->waitForReadCountAtLeast(
                "session/lifetime-other.phscene", alreadyRead + 1u,
                outFail)) {
            return false;
        }
        session.unload();
        if (session.state() != ArchiveSessionState::Idle ||
            session.active() || session.staged() || !session.error().empty()) {
            outFail = "unload during candidate work left state behind";
            return false;
        }
        if (!session.begin("registry/lifetime", ArchiveKind::Scene, &error)) {
            outFail = "the session was not reusable after unload: " + error;
            return false;
        }
        // The released read is still blocked at the provider, so the
        // replacement cannot have reached it yet. This path was read by an
        // earlier candidate, so compare against its captured count.
        session.pump();
        if (store->readCount("session/lifetime.phscene") !=
            alreadyReadReplacement) {
            outFail = "the replacement was read while the released read was "
                "still blocked";
            return false;
        }
        store->openGate();
        if (!expectStaged(session, outFail)) {
            return false;
        }
        if (session.staged()->graph.rootAssetId != "registry/lifetime") {
            outFail = "a released read published into the new candidate";
            return false;
        }
        // Ordering proof: the released read reached the provider before the
        // replacement read did.
        const std::vector<std::string> order = store->readOrder();
        std::size_t releasedIndex = order.size();
        std::size_t replacementIndex = order.size();
        for (std::size_t index = 0u; index < order.size(); ++index) {
            if (order[index] == "session/lifetime-other.phscene") {
                releasedIndex = index;
            } else if (order[index] == "session/lifetime.phscene") {
                replacementIndex = index;
            }
        }
        if (releasedIndex == order.size() ||
            replacementIndex == order.size() ||
            releasedIndex > replacementIndex) {
            outFail = "the released active read did not finish before the "
                "replacement read";
            return false;
        }
        if (!expectNoGateTimeout(store, outFail)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool test_phlosion_archive_session_contract(std::string& outFail) {
    if (!caseSceneStagingAndActivation(outFail)) {
        return false;
    }
    if (!casePrefabGraphStaging(false, outFail)) {
        return false;
    }
    if (!casePrefabGraphStaging(true, outFail)) {
        return false;
    }
    if (!caseActivationSwitchKeepsSnapshots(outFail)) {
        return false;
    }
    if (!caseCandidateFailuresPreserveActive(outFail)) {
        return false;
    }
    if (!caseContractViolations(outFail)) {
        return false;
    }
    if (!caseGatedCancelAndReuse(true, outFail)) {
        return false;
    }
    if (!caseGatedCancelAndReuse(false, outFail)) {
        return false;
    }
    if (!caseUnloadAndSnapshotLifetime(outFail)) {
        return false;
    }
    return true;
}
