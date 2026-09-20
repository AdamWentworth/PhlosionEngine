#include "engine/assets/phlosion/PhlosionAssetGraphLoader.h"

#include "engine/assets/phlosion/PhlosionResourceContainer.h"
#include "engine/assets/phlosion/PhlosionSceneArchive.h"

#include <nlohmann/json.hpp>

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

using engine::assets::phlosion::AssetGraphOptions;
using engine::assets::phlosion::AssetGraphResult;
using engine::assets::phlosion::AssetGraphState;
using engine::assets::phlosion::AssetKind;
using engine::assets::phlosion::AssetLocation;
using engine::assets::phlosion::AssetResolver;
using engine::assets::phlosion::LoadedAsset;
using engine::assets::phlosion::PhlosionAssetGraphLoader;
using engine::assets::phlosion::PrefabArchiveStore;
using engine::assets::phlosion::SceneArchiveFile;
using engine::assets::phlosion::SceneArchiveStore;
using engine::assets::phrc::Dependency;

constexpr std::chrono::milliseconds kWaitTimeout{15000};
constexpr std::chrono::milliseconds kPollInterval{1};

const char* stateName(AssetGraphState state) {
    switch (state) {
    case AssetGraphState::Idle: return "Idle";
    case AssetGraphState::Loading: return "Loading";
    case AssetGraphState::Ready: return "Ready";
    case AssetGraphState::Failed: return "Failed";
    case AssetGraphState::Cancelled: return "Cancelled";
    }
    return "invalid";
}

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

Dependency optionalDep(
    std::string assetId,
    std::uint64_t expectedHash = 0u,
    std::uint32_t flags = 0u) {
    Dependency dependency;
    dependency.assetId = std::move(assetId);
    dependency.expectedContentHash = expectedHash;
    dependency.flags = flags;
    return dependency;
}

// Immutable in-memory provider. Reads can be failed, made to throw, or paused
// on a gate so a test can observe an in-flight read deterministically.
class SyntheticStore final : public engine::IAssetStore {
public:
    void add(std::string virtualPath, std::vector<std::uint8_t> bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        assets_.insert_or_assign(std::move(virtualPath), std::move(bytes));
    }

    void failFor(std::string virtualPath) {
        std::lock_guard<std::mutex> lock(mutex_);
        failPaths_.insert(std::move(virtualPath));
    }

    void throwFor(std::string virtualPath) {
        std::lock_guard<std::mutex> lock(mutex_);
        throwPaths_.insert(std::move(virtualPath));
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

    bool waitForReadFinished(
        const std::string& virtualPath,
        std::string& outFail) const {
        return waitUntil(
            [this, &virtualPath] {
                const auto found = finishedCounts_.find(virtualPath);
                return found != finishedCounts_.end() && found->second > 0u;
            },
            "read completion for '" + virtualPath + "'",
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

    bool sawReadFinished(const std::string& virtualPath) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = finishedCounts_.find(virtualPath);
        return found != finishedCounts_.end() && found->second > 0u;
    }

    // Gate waits that hit their bound. A non-empty result means a read
    // continued without the gate opening, so any ordering claim built on it is
    // void and the test must fail.
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
            reinterpret_cast<const char*>(bytes.data()),
            bytes.size());
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
            // Bounded: a timed-out gate is recorded so the caller can assert
            // that the paused-read ordering was real.
            if (!changed_.wait_for(
                    lock, kWaitTimeout, [this] { return gateOpen_; })) {
                gateTimeouts_.push_back(virtualPath);
            }
        }
        if (throwPaths_.contains(virtualPath)) {
            ++finishedCounts_[virtualPath];
            changed_.notify_all();
            throw std::runtime_error(
                "synthetic provider exception for '" + virtualPath + "'");
        }
        const auto found = assets_.find(virtualPath);
        if (found == assets_.end() || failPaths_.contains(virtualPath)) {
            if (outError) {
                *outError =
                    "synthetic provider could not read '" + virtualPath + "'";
            }
            ++finishedCounts_[virtualPath];
            changed_.notify_all();
            return false;
        }
        outBytes = found->second;
        ++finishedCounts_[virtualPath];
        changed_.notify_all();
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
        if (predicate()) {
            return true;
        }
        while (std::chrono::steady_clock::now() < deadline) {
            changed_.wait_for(lock, kPollInterval);
            if (predicate()) {
                return true;
            }
        }
        outFail = "timed out waiting for " + description;
        return false;
    }

    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    mutable std::map<std::string, std::vector<std::uint8_t>> assets_;
    mutable std::set<std::string> failPaths_;
    mutable std::set<std::string> throwPaths_;
    mutable std::set<std::string> gated_;
    mutable bool gateOpen_ = false;
    mutable std::map<std::string, std::size_t> readCounts_;
    mutable std::map<std::string, std::size_t> finishedCounts_;
    mutable std::vector<std::string> readOrder_;
    mutable std::vector<std::string> gateTimeouts_;
};

// Opens every gate on destruction. Declare it after a loader so the gate is
// always open before the loader's destructor joins a paused read.
class GateCleanup {
public:
    explicit GateCleanup(const std::shared_ptr<SyntheticStore>& store)
        : store_(store) {}

    ~GateCleanup() {
        store_->openGate();
    }

    GateCleanup(const GateCleanup&) = delete;
    GateCleanup& operator=(const GateCleanup&) = delete;

private:
    std::shared_ptr<SyntheticStore> store_;
};

// Records resolver calls and can inject every resolver failure shape.
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

    void setRaw(std::string assetId, AssetLocation location) {
        locations_.insert_or_assign(std::move(assetId), std::move(location));
    }

    void throwFor(std::string assetId) {
        throwing_.insert(std::move(assetId));
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
            if (throwing_.contains(assetId)) {
                throw std::runtime_error(
                    "synthetic resolver exception for '" + assetId + "'");
            }
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
    std::set<std::string> throwing_;
    mutable std::map<std::string, std::size_t> resolveCounts_;
};

struct PhrcFixture {
    std::vector<std::uint8_t> bytes;
    std::uint64_t contentHash = 0u;
};

std::uint64_t phrcHash(const std::vector<std::uint8_t>& bytes) {
    engine::assets::phrc::Document document;
    if (!engine::assets::phrc::decode(bytes, document, nullptr)) {
        return 0u;
    }
    return document.contentHash;
}

bool addPhrcAsset(
    const std::shared_ptr<SyntheticStore>& store,
    ResolverFixture& resolver,
    const std::string& assetId,
    const std::string& virtualPath,
    PhrcFixture& outFixture,
    std::string& outFail,
    std::vector<Dependency> dependencies = {},
    std::string manifestJson = "{}") {
    engine::assets::phrc::Document document;
    document.manifestJson = std::move(manifestJson);
    document.dependencies = std::move(dependencies);
    std::string error;
    if (!engine::assets::phrc::encode(document, outFixture.bytes, &error)) {
        outFail = "synthetic PHRC encode failed: " + error;
        return false;
    }
    engine::assets::phrc::Document decoded;
    if (!engine::assets::phrc::decode(outFixture.bytes, decoded, &error)) {
        outFail = "synthetic PHRC did not decode: " + error;
        return false;
    }
    outFixture.contentHash = decoded.contentHash;
    store->add(virtualPath, outFixture.bytes);
    resolver.map(assetId, virtualPath, AssetKind::Phrc);
    return true;
}

bool addBytesAsset(
    const std::shared_ptr<SyntheticStore>& store,
    ResolverFixture& resolver,
    const std::string& assetId,
    const std::string& virtualPath,
    std::vector<std::uint8_t> payload,
    std::uint64_t& outHash) {
    outHash = engine::assets::phrc::contentHash64(payload);
    store->add(virtualPath, std::move(payload));
    resolver.map(assetId, virtualPath, AssetKind::Bytes);
    return true;
}

const LoadedAsset* findAsset(
    const AssetGraphResult& result,
    const std::string& assetId) {
    const auto found = result.assets.find(assetId);
    return found == result.assets.end() ? nullptr : &found->second;
}

// A gated ordering claim is only valid if no gate wait timed out. The timeout
// record cannot be discarded by cancel, reset or the cleanup guard because it
// lives in the provider and is asserted after the session finished.
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

bool pumpToTerminal(
    PhlosionAssetGraphLoader& loader,
    AssetGraphState& outState,
    std::string& outFail) {
    const auto deadline = std::chrono::steady_clock::now() + kWaitTimeout;
    for (;;) {
        loader.pump();
        const AssetGraphState state = loader.state();
        if (state != AssetGraphState::Loading) {
            outState = state;
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            outFail = "timed out pumping the asset graph while Loading";
            return false;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
}

bool expectReady(
    PhlosionAssetGraphLoader& loader,
    AssetGraphResult& outResult,
    std::string& outFail,
    const std::size_t* expectedLoadedBytes = nullptr) {
    AssetGraphState state = AssetGraphState::Idle;
    if (!pumpToTerminal(loader, state, outFail)) {
        return false;
    }
    if (state != AssetGraphState::Ready) {
        AssetGraphResult failed;
        std::string detail = std::string("the graph is ") + stateName(state);
        if (loader.take(failed)) {
            detail += ": " + failed.error;
        }
        outFail = detail;
        return false;
    }
    if (expectedLoadedBytes && loader.loadedBytes() != *expectedLoadedBytes) {
        outFail = "loadedBytes is " + std::to_string(loader.loadedBytes()) +
            " but " + std::to_string(*expectedLoadedBytes) + " was expected";
        return false;
    }
    if (!loader.take(outResult)) {
        outFail = "taking a Ready graph result failed";
        return false;
    }
    if (outResult.state != AssetGraphState::Ready) {
        outFail = "the taken graph result is not Ready";
        return false;
    }
    return true;
}

bool expectFailed(
    PhlosionAssetGraphLoader& loader,
    const std::string& expectedRoot,
    std::string_view needle,
    AssetGraphResult& outResult,
    std::string& outFail) {
    AssetGraphState state = AssetGraphState::Idle;
    if (!pumpToTerminal(loader, state, outFail)) {
        return false;
    }
    if (state != AssetGraphState::Failed) {
        outFail = std::string("expected a failed graph but the state is ") +
            stateName(state);
        return false;
    }
    if (loader.loadedBytes() != 0u) {
        outFail = "a failed graph still accounts for loaded bytes";
        return false;
    }
    AssetGraphResult result;
    if (!loader.take(result)) {
        outFail = "taking a failed graph result failed";
        return false;
    }
    if (result.state != AssetGraphState::Failed) {
        outFail = "the taken failed result is not Failed";
        return false;
    }
    if (result.rootAssetId != expectedRoot) {
        outFail = "the failed result lost the root asset ID";
        return false;
    }
    if (!result.assets.empty()) {
        outFail = "a failed graph published partial assets";
        return false;
    }
    if (!result.skippedOptionalDependencies.empty()) {
        outFail = "a failed graph published skipped optional edges";
        return false;
    }
    if (result.error.empty()) {
        outFail = "a failed graph has no diagnostic";
        return false;
    }
    const std::string needleText(needle);
    if (result.error.find(needleText) == std::string::npos) {
        outFail = "diagnostic \"" + result.error +
            "\" does not mention '" + needleText + "'";
        return false;
    }
    outResult = std::move(result);
    return true;
}

bool caseSuccessShapes(std::string& outFail) {
    const std::shared_ptr<SyntheticStore> store =
        std::make_shared<SyntheticStore>();
    ResolverFixture resolver;

    PhrcFixture phrcRoot;
    if (!addPhrcAsset(
            store, resolver, "demo/root-phrc", "roots/root.phrc", phrcRoot,
            outFail)) {
        return false;
    }
    const std::vector<std::uint8_t> rawPayload{9u, 8u, 7u, 0u, 255u};
    std::uint64_t rawHash = 0u;
    addBytesAsset(
        store, resolver, "demo/root-bytes", "roots/raw.bin", rawPayload,
        rawHash);
    std::uint64_t emptyHash = 0u;
    addBytesAsset(
        store, resolver, "demo/root-empty", "roots/empty.bin", {},
        emptyHash);

    {
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root-phrc")) {
            outFail = "a PHRC root was rejected";
            return false;
        }
        if (loader.state() != AssetGraphState::Loading) {
            outFail = "start did not enter Loading";
            return false;
        }
        AssetGraphResult result;
        if (!expectReady(loader, result, outFail)) {
            return false;
        }
        if (result.rootAssetId != "demo/root-phrc" ||
            result.assets.size() != 1u ||
            !result.error.empty()) {
            outFail = "the PHRC root result shape is wrong";
            return false;
        }
        const LoadedAsset* asset = findAsset(result, "demo/root-phrc");
        if (!asset ||
            asset->kind != AssetKind::Phrc ||
            asset->virtualPath != "roots/root.phrc" ||
            asset->bytes != phrcRoot.bytes ||
            asset->contentHash != phrcRoot.contentHash) {
            outFail = "the PHRC root asset does not match the fixture";
            return false;
        }
        if (loader.state() != AssetGraphState::Idle ||
            loader.loadedBytes() != 0u) {
            outFail = "take did not return the session to Idle";
            return false;
        }
        AssetGraphResult sentinel;
        sentinel.state = AssetGraphState::Ready;
        sentinel.rootAssetId = "sentinel";
        if (loader.take(sentinel) ||
            sentinel.state != AssetGraphState::Ready ||
            sentinel.rootAssetId != "sentinel") {
            outFail = "take from Idle did not leave the output unchanged";
            return false;
        }
    }
    if (resolver.resolveCount("demo/root-phrc") != 1u) {
        outFail = "the root was not resolved exactly once";
        return false;
    }

    {
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root-bytes")) {
            outFail = "a Bytes root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectReady(loader, result, outFail)) {
            return false;
        }
        const LoadedAsset* asset = findAsset(result, "demo/root-bytes");
        if (result.assets.size() != 1u ||
            !asset ||
            asset->kind != AssetKind::Bytes ||
            asset->bytes != rawPayload ||
            asset->contentHash != rawHash) {
            outFail = "the opaque Bytes root does not match the fixture";
            return false;
        }
    }

    {
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root-empty")) {
            outFail = "an empty Bytes root was rejected";
            return false;
        }
        AssetGraphState state = AssetGraphState::Idle;
        if (!pumpToTerminal(loader, state, outFail)) {
            return false;
        }
        if (state != AssetGraphState::Ready || loader.loadedBytes() != 0u) {
            outFail =
                "an empty payload was not accepted with zero loaded bytes";
            return false;
        }
        AssetGraphResult result;
        if (!loader.take(result) || result.state != AssetGraphState::Ready) {
            outFail = "the empty Bytes root did not take a Ready result";
            return false;
        }
        const LoadedAsset* asset = findAsset(result, "demo/root-empty");
        if (result.assets.size() != 1u ||
            !asset ||
            !asset->bytes.empty() ||
            asset->contentHash != emptyHash) {
            outFail = "the empty Bytes root does not match the fixture";
            return false;
        }
    }
    return true;
}

bool caseCancellationAndReset(std::string& outFail) {
    // Cancel an in-flight read and reuse the loader; the released read must
    // finish at the provider and still never reach the new session.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        PhrcFixture first;
        PhrcFixture second;
        if (!addPhrcAsset(
                store, resolver, "gate/root", "gate/root.phrc", first,
                outFail) ||
            !addPhrcAsset(
                store, resolver, "gate/next", "gate/next.phrc", second,
                outFail)) {
            return false;
        }
        store->gate("gate/root.phrc");

        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        GateCleanup cleanup(store);
        if (!loader.start("gate/root")) {
            outFail = "the gated root was rejected";
            return false;
        }
        loader.pump();
        if (!store->waitForReadStarted("gate/root.phrc", outFail)) {
            return false;
        }
        if (loader.state() != AssetGraphState::Loading ||
            loader.loadedBytes() != 0u) {
            outFail = "the session was not Loading during the gated read";
            return false;
        }
        if (!loader.cancel()) {
            outFail = "cancel during an active read failed";
            return false;
        }
        if (loader.state() != AssetGraphState::Cancelled ||
            loader.loadedBytes() != 0u) {
            outFail = "cancel did not immediately cancel the session";
            return false;
        }
        loader.pump();
        if (loader.state() != AssetGraphState::Cancelled) {
            outFail = "pump changed a Cancelled session";
            return false;
        }
        if (loader.cancel()) {
            outFail = "cancel succeeded twice";
            return false;
        }
        AssetGraphResult cancelled;
        if (!loader.take(cancelled) ||
            cancelled.state != AssetGraphState::Cancelled ||
            cancelled.rootAssetId != "gate/root" ||
            !cancelled.assets.empty() ||
            !cancelled.skippedOptionalDependencies.empty() ||
            cancelled.error.empty()) {
            outFail = "the cancelled result shape is wrong";
            return false;
        }
        if (loader.state() != AssetGraphState::Idle ||
            loader.loadedBytes() != 0u) {
            outFail = "take did not return the cancelled session to Idle";
            return false;
        }

        if (!loader.start("gate/next")) {
            outFail = "start was rejected after cancel";
            return false;
        }
        loader.pump();
        if (loader.state() != AssetGraphState::Loading) {
            outFail = "the reused session published without a completed read";
            return false;
        }
        store->openGate();
        if (!store->waitForReadFinished("gate/root.phrc", outFail)) {
            return false;
        }
        AssetGraphResult next;
        if (!expectReady(loader, next, outFail)) {
            return false;
        }
        if (next.assets.size() != 1u ||
            !findAsset(next, "gate/next") ||
            findAsset(next, "gate/root")) {
            outFail = "the released read contaminated the reused session";
            return false;
        }
        const std::vector<std::string> order = store->readOrder();
        if (order.size() != 2u ||
            order[0] != "gate/root.phrc" ||
            order[1] != "gate/next.phrc") {
            outFail = "the provider read order is not deterministic";
            return false;
        }
        if (!expectNoGateTimeout(store, outFail)) {
            return false;
        }
    }

    // Reset while a read is in flight: the released read still finishes at the
    // single serialized provider, and the new session admitted before that
    // completion only completes afterwards without ever seeing it.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        PhrcFixture first;
        PhrcFixture second;
        if (!addPhrcAsset(
                store, resolver, "reset/root", "reset/root.phrc", first,
                outFail) ||
            !addPhrcAsset(
                store, resolver, "reset/next", "reset/next.phrc", second,
                outFail)) {
            return false;
        }
        store->gate("reset/root.phrc");

        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        GateCleanup cleanup(store);
        if (!loader.start("reset/root")) {
            outFail = "the gated reset root was rejected";
            return false;
        }
        loader.pump();
        if (!store->waitForReadStarted("reset/root.phrc", outFail)) {
            return false;
        }
        loader.reset();
        if (loader.state() != AssetGraphState::Idle ||
            loader.loadedBytes() != 0u) {
            outFail = "reset did not return the session to Idle";
            return false;
        }
        AssetGraphResult afterReset;
        afterReset.rootAssetId = "sentinel";
        if (loader.take(afterReset) ||
            afterReset.rootAssetId != "sentinel") {
            outFail = "take succeeded after reset";
            return false;
        }
        if (!loader.start("reset/next")) {
            outFail = "start was rejected after reset";
            return false;
        }
        if (store->sawReadFinished("reset/root.phrc")) {
            outFail = "the released read already finished before the new start";
            return false;
        }
        loader.pump();
        if (loader.state() != AssetGraphState::Loading) {
            outFail = "the reused session published without a completed read";
            return false;
        }
        store->openGate();
        AssetGraphResult result;
        if (!expectReady(loader, result, outFail)) {
            return false;
        }
        if (result.assets.size() != 1u ||
            !findAsset(result, "reset/next") ||
            findAsset(result, "reset/root")) {
            outFail = "the reset read contaminated the reused session";
            return false;
        }
        if (!store->sawReadFinished("reset/root.phrc")) {
            outFail = "the released read never finished at the provider";
            return false;
        }
        if (!expectNoGateTimeout(store, outFail)) {
            return false;
        }
    }

    // Reset from Ready drops the payload and permits a later session.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "ready/root", "ready/root.phrc", root,
                outFail)) {
            return false;
        }
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("ready/root")) {
            outFail = "the ready-reset root was rejected";
            return false;
        }
        AssetGraphState state = AssetGraphState::Idle;
        if (!pumpToTerminal(loader, state, outFail) ||
            state != AssetGraphState::Ready) {
            if (outFail.empty()) {
                outFail = "the ready-reset session did not reach Ready";
            }
            return false;
        }
        if (loader.loadedBytes() != root.bytes.size()) {
            outFail = "loadedBytes did not match the retained payload";
            return false;
        }
        loader.reset();
        if (loader.state() != AssetGraphState::Idle ||
            loader.loadedBytes() != 0u) {
            outFail = "reset did not drop the Ready payload";
            return false;
        }
        AssetGraphResult result;
        if (!loader.start("ready/root") ||
            !expectReady(loader, result, outFail) ||
            result.assets.size() != 1u) {
            if (outFail.empty()) {
                outFail = "the session was not reusable after reset";
            }
            return false;
        }
    }
    return true;
}

bool caseConsumerGraph(std::string& outFail) {
    const std::string sceneJson =
        R"({"schema_version":1,"kind":"demo_scene"})";
    const std::vector<std::uint8_t> sceneGeometry{0u, 1u, 2u, 255u};
    const std::string prefabMetadata = R"({"size":[1,2,3]})";
    const std::vector<std::uint8_t> rawPayload{10u, 20u, 30u};

    const std::shared_ptr<SyntheticStore> store =
        std::make_shared<SyntheticStore>();
    ResolverFixture resolver;
    std::uint64_t rawHash = 0u;
    addBytesAsset(
        store, resolver, "demo/raw-payload", "consumer/raw.bin", rawPayload,
        rawHash);

    // A real scene archive with a required dependency on the raw payload.
    std::vector<SceneArchiveFile> sceneFiles;
    sceneFiles.push_back(
        SceneArchiveFile{"canonical/scene.json", bytesOf(sceneJson)});
    sceneFiles.push_back(
        SceneArchiveFile{"canonical/geometry.bin", sceneGeometry});
    std::vector<std::uint8_t> sceneArchive;
    std::string error;
    if (!engine::assets::phlosion::encodeSceneArchive(
            "demo/board-scene", std::move(sceneFiles), sceneArchive,
            &error)) {
        outFail = "synthetic scene archive encode failed: " + error;
        return false;
    }
    engine::assets::phrc::Document sceneDocument;
    if (!engine::assets::phrc::decode(sceneArchive, sceneDocument, &error)) {
        outFail = "synthetic scene archive did not decode: " + error;
        return false;
    }
    sceneDocument.dependencies.push_back(
        requiredDep("demo/raw-payload", rawHash));
    std::vector<std::uint8_t> sceneBytes;
    if (!engine::assets::phrc::encode(sceneDocument, sceneBytes, &error)) {
        outFail = "scene archive re-encode failed: " + error;
        return false;
    }
    store->add("consumer/board-scene.phscene", sceneBytes);
    resolver.map(
        "demo/board-scene", "consumer/board-scene.phscene", AssetKind::Phrc);

    // A dependency-only prefab: no embedded files, one required scene edge.
    std::vector<std::uint8_t> prefabBytes;
    if (!engine::assets::phlosion::encodePrefabArchive(
            "demo/crate-a",
            "DemoProp",
            prefabMetadata,
            {},
            {requiredDep("demo/board-scene", phrcHash(sceneBytes))},
            prefabBytes,
            &error)) {
        outFail = "dependency-only prefab encode failed: " + error;
        return false;
    }
    store->add("consumer/crate-a.phlo", prefabBytes);
    resolver.map("demo/crate-a", "consumer/crate-a.phlo", AssetKind::Phrc);

    PhlosionAssetGraphLoader loader(store, resolver.resolver());
    if (!loader.start("demo/crate-a")) {
        outFail = "the prefab root was rejected";
        return false;
    }
    AssetGraphResult result;
    if (!expectReady(loader, result, outFail)) {
        return false;
    }
    if (result.assets.size() != 3u ||
        !result.skippedOptionalDependencies.empty()) {
        outFail = "the prefab closure is not exactly prefab, scene and raw";
        return false;
    }
    const LoadedAsset* prefab = findAsset(result, "demo/crate-a");
    const LoadedAsset* scene = findAsset(result, "demo/board-scene");
    const LoadedAsset* raw = findAsset(result, "demo/raw-payload");
    if (!prefab || !scene || !raw ||
        raw->kind != AssetKind::Bytes ||
        raw->bytes != rawPayload ||
        raw->contentHash != rawHash ||
        prefab->kind != AssetKind::Phrc ||
        scene->kind != AssetKind::Phrc) {
        outFail = "the prefab closure assets do not match the fixture";
        return false;
    }

    PrefabArchiveStore prefabStore;
    if (!prefabStore.loadBytes(prefab->bytes, &error) ||
        prefabStore.prefabId() != "demo/crate-a" ||
        prefabStore.prefabKind() != "DemoProp" ||
        prefabStore.fileCount() != 0u) {
        outFail = "the returned prefab archive did not decode: " + error;
        return false;
    }
    try {
        if (nlohmann::json::parse(prefabStore.metadataJson()) !=
            nlohmann::json::parse(prefabMetadata)) {
            outFail = "the returned prefab metadata does not match the fixture";
            return false;
        }
    } catch (const nlohmann::json::exception& exception) {
        outFail = "the returned prefab metadata is not valid JSON: " +
            std::string(exception.what());
        return false;
    }

    SceneArchiveStore sceneStore;
    if (!sceneStore.loadBytes(scene->bytes, &error) ||
        sceneStore.sceneId() != "demo/board-scene" ||
        sceneStore.fileCount() != 2u) {
        outFail = "the returned scene archive did not decode: " + error;
        return false;
    }
    std::string decodedJson;
    std::vector<std::uint8_t> decodedGeometry;
    if (!sceneStore.readText("canonical/scene.json", decodedJson, &error) ||
        decodedJson != sceneJson ||
        !sceneStore.readBytes(
            "canonical/geometry.bin", decodedGeometry, &error) ||
        decodedGeometry != sceneGeometry) {
        outFail = "the returned scene content does not match the fixture";
        return false;
    }

    if (resolver.resolveCount("demo/crate-a") != 1u ||
        resolver.resolveCount("demo/board-scene") != 1u ||
        resolver.resolveCount("demo/raw-payload") != 1u ||
        store->readCount("consumer/crate-a.phlo") != 1u ||
        store->readCount("consumer/board-scene.phscene") != 1u ||
        store->readCount("consumer/raw.bin") != 1u) {
        outFail = "the consumer graph did not resolve and read each asset once";
        return false;
    }
    const std::vector<std::string> order = store->readOrder();
    if (order.size() != 3u ||
        order[0] != "consumer/crate-a.phlo" ||
        order[1] != "consumer/board-scene.phscene" ||
        order[2] != "consumer/raw.bin") {
        outFail = "the consumer graph did not read the closure in "
            "dependency order";
        return false;
    }
    return true;
}

// Diamond closure: the shared leaf is resolved, read and retained once, an
// early edge records a hash constraint before the leaf loads, and a later edge
// constrains the leaf after it is already loaded.
bool caseClosureAndDedup(std::string& outFail) {
    const std::shared_ptr<SyntheticStore> store =
        std::make_shared<SyntheticStore>();
    ResolverFixture resolver;

    const std::vector<std::uint8_t> leafPayload{1u, 2u, 3u, 4u, 5u};
    std::uint64_t leafHash = 0u;
    addBytesAsset(
        store, resolver, "demo/leaf", "graph/leaf.bin", leafPayload, leafHash);
    PhrcFixture alpha;
    if (!addPhrcAsset(
            store, resolver, "demo/alpha", "graph/alpha.phrc", alpha, outFail,
            {requiredDep("demo/leaf", leafHash)})) {
        return false;
    }
    PhrcFixture beta;
    if (!addPhrcAsset(
            store, resolver, "demo/beta", "graph/beta.phrc", beta, outFail,
            {requiredDep("demo/leaf")})) {
        return false;
    }
    PhrcFixture root;
    if (!addPhrcAsset(
            store, resolver, "demo/root", "graph/root.phrc", root, outFail,
            {requiredDep("demo/alpha", alpha.contentHash),
             requiredDep("demo/beta", beta.contentHash)})) {
        return false;
    }
    const std::size_t expectedBytes = root.bytes.size() + alpha.bytes.size() +
        beta.bytes.size() + leafPayload.size();

    PhlosionAssetGraphLoader loader(store, resolver.resolver());
    if (!loader.start("demo/root")) {
        outFail = "the diamond root was rejected";
        return false;
    }
    AssetGraphResult result;
    if (!expectReady(loader, result, outFail, &expectedBytes)) {
        return false;
    }
    if (result.assets.size() != 4u ||
        !findAsset(result, "demo/leaf") ||
        !findAsset(result, "demo/alpha") ||
        !findAsset(result, "demo/beta") ||
        !findAsset(result, "demo/root")) {
        outFail = "the diamond closure did not publish every unique asset";
        return false;
    }
    if (resolver.resolveCount("demo/leaf") != 1u ||
        resolver.resolveCount("demo/alpha") != 1u ||
        resolver.resolveCount("demo/beta") != 1u ||
        resolver.resolveCount("demo/root") != 1u) {
        outFail = "a shared asset was resolved more than once";
        return false;
    }
    if (store->readCount("graph/leaf.bin") != 1u ||
        store->readCount("graph/alpha.phrc") != 1u ||
        store->readCount("graph/beta.phrc") != 1u ||
        store->readCount("graph/root.phrc") != 1u) {
        outFail = "a shared asset was read more than once";
        return false;
    }
    const LoadedAsset* leaf = findAsset(result, "demo/leaf");
    if (!leaf || leaf->kind != AssetKind::Bytes ||
        leaf->bytes != leafPayload || leaf->contentHash != leafHash) {
        outFail = "the shared leaf payload does not match the fixture";
        return false;
    }
    if (!result.skippedOptionalDependencies.empty()) {
        outFail = "a required-only closure recorded skipped optionals";
        return false;
    }
    if (loader.loadedBytes() != 0u) {
        outFail = "take did not release the ready byte accounting";
        return false;
    }

    // A required edge that constrains an already-loaded asset, plus an edge
    // with zero (unspecified) hash, both succeed: the root names the leaf with
    // no hash, and the alpha document, decoded only after the leaf was read,
    // adds the nonzero constraint.
    {
        const std::shared_ptr<SyntheticStore> lateStore =
            std::make_shared<SyntheticStore>();
        ResolverFixture lateResolver;
        std::uint64_t lateLeafHash = 0u;
        addBytesAsset(
            lateStore, lateResolver, "demo/leaf", "late/leaf.bin",
            leafPayload, lateLeafHash);
        PhrcFixture lateAlpha;
        if (!addPhrcAsset(
                lateStore, lateResolver, "demo/alpha", "late/alpha.phrc",
                lateAlpha, outFail,
                {requiredDep("demo/leaf", lateLeafHash)})) {
            return false;
        }
        PhrcFixture lateRoot;
        if (!addPhrcAsset(
                lateStore, lateResolver, "demo/root", "late/root.phrc",
                lateRoot, outFail,
                {requiredDep("demo/leaf"),
                 requiredDep("demo/alpha", lateAlpha.contentHash)})) {
            return false;
        }
        PhlosionAssetGraphLoader lateLoader(
            lateStore, lateResolver.resolver());
        if (!lateLoader.start("demo/root")) {
            outFail = "the late-constraint root was rejected";
            return false;
        }
        AssetGraphResult lateResult;
        if (!expectReady(lateLoader, lateResult, outFail)) {
            return false;
        }
        if (lateResult.assets.size() != 3u ||
            lateResolver.resolveCount("demo/leaf") != 1u ||
            lateStore->readCount("late/leaf.bin") != 1u) {
            outFail = "the late-constraint graph did not dedup the leaf";
            return false;
        }
        const std::vector<std::string> lateOrder = lateStore->readOrder();
        if (lateOrder.size() != 3u ||
            lateOrder[0] != "late/root.phrc" ||
            lateOrder[1] != "late/leaf.bin" ||
            lateOrder[2] != "late/alpha.phrc") {
            outFail = "the constrained leaf was not loaded before the document "
                "that constrains it";
            return false;
        }
        const LoadedAsset* lateLeaf = findAsset(lateResult, "demo/leaf");
        if (!lateLeaf || lateLeaf->contentHash != lateLeafHash) {
            outFail = "the late-constrained leaf hash does not match";
            return false;
        }
    }
    return true;
}

bool caseHashConstraints(std::string& outFail) {
    const std::vector<std::uint8_t> leafPayload{4u, 5u, 6u};
    const std::uint64_t leafHash = engine::assets::phrc::contentHash64(leafPayload);

    // Ordinary mismatch between a required edge and an opaque Bytes leaf.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        std::uint64_t ignoredHash = 0u;
        addBytesAsset(
            store, resolver, "demo/leaf", "hash/leaf.bin", leafPayload,
            ignoredHash);
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "hash/root.phrc", root, outFail,
                {requiredDep("demo/leaf", leafHash ^ 0x1u)})) {
            return false;
        }
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the mismatch root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(loader, "demo/root", "demo/root", result, outFail) ||
            result.error.find("demo/leaf") == std::string::npos) {
            if (outFail.empty()) {
                outFail = "the mismatch diagnostic omits the target asset ID";
            }
            return false;
        }
    }

    // A PHRC edge must compare the document hash, not the whole-file hash.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        PhrcFixture child;
        if (!addPhrcAsset(
                store, resolver, "demo/child", "hash/child.phrc", child,
                outFail)) {
            return false;
        }
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "hash/root.phrc", root, outFail,
                {requiredDep(
                    "demo/child",
                    engine::assets::phrc::contentHash64(child.bytes))})) {
            return false;
        }
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the whole-file-hash root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(loader, "demo/root", "demo/child", result, outFail)) {
            return false;
        }
        if (child.bytes.empty() || child.contentHash == 0u) {
            outFail = "the PHRC fixture is not usable";
            return false;
        }
    }

    // Conflicting nonzero constraints from two parents of one shared leaf.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        std::uint64_t ignoredHash = 0u;
        addBytesAsset(
            store, resolver, "demo/leaf", "conflict/leaf.bin", leafPayload,
            ignoredHash);
        PhrcFixture alpha;
        if (!addPhrcAsset(
                store, resolver, "demo/alpha", "conflict/alpha.phrc", alpha,
                outFail, {requiredDep("demo/leaf", leafHash)})) {
            return false;
        }
        PhrcFixture beta;
        if (!addPhrcAsset(
                store, resolver, "demo/beta", "conflict/beta.phrc", beta,
                outFail,
                {requiredDep("demo/leaf", leafHash ^ 0x2u)})) {
            return false;
        }
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "conflict/root.phrc", root,
                outFail,
                {requiredDep("demo/alpha"), requiredDep("demo/beta")})) {
            return false;
        }
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the conflicting-constraint root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(loader, "demo/root", "demo/beta", result, outFail)) {
            return false;
        }
        if (result.error.find("demo/leaf") == std::string::npos) {
            outFail = "the conflict diagnostic omits the target asset ID";
            return false;
        }
    }

    // A later constraint on an already-loaded shared node fails immediately.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        std::uint64_t ignoredHash = 0u;
        addBytesAsset(
            store, resolver, "demo/leaf", "late-conflict/leaf.bin",
            leafPayload, ignoredHash);
        PhrcFixture alpha;
        if (!addPhrcAsset(
                store, resolver, "demo/alpha", "late-conflict/alpha.phrc",
                alpha, outFail, {requiredDep("demo/leaf", leafHash ^ 0x4u)})) {
            return false;
        }
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "late-conflict/root.phrc", root,
                outFail,
                {requiredDep("demo/leaf"), requiredDep("demo/alpha")})) {
            return false;
        }
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the late-conflict root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(loader, "demo/root", "demo/alpha", result, outFail)) {
            return false;
        }
        if (result.error.find("demo/leaf") == std::string::npos) {
            outFail = "the late-conflict diagnostic omits the target asset ID";
            return false;
        }
    }
    return true;
}

bool caseOptionalEdges(std::string& outFail) {
    const std::vector<std::uint8_t> leafPayload{7u, 7u, 7u};
    std::uint64_t leafHash = 0u;

    // An optional edge to an unresolved ID is recorded without being resolved
    // or read, and its nonzero hash is not enforced.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "optional/root.phrc", root,
                outFail,
                {optionalDep("demo/absent-optional", 0x1122334455ull)})) {
            return false;
        }
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the optional-only root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectReady(loader, result, outFail)) {
            return false;
        }
        if (result.assets.size() != 1u ||
            result.skippedOptionalDependencies.size() != 1u) {
            outFail = "the optional edge was not recorded exactly once";
            return false;
        }
        const auto& skipped = result.skippedOptionalDependencies.front();
        if (skipped.parentAssetId != "demo/root" ||
            skipped.dependency.assetId != "demo/absent-optional" ||
            skipped.dependency.expectedContentHash != 0x1122334455ull ||
            skipped.dependency.flags != 0u) {
            outFail = "the recorded optional dependency changed";
            return false;
        }
        const std::vector<std::string> reads = store->readOrder();
        if (resolver.resolveCount("demo/absent-optional") != 0u ||
            reads.size() != 1u ||
            reads[0] != "optional/root.phrc") {
            outFail = "an optional edge resolved or read its target";
            return false;
        }
    }

    // An optional edge to an asset that another required edge loads stays
    // recorded and imposes no hash constraint.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        addBytesAsset(
            store, resolver, "demo/leaf", "optional/leaf.bin", leafPayload,
            leafHash);
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "optional/diamond.phrc", root,
                outFail,
                {requiredDep("demo/leaf", leafHash),
                 optionalDep("demo/leaf", leafHash ^ 0xffu)})) {
            return false;
        }
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the shared optional root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectReady(loader, result, outFail)) {
            return false;
        }
        if (result.assets.size() != 2u ||
            result.skippedOptionalDependencies.size() != 1u ||
            result.skippedOptionalDependencies.front().dependency.assetId !=
                "demo/leaf" ||
            resolver.resolveCount("demo/leaf") != 1u ||
            store->readCount("optional/leaf.bin") != 1u) {
            outFail =
                "a shared optional edge changed the loaded closure or counts";
            return false;
        }
    }

    // An optional edge that would close a required cycle is ignored.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        PhrcFixture child;
        if (!addPhrcAsset(
                store, resolver, "demo/child", "optional/cycle-child.phrc",
                child, outFail, {optionalDep("demo/root")})) {
            return false;
        }
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "optional/cycle-root.phrc",
                root, outFail, {requiredDep("demo/child")})) {
            return false;
        }
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the optional-cycle root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectReady(loader, result, outFail)) {
            return false;
        }
        if (result.assets.size() != 2u ||
            result.skippedOptionalDependencies.size() != 1u ||
            result.skippedOptionalDependencies.front().parentAssetId !=
                "demo/child" ||
            result.skippedOptionalDependencies.front().dependency.assetId !=
                "demo/root") {
            outFail = "the optional cycle was not ignored and recorded";
            return false;
        }
    }
    return true;
}

bool caseCyclesAndFlags(std::string& outFail) {
    // Self-cycle through a required edge.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "cycle/self.phrc", root,
                outFail, {requiredDep("demo/root")})) {
            return false;
        }
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the self-cycle root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(
                loader, "demo/root", "depends on itself", result, outFail)) {
            return false;
        }
    }

    // Three-node required cycle.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        PhrcFixture first;
        PhrcFixture second;
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "cycle/root.phrc", root,
                outFail, {requiredDep("demo/first")}) ||
            !addPhrcAsset(
                store, resolver, "demo/first", "cycle/first.phrc", first,
                outFail, {requiredDep("demo/second")}) ||
            !addPhrcAsset(
                store, resolver, "demo/second", "cycle/second.phrc", second,
                outFail, {requiredDep("demo/root")})) {
            return false;
        }
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the three-node cycle root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(loader, "demo/root", "cycle", result, outFail)) {
            return false;
        }
        if (result.error.find("demo/second") == std::string::npos ||
            result.error.find("demo/root") == std::string::npos) {
            outFail = "the cycle diagnostic omits the closing edge IDs";
            return false;
        }
    }

    // The closing edge is discovered at a node whose target was decoded later
    // than the rest of the cycle; detection must still succeed.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        PhrcFixture p;
        PhrcFixture t;
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "cycle/wide-root.phrc", root,
                outFail,
                {requiredDep("demo/p"), requiredDep("demo/t")}) ||
            !addPhrcAsset(
                store, resolver, "demo/p", "cycle/p.phrc", p, outFail,
                {requiredDep("demo/t")}) ||
            !addPhrcAsset(
                store, resolver, "demo/t", "cycle/t.phrc", t, outFail,
                {requiredDep("demo/p")})) {
            return false;
        }
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the wide cycle root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(loader, "demo/root", "cycle", result, outFail)) {
            return false;
        }
    }

    // Unknown flag bits are rejected on required and optional edges alike.
    for (const std::uint32_t flags : {
             engine::assets::phrc::kDependencyRequired | 0x4u,
             0x2u}) {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        std::uint64_t leafHash = 0u;
        const std::vector<std::uint8_t> leafPayload{3u};
        addBytesAsset(
            store, resolver, "demo/leaf", "flags/leaf.bin", leafPayload,
            leafHash);
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "flags/root.phrc", root,
                outFail, {optionalDep("demo/leaf", leafHash, flags)})) {
            return false;
        }
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the unknown-flag root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(loader, "demo/root", "flag", result, outFail)) {
            return false;
        }
        if (resolver.resolveCount("demo/leaf") != 0u) {
            outFail = "an edge with unknown flags was resolved anyway";
            return false;
        }
    }
    return true;
}

bool caseFailureShapes(std::string& outFail) {
    // Unmapped root: no read is attempted.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/unmapped")) {
            outFail = "an unmapped root ID was rejected by start";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(
                loader, "demo/unmapped", "demo/unmapped", result, outFail)) {
            return false;
        }
        if (resolver.resolveCount("demo/unmapped") != 1u ||
            !store->readOrder().empty()) {
            outFail = "an unmapped root triggered a read";
            return false;
        }
    }

    // Resolver exception.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        resolver.throwFor("demo/root");
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the throwing-resolver root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(loader, "demo/root", "threw", result, outFail)) {
            return false;
        }
    }

    // Empty resolved path.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        resolver.setRaw("demo/root", AssetLocation{"", AssetKind::Phrc});
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the empty-path root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(
                loader, "demo/root", "empty virtual path", result, outFail)) {
            return false;
        }
    }

    // Invalid asset kind.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        resolver.setRaw(
            "demo/root",
            AssetLocation{"invalid/root.bin", static_cast<AssetKind>(7)});
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the invalid-kind root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(
                loader, "demo/root", "invalid asset kind", result, outFail)) {
            return false;
        }
    }

    // Provider failure and provider exception.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        resolver.map("demo/root", "missing/root.bin");
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the missing-provider root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(loader, "demo/root", "demo/root", result, outFail)) {
            return false;
        }
        if (result.error.find("missing/root.bin") == std::string::npos) {
            outFail = "the provider failure diagnostic omits the path";
            return false;
        }
    }
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        store->add("throwing/root.bin", bytesOf("payload"));
        store->throwFor("throwing/root.bin");
        resolver.map("demo/root", "throwing/root.bin");
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the throwing-provider root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(loader, "demo/root", "threw", result, outFail)) {
            return false;
        }
    }

    // Corrupt and truncated PHRC payloads fail with an asset-specific message.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        store->add("corrupt/root.bin", bytesOf("not a phrc document"));
        resolver.map("demo/corrupt", "corrupt/root.bin");
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/corrupt")) {
            outFail = "the corrupt root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(loader, "demo/corrupt", "PHRC", result, outFail)) {
            return false;
        }
        if (result.error.find("demo/corrupt") == std::string::npos) {
            outFail = "the corrupt PHRC diagnostic omits the asset ID";
            return false;
        }
    }
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        PhrcFixture fixture;
        if (!addPhrcAsset(
                store, resolver, "demo/truncated", "corrupt/truncated.phrc",
                fixture, outFail)) {
            return false;
        }
        std::vector<std::uint8_t> truncated = fixture.bytes;
        truncated.resize(truncated.size() / 2u);
        store->add("corrupt/truncated.phrc", std::move(truncated));
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/truncated")) {
            outFail = "the truncated root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(
                loader, "demo/truncated", "demo/truncated", result, outFail)) {
            return false;
        }
    }
    return true;
}

// Failures that happen after a valid root already loaded and staged data: the
// Failed result keeps the root and the child diagnostic and drops the staged
// payload, the recorded optional edge and the byte accounting.
bool caseChildFailures(std::string& outFail) {
    // Required child with no resolver mapping.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "child/root.phrc", root,
                outFail,
                {requiredDep("demo/child"),
                 optionalDep("demo/absent-optional", 0x99u)})) {
            return false;
        }
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the missing-child root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(loader, "demo/root", "demo/child", result, outFail)) {
            return false;
        }
        const std::vector<std::string> reads = store->readOrder();
        if (reads.size() != 1u || reads[0] != "child/root.phrc") {
            outFail = "the missing-child graph did not read only the root";
            return false;
        }
        if (resolver.resolveCount("demo/child") != 1u) {
            outFail = "the missing child was not resolved before failing";
            return false;
        }
        if (resolver.resolveCount("demo/absent-optional") != 0u) {
            outFail = "the optional edge was resolved during a failure";
            return false;
        }
    }

    // Required child whose payload is not a valid PHRC document.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        PhrcFixture root;
        store->add("child/corrupt.phrc", bytesOf("not a phrc document"));
        resolver.map("demo/child", "child/corrupt.phrc", AssetKind::Phrc);
        if (!addPhrcAsset(
                store, resolver, "demo/root", "child/root.phrc", root,
                outFail,
                {requiredDep("demo/child"),
                 optionalDep("demo/absent-optional", 0x99u)})) {
            return false;
        }
        PhlosionAssetGraphLoader loader(store, resolver.resolver());
        if (!loader.start("demo/root")) {
            outFail = "the corrupt-child root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(loader, "demo/root", "demo/child", result, outFail)) {
            return false;
        }
        if (result.error.find("PHRC") == std::string::npos) {
            outFail = "the corrupt child diagnostic does not name the PHRC "
                "decode failure";
            return false;
        }
        const std::vector<std::string> reads = store->readOrder();
        if (reads.size() != 2u ||
            reads[0] != "child/root.phrc" ||
            reads[1] != "child/corrupt.phrc") {
            outFail = "the corrupt-child graph did not read the root first";
            return false;
        }
        if (resolver.resolveCount("demo/absent-optional") != 0u) {
            outFail = "the optional edge was resolved during a failure";
            return false;
        }
    }
    return true;
}

bool caseLifecycle(std::string& outFail) {
    const std::shared_ptr<SyntheticStore> store =
        std::make_shared<SyntheticStore>();
    ResolverFixture resolver;
    PhrcFixture root;
    if (!addPhrcAsset(
            store, resolver, "demo/root", "lifecycle/root.phrc", root,
            outFail)) {
        return false;
    }

    const auto rejectsConstructor = [](const std::function<void()>& build) {
        try {
            build();
        } catch (const std::invalid_argument&) {
            return true;
        } catch (...) {
            return false;
        }
        return false;
    };
    if (!rejectsConstructor([&] {
            PhlosionAssetGraphLoader loader(nullptr, resolver.resolver());
            (void)loader;
        }) ||
        !rejectsConstructor([&] {
            PhlosionAssetGraphLoader loader(store, AssetResolver{});
            (void)loader;
        }) ||
        !rejectsConstructor([&] {
            AssetGraphOptions options;
            options.maxAssets = 0u;
            PhlosionAssetGraphLoader loader(
                store, resolver.resolver(), options);
            (void)loader;
        })) {
        outFail =
            "the constructor did not reject a null store, empty resolver or "
            "zero maxAssets with invalid_argument";
        return false;
    }

    PhlosionAssetGraphLoader loader(store, resolver.resolver());
    std::string error = "unset";
    if (loader.start("", &error) || error.empty() ||
        loader.state() != AssetGraphState::Idle) {
        outFail = "start accepted an empty root ID";
        return false;
    }
    loader.pump();
    if (loader.state() != AssetGraphState::Idle) {
        outFail = "pump changed the Idle state";
        return false;
    }
    if (loader.cancel()) {
        outFail = "cancel succeeded from Idle";
        return false;
    }
    AssetGraphResult idleResult;
    idleResult.rootAssetId = "sentinel";
    if (loader.take(idleResult) ||
        idleResult.rootAssetId != "sentinel") {
        outFail = "take succeeded from Idle";
        return false;
    }

    error.clear();
    if (!loader.start("demo/root", &error) || !error.empty() ||
        loader.state() != AssetGraphState::Loading) {
        outFail = "start did not admit a valid root from Idle";
        return false;
    }
    AssetGraphResult loadingResult;
    loadingResult.rootAssetId = "sentinel";
    if (loader.take(loadingResult) ||
        loadingResult.rootAssetId != "sentinel") {
        outFail = "take succeeded while Loading";
        return false;
    }
    error.clear();
    if (loader.start("demo/other", &error) || error.empty() ||
        loader.state() != AssetGraphState::Loading) {
        outFail = "start admitted a second session while Loading";
        return false;
    }
    AssetGraphResult result;
    if (!expectReady(loader, result, outFail)) {
        return false;
    }
    if (result.rootAssetId != "demo/root") {
        outFail = "the rejected second start replaced the active session";
        return false;
    }

    // Ready sessions reject admission and cancel until they are taken or reset.
    error.clear();
    if (loader.cancel()) {
        outFail = "cancel succeeded from Idle after take";
        return false;
    }
    if (!loader.start("demo/root") ||
        !pumpToTerminal(loader, result.state, outFail)) {
        return false;
    }
    if (loader.state() != AssetGraphState::Ready) {
        outFail = "the session did not reach Ready for the admission checks";
        return false;
    }
    if (loader.cancel()) {
        outFail = "cancel succeeded from Ready";
        return false;
    }
    error.clear();
    if (loader.start("demo/other", &error) || error.empty() ||
        loader.state() != AssetGraphState::Ready) {
        outFail = "start admitted a session while a Ready result was pending";
        return false;
    }
    const std::size_t readyBytes = loader.loadedBytes();
    loader.pump();
    if (loader.state() != AssetGraphState::Ready ||
        loader.loadedBytes() != readyBytes) {
        outFail = "pump changed a Ready session";
        return false;
    }
    loader.reset();
    if (loader.state() != AssetGraphState::Idle || loader.loadedBytes() != 0u ||
        loader.take(result)) {
        outFail = "reset did not drop the Ready session";
        return false;
    }

    // Failure recovery through take.
    if (!loader.start("demo/unmapped")) {
        outFail = "the recovery root was rejected";
        return false;
    }
    AssetGraphResult failed;
    if (!expectFailed(
            loader, "demo/unmapped", "demo/unmapped", failed, outFail)) {
        return false;
    }
    if (loader.state() != AssetGraphState::Idle) {
        outFail = "take did not return a failed session to Idle";
        return false;
    }
    if (!loader.start("demo/root") ||
        !expectReady(loader, failed, outFail)) {
        return false;
    }
    if (failed.rootAssetId != "demo/root" || failed.assets.size() != 1u) {
        outFail = "a failed session did not recover with a new root";
        return false;
    }
    return true;
}

bool caseLimits(std::string& outFail) {
    const std::vector<std::uint8_t> leafPayload{1u, 1u};

    // Unique asset limit counts required assets only.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        PhrcFixture first;
        PhrcFixture second;
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/first", "limits/first.phrc", first,
                outFail) ||
            !addPhrcAsset(
                store, resolver, "demo/second", "limits/second.phrc", second,
                outFail) ||
            !addPhrcAsset(
                store, resolver, "demo/root", "limits/root.phrc", root,
                outFail,
                {requiredDep("demo/first"), requiredDep("demo/second")})) {
            return false;
        }
        const std::size_t totalBytes =
            root.bytes.size() + first.bytes.size() + second.bytes.size();
        {
            AssetGraphOptions options;
            options.maxAssets = 3u;
            PhlosionAssetGraphLoader loader(store, resolver.resolver(), options);
            if (!loader.start("demo/root")) {
                outFail = "the at-limit root was rejected";
                return false;
            }
            AssetGraphResult result;
            if (!expectReady(loader, result, outFail, &totalBytes) ||
                result.assets.size() != 3u) {
                if (outFail.empty()) {
                    outFail = "the exact asset limit was not accepted";
                }
                return false;
            }
        }
        {
            AssetGraphOptions options;
            options.maxAssets = 2u;
            PhlosionAssetGraphLoader loader(store, resolver.resolver(), options);
            if (!loader.start("demo/root")) {
                outFail = "the over-limit root was rejected";
                return false;
            }
            AssetGraphResult result;
            if (!expectFailed(
                    loader, "demo/root", "unique asset limit", result,
                    outFail)) {
                return false;
            }
        }
    }

    // Dependency edge limit counts duplicate and optional records.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        std::uint64_t leafHash = 0u;
        addBytesAsset(
            store, resolver, "demo/leaf", "limits/leaf.bin", leafPayload,
            leafHash);
        PhrcFixture duplicated;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "limits/duplicated.phrc",
                duplicated, outFail,
                {requiredDep("demo/leaf", leafHash),
                 requiredDep("demo/leaf")})) {
            return false;
        }
        {
            AssetGraphOptions options;
            options.maxDependencyEdges = 1u;
            PhlosionAssetGraphLoader loader(store, resolver.resolver(), options);
            if (!loader.start("demo/root")) {
                outFail = "the duplicate-edge root was rejected";
                return false;
            }
            AssetGraphResult result;
            if (!expectFailed(
                    loader, "demo/root", "dependency edge limit", result,
                    outFail)) {
                return false;
            }
        }
        {
            AssetGraphOptions options;
            options.maxDependencyEdges = 2u;
            PhlosionAssetGraphLoader loader(store, resolver.resolver(), options);
            if (!loader.start("demo/root")) {
                outFail = "the duplicate-edge root was rejected";
                return false;
            }
            AssetGraphResult result;
            if (!expectReady(loader, result, outFail)) {
                return false;
            }
            if (result.assets.size() != 2u ||
                resolver.resolveCount("demo/leaf") != 1u ||
                store->readCount("limits/leaf.bin") != 1u) {
                outFail = "duplicate edge records changed the loaded closure";
                return false;
            }
        }
    }
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        std::uint64_t leafHash = 0u;
        addBytesAsset(
            store, resolver, "demo/leaf", "limits/optional-leaf.bin",
            leafPayload, leafHash);
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "limits/optional-root.phrc",
                root, outFail,
                {requiredDep("demo/leaf"),
                 optionalDep("demo/absent-optional")})) {
            return false;
        }
        AssetGraphOptions options;
        options.maxDependencyEdges = 1u;
        PhlosionAssetGraphLoader loader(store, resolver.resolver(), options);
        if (!loader.start("demo/root")) {
            outFail = "the optional-edge root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectFailed(
                loader, "demo/root", "dependency edge limit", result,
                outFail)) {
            return false;
        }
    }

    // A zero edge limit is valid for a dependency-free document.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "limits/leaf-only.phrc", root,
                outFail)) {
            return false;
        }
        AssetGraphOptions options;
        options.maxDependencyEdges = 0u;
        const std::size_t rootBytes = root.bytes.size();
        options.maxEncodedBytes = rootBytes;
        PhlosionAssetGraphLoader loader(store, resolver.resolver(), options);
        if (!loader.start("demo/root")) {
            outFail = "the zero-limit root was rejected";
            return false;
        }
        AssetGraphResult result;
        if (!expectReady(loader, result, outFail, &rootBytes)) {
            return false;
        }
        if (result.assets.size() != 1u) {
            outFail = "the zero-limit closure is wrong";
            return false;
        }
    }

    // Encoded byte limit covers the whole retained closure.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        std::uint64_t leafHash = 0u;
        addBytesAsset(
            store, resolver, "demo/leaf", "limits/bytes-leaf.bin",
            leafPayload, leafHash);
        PhrcFixture root;
        if (!addPhrcAsset(
                store, resolver, "demo/root", "limits/bytes-root.phrc", root,
                outFail, {requiredDep("demo/leaf", leafHash)})) {
            return false;
        }
        const std::size_t totalBytes = root.bytes.size() + leafPayload.size();
        {
            AssetGraphOptions options;
            options.maxEncodedBytes = totalBytes;
            PhlosionAssetGraphLoader loader(store, resolver.resolver(), options);
            if (!loader.start("demo/root")) {
                outFail = "the at-limit byte root was rejected";
                return false;
            }
            AssetGraphResult result;
            if (!expectReady(loader, result, outFail, &totalBytes)) {
                return false;
            }
        }
        {
            AssetGraphOptions options;
            options.maxEncodedBytes = totalBytes - 1u;
            PhlosionAssetGraphLoader loader(store, resolver.resolver(), options);
            if (!loader.start("demo/root")) {
                outFail = "the over-limit byte root was rejected";
                return false;
            }
            AssetGraphResult result;
            if (!expectFailed(
                    loader, "demo/root", "byte limit", result, outFail)) {
                return false;
            }
        }
    }

    // A zero encoded byte cap admits an empty payload and rejects others.
    {
        const std::shared_ptr<SyntheticStore> store =
            std::make_shared<SyntheticStore>();
        ResolverFixture resolver;
        std::uint64_t emptyHash = 0u;
        addBytesAsset(
            store, resolver, "demo/empty", "limits/empty.bin", {},
            emptyHash);
        std::uint64_t payloadHash = 0u;
        addBytesAsset(
            store, resolver, "demo/nonempty", "limits/nonempty.bin",
            leafPayload, payloadHash);
        AssetGraphOptions options;
        options.maxEncodedBytes = 0u;
        {
            PhlosionAssetGraphLoader loader(store, resolver.resolver(), options);
            if (!loader.start("demo/empty")) {
                outFail = "the zero-cap empty root was rejected";
                return false;
            }
            AssetGraphResult result;
            if (!expectReady(loader, result, outFail)) {
                return false;
            }
            if (result.assets.size() != 1u) {
                outFail = "the zero-cap empty closure is wrong";
                return false;
            }
        }
        {
            PhlosionAssetGraphLoader loader(store, resolver.resolver(), options);
            if (!loader.start("demo/nonempty")) {
                outFail = "the zero-cap payload root was rejected";
                return false;
            }
            AssetGraphResult result;
            if (!expectFailed(
                    loader, "demo/nonempty", "byte budget", result, outFail)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool test_phlosion_asset_graph_loader_contract(std::string& outFail) {
    struct Case {
        const char* name;
        bool (*run)(std::string&);
    };
    const Case cases[] = {
        {"success shapes", &caseSuccessShapes},
        {"closure and dedup", &caseClosureAndDedup},
        {"hash constraints", &caseHashConstraints},
        {"optional edges", &caseOptionalEdges},
        {"cycles and flags", &caseCyclesAndFlags},
        {"failure shapes", &caseFailureShapes},
        {"required child failures", &caseChildFailures},
        {"lifecycle", &caseLifecycle},
        {"limits", &caseLimits},
        {"cancellation and reset", &caseCancellationAndReset},
        {"consumer graph", &caseConsumerGraph},
    };
    for (const Case& test : cases) {
        std::string failure;
        if (test.run(failure)) {
            continue;
        }
        outFail = std::string(test.name) + ": " + failure;
        return false;
    }
    return true;
}
