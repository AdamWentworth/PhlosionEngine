#include "engine/assets/AsyncAssetLoader.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
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

using engine::assets::AssetLoadId;
using engine::assets::AssetLoadOptions;
using engine::assets::AssetLoadResult;
using engine::assets::AssetLoadState;
using engine::assets::AsyncAssetLoader;

constexpr std::chrono::milliseconds kWaitTimeout{15000};
constexpr std::chrono::milliseconds kGateTimeout{10000};
constexpr std::chrono::milliseconds kPollInterval{1};
// Supplementary negative observation only. Ordering and completion claims in
// these tests come from condition-variable signals, joined threads, or the
// single worker's queue ordering, never from this window.
constexpr std::chrono::milliseconds kNegativeObservation{200};

std::vector<std::uint8_t> bytesOf(std::string_view text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

const char* stateName(AssetLoadState state) {
    switch (state) {
    case AssetLoadState::Unknown: return "Unknown";
    case AssetLoadState::Queued: return "Queued";
    case AssetLoadState::Loading: return "Loading";
    case AssetLoadState::Ready: return "Ready";
    case AssetLoadState::Failed: return "Failed";
    case AssetLoadState::Cancelled: return "Cancelled";
    }
    return "invalid";
}

// Synthetic provider with an explicit read gate. A read can only pause in the
// bounded gate wait, which records a timeout diagnostic, so a stalled test
// fails clearly instead of hanging CI. Every read also records whether it ran on
// the caller thread, which must never happen.
class ControlledAssetStore final : public engine::IAssetStore {
public:
    ControlledAssetStore() : callerThread_(std::this_thread::get_id()) {}

    void addAsset(std::string path, std::vector<std::uint8_t> bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        assets_.insert_or_assign(std::move(path), std::move(bytes));
    }

    void setFailing(std::string path) {
        std::lock_guard<std::mutex> lock(mutex_);
        failing_.insert(std::move(path));
    }

    // Writes partial bytes into the caller's output and then reports failure.
    void setFailingWithPartialBytes(
        std::string path,
        std::vector<std::uint8_t> partialBytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        partialFailures_.insert_or_assign(
            std::move(path),
            std::move(partialBytes));
    }

    // The throwing path also writes partial bytes before failing.
    void setThrowing(std::string path, std::vector<std::uint8_t> partialBytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        throwing_.insert_or_assign(std::move(path), std::move(partialBytes));
    }

    void setGateEnabled(bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        gateEnabled_ = enabled;
        gateOpen_ = !enabled;
        cv_.notify_all();
    }

    void openGate() {
        std::lock_guard<std::mutex> lock(mutex_);
        gateOpen_ = true;
        cv_.notify_all();
    }

    bool gateTimedOut() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return gateTimedOut_;
    }

    bool callerThreadRead() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return callerThreadRead_;
    }

    bool waitForReadsStarted(std::size_t count, std::string& outFail) const {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, kWaitTimeout, [this, count] {
                return readsStarted_ >= count;
            })) {
            outFail = "timed out waiting for " + std::to_string(count) +
                " provider read(s) to start";
            return false;
        }
        return true;
    }

    bool waitForReadsCompleted(std::size_t count, std::string& outFail) const {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, kWaitTimeout, [this, count] {
                return readsCompleted_ >= count;
            })) {
            outFail = "timed out waiting for " + std::to_string(count) +
                " provider read(s) to complete";
            return false;
        }
        return true;
    }

    std::size_t readsStarted() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return readsStarted_;
    }

    std::size_t readsCompleted() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return readsCompleted_;
    }

    std::size_t countReads(std::string_view virtualPath) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = readCounts_.find(std::string(virtualPath));
        return found == readCounts_.end() ? 0u : found->second;
    }

    bool readText(
        const std::string& virtualPath,
        std::string& outText,
        std::string* outError = nullptr) const override {
        std::vector<std::uint8_t> bytes;
        if (!readBytes(virtualPath, bytes, outError)) {
            return false;
        }
        if (bytes.empty()) {
            outText.clear();
        } else {
            outText.assign(
                reinterpret_cast<const char*>(bytes.data()),
                bytes.size());
        }
        return true;
    }

    bool readBytes(
        const std::string& virtualPath,
        std::vector<std::uint8_t>& outBytes,
        std::string* outError = nullptr) const override {
        std::unique_lock<std::mutex> lock(mutex_);
        ++readsStarted_;
        ++readCounts_[virtualPath];
        if (std::this_thread::get_id() == callerThread_) {
            callerThreadRead_ = true;
        }
        cv_.notify_all();
        if (gateEnabled_ && !gateOpen_) {
            if (!cv_.wait_for(lock, kGateTimeout, [this] {
                    return gateOpen_;
                })) {
                gateTimedOut_ = true;
                if (outError) {
                    *outError =
                        "test gate timed out while reading: " + virtualPath;
                }
                outBytes.clear();
                ++readsCompleted_;
                cv_.notify_all();
                return false;
            }
        }
        const auto throwing = throwing_.find(virtualPath);
        if (throwing != throwing_.end()) {
            outBytes = throwing->second;
            if (outError) {
                *outError = "synthetic provider exception";
            }
            ++readsCompleted_;
            cv_.notify_all();
            lock.unlock();
            throw std::runtime_error("synthetic provider failure");
        }
        const auto partial = partialFailures_.find(virtualPath);
        if (partial != partialFailures_.end()) {
            outBytes = partial->second;
            if (outError) {
                *outError = "synthetic provider failure with partial bytes";
            }
            ++readsCompleted_;
            cv_.notify_all();
            return false;
        }
        const auto asset = assets_.find(virtualPath);
        if (failing_.contains(virtualPath) || asset == assets_.end()) {
            if (outError) {
                *outError = "synthetic missing asset: " + virtualPath;
            }
            ++readsCompleted_;
            cv_.notify_all();
            return false;
        }
        outBytes = asset->second;
        ++readsCompleted_;
        cv_.notify_all();
        return true;
    }

    bool exists(const std::string& virtualPath) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return assets_.contains(virtualPath);
    }

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    const std::thread::id callerThread_;
    std::map<std::string, std::vector<std::uint8_t>> assets_;
    std::map<std::string, std::vector<std::uint8_t>> throwing_;
    std::map<std::string, std::vector<std::uint8_t>> partialFailures_;
    mutable std::map<std::string, std::size_t> readCounts_;
    std::set<std::string> failing_;
    bool gateEnabled_ = false;
    bool gateOpen_ = true;
    mutable bool gateTimedOut_ = false;
    mutable bool callerThreadRead_ = false;
    mutable std::size_t readsStarted_ = 0u;
    mutable std::size_t readsCompleted_ = 0u;
};

// Failure-path cleanup: opens a controlled gate so a paused read can never block
// a loader destructor, then joins any tracked helper thread. Constructed after
// the loader so it runs before loader destruction, and it never joins while a
// caller-held lock is active.
class GateAndThreadCleanup final {
public:
    explicit GateAndThreadCleanup(std::weak_ptr<ControlledAssetStore> store)
        : store_(std::move(store)) {}

    GateAndThreadCleanup(
        std::weak_ptr<ControlledAssetStore> store,
        std::thread& thread)
        : store_(std::move(store)), thread_(&thread) {}

    ~GateAndThreadCleanup() {
        if (const std::shared_ptr<ControlledAssetStore> store = store_.lock()) {
            store->openGate();
        }
        if (thread_ && thread_->joinable()) {
            thread_->join();
        }
    }

private:
    std::weak_ptr<ControlledAssetStore> store_;
    std::thread* thread_ = nullptr;
};

AssetLoadResult sentinelResult() {
    AssetLoadResult result;
    result.state = AssetLoadState::Ready;
    result.bytes = bytesOf("sentinel");
    result.error = "sentinel";
    return result;
}

bool isSentinel(const AssetLoadResult& result) {
    return result.state == AssetLoadState::Ready &&
        result.bytes == bytesOf("sentinel") && result.error == "sentinel";
}

// The loader exposes no state-change wait, so reaching a state is polled within
// a bounded deadline and reports the last observed state on timeout.
bool waitForState(
    AsyncAssetLoader& loader,
    AssetLoadId id,
    AssetLoadState expected,
    std::string& outFail) {
    const auto deadline = std::chrono::steady_clock::now() + kWaitTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (loader.state(id) == expected) {
            return true;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    outFail = "timed out waiting for load " + std::to_string(id) +
        " to reach " + stateName(expected) + "; last state " +
        stateName(loader.state(id));
    return false;
}

bool checkConstructorAndEmptyBudget(std::string& outFail) {
    const std::shared_ptr<ControlledAssetStore> store =
        std::make_shared<ControlledAssetStore>();
    store->addAsset("empty/blob.bin", {});

    bool threw = false;
    try {
        AsyncAssetLoader loader(nullptr);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    if (!threw) {
        outFail = "a null provider was accepted by the constructor";
        return false;
    }

    AssetLoadOptions zeroRequests;
    zeroRequests.maxRequests = 0u;
    threw = false;
    try {
        AsyncAssetLoader loader(store, zeroRequests);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    if (!threw) {
        outFail = "maxRequests of zero was accepted by the constructor";
        return false;
    }

    AssetLoadOptions zeroBudget;
    zeroBudget.maxResidentBytes = 0u;
    AsyncAssetLoader loader(store, zeroBudget);
    std::string error = "unset";
    if (loader.request("", &error) != 0u || error.empty()) {
        outFail = "an empty virtual path was admitted without a diagnostic";
        return false;
    }
    const AssetLoadId empty = loader.request("empty/blob.bin");
    if (empty == 0u ||
        !waitForState(loader, empty, AssetLoadState::Ready, outFail)) {
        return false;
    }
    AssetLoadResult result;
    if (!loader.take(empty, result) ||
        result.state != AssetLoadState::Ready ||
        !result.bytes.empty() ||
        !result.error.empty() ||
        loader.residentBytes() != 0u) {
        outFail = "a zero-byte asset did not succeed under a zero byte budget";
        return false;
    }
    return true;
}

bool checkSuccessAndFailure(std::string& outFail) {
    const std::shared_ptr<ControlledAssetStore> store =
        std::make_shared<ControlledAssetStore>();
    store->addAsset("text/hello.txt", bytesOf("hello"));
    store->addAsset("empty/blob.bin", {});
    store->setFailing("broken/missing.bin");
    store->setFailingWithPartialBytes("broken/partial.bin", bytesOf("partial"));
    store->setThrowing("broken/throw.bin", bytesOf("partial"));

    AsyncAssetLoader loader(store);

    if (loader.state(0u) != AssetLoadState::Unknown ||
        loader.state(12345u) != AssetLoadState::Unknown ||
        loader.cancel(0u) ||
        loader.release(0u) ||
        loader.cancel(12345u) ||
        loader.release(12345u)) {
        outFail = "unknown or invalid load IDs did not behave as Unknown";
        return false;
    }
    AssetLoadResult result = sentinelResult();
    if (loader.take(0u, result) || loader.take(12345u, result) ||
        !isSentinel(result)) {
        outFail = "taking an unknown ID modified the output result";
        return false;
    }

    const AssetLoadId hello = loader.request("text/hello.txt");
    if (hello == 0u ||
        !waitForState(loader, hello, AssetLoadState::Ready, outFail)) {
        return false;
    }
    if (!loader.take(hello, result) ||
        result.state != AssetLoadState::Ready ||
        result.bytes != bytesOf("hello") ||
        !result.error.empty() ||
        loader.residentBytes() != 0u) {
        outFail = "a successful read did not round-trip its bytes";
        return false;
    }
    if (loader.state(hello) != AssetLoadState::Unknown ||
        loader.take(hello, result) ||
        loader.cancel(hello) ||
        loader.release(hello)) {
        outFail = "a taken ID did not become Unknown";
        return false;
    }

    const AssetLoadId empty = loader.request("empty/blob.bin");
    if (empty == 0u ||
        !waitForState(loader, empty, AssetLoadState::Ready, outFail) ||
        !loader.take(empty, result) ||
        result.state != AssetLoadState::Ready ||
        !result.bytes.empty() ||
        !result.error.empty()) {
        outFail = "an empty successful asset was not a valid Ready result";
        return false;
    }

    const AssetLoadId missing = loader.request("broken/missing.bin");
    if (missing == 0u ||
        !waitForState(loader, missing, AssetLoadState::Failed, outFail)) {
        return false;
    }
    if (loader.cancel(missing) || !loader.take(missing, result) ||
        result.state != AssetLoadState::Failed ||
        !result.bytes.empty() ||
        result.error.empty()) {
        outFail = "a missing asset did not fail with a diagnostic";
        return false;
    }

    const AssetLoadId partial = loader.request("broken/partial.bin");
    if (partial == 0u ||
        !waitForState(loader, partial, AssetLoadState::Failed, outFail) ||
        !loader.take(partial, result) ||
        result.state != AssetLoadState::Failed ||
        !result.bytes.empty() ||
        result.error.empty()) {
        outFail = "a failing provider leaked partially written bytes";
        return false;
    }

    const AssetLoadId throwing = loader.request("broken/throw.bin");
    if (throwing == 0u ||
        !waitForState(loader, throwing, AssetLoadState::Failed, outFail) ||
        !loader.take(throwing, result) ||
        result.state != AssetLoadState::Failed ||
        !result.bytes.empty() ||
        result.error.find("threw") == std::string::npos) {
        outFail = "a throwing provider did not fail cleanly: " + result.error;
        return false;
    }
    if (loader.residentBytes() != 0u) {
        outFail = "a failed read retained resident bytes";
        return false;
    }
    if (store->callerThreadRead()) {
        outFail = "the provider ran on the requesting thread";
        return false;
    }

    const AssetLoadId recovered = loader.request("text/hello.txt");
    if (recovered == 0u || recovered <= throwing) {
        outFail = "the loader refused a request after provider failures";
        return false;
    }
    if (!waitForState(loader, recovered, AssetLoadState::Ready, outFail)) {
        outFail = "recovery after provider failures: " + outFail;
        return false;
    }
    if (!loader.take(recovered, result)) {
        outFail = "a recovered Ready result could not be taken";
        return false;
    }
    if (result.state != AssetLoadState::Ready ||
        result.bytes != bytesOf("hello")) {
        outFail = "recovered bytes did not match the requested asset";
        return false;
    }
    if (loader.take(recovered, result)) {
        outFail = "a taken recovery result remained available";
        return false;
    }
    return true;
}

bool checkResidentBudget(std::string& outFail) {
    const std::shared_ptr<ControlledAssetStore> store =
        std::make_shared<ControlledAssetStore>();
    store->addAsset("budget/a.bin", bytesOf("aaaa"));
    store->addAsset("budget/b.bin", bytesOf("bbbb"));
    store->addAsset("budget/c.bin", bytesOf("c"));
    store->addAsset("budget/d.bin", bytesOf("ddddddddd"));

    AssetLoadOptions options;
    options.maxResidentBytes = 8u;
    AsyncAssetLoader loader(store, options);

    const AssetLoadId a = loader.request("budget/a.bin");
    const AssetLoadId b = loader.request("budget/b.bin");
    const AssetLoadId c = loader.request("budget/c.bin");
    if (a == 0u || b == 0u || c == 0u || a >= b || b >= c) {
        outFail = "budget requests were rejected or IDs did not increase";
        return false;
    }
    if (!waitForState(loader, a, AssetLoadState::Ready, outFail) ||
        !waitForState(loader, b, AssetLoadState::Ready, outFail) ||
        !waitForState(loader, c, AssetLoadState::Failed, outFail)) {
        return false;
    }
    if (loader.residentBytes() != 8u) {
        outFail = "retained bytes did not match two resident assets";
        return false;
    }
    AssetLoadResult result;
    if (!loader.take(c, result) ||
        result.state != AssetLoadState::Failed ||
        !result.bytes.empty() ||
        result.error.empty()) {
        outFail = "an over-budget asset did not report a diagnostic";
        return false;
    }
    if (!loader.take(a, result) ||
        result.state != AssetLoadState::Ready ||
        result.bytes != bytesOf("aaaa") ||
        loader.residentBytes() != 4u) {
        outFail = "taking a Ready result did not free retained bytes";
        return false;
    }
    const AssetLoadId c2 = loader.request("budget/c.bin");
    if (c2 == 0u ||
        c2 <= c ||
        !waitForState(loader, c2, AssetLoadState::Ready, outFail) ||
        loader.residentBytes() != 5u) {
        outFail = "capacity was not reusable after taking a Ready result";
        return false;
    }
    if (!loader.release(b) || loader.residentBytes() != 1u) {
        outFail = "releasing a Ready result did not free retained bytes";
        return false;
    }
    const AssetLoadId d = loader.request("budget/d.bin");
    if (d == 0u ||
        !waitForState(loader, d, AssetLoadState::Failed, outFail) ||
        loader.residentBytes() != 1u ||
        !loader.take(d, result) ||
        result.state != AssetLoadState::Failed ||
        !result.bytes.empty()) {
        outFail = "an oversize asset was retained instead of failing";
        return false;
    }
    if (!loader.take(c2, result) ||
        result.bytes != bytesOf("c") ||
        loader.residentBytes() != 0u) {
        outFail = "final retained accounting was not zero";
        return false;
    }
    return true;
}

bool checkCapacityAccounting(std::string& outFail) {
    const std::shared_ptr<ControlledAssetStore> store =
        std::make_shared<ControlledAssetStore>();
    store->setGateEnabled(true);
    store->addAsset("gated/a.bin", bytesOf("aaaa"));
    store->addAsset("gated/b.bin", bytesOf("bbbb"));
    store->addAsset("gated/c.bin", bytesOf("cccc"));

    AssetLoadOptions options;
    options.maxRequests = 2u;
    AsyncAssetLoader loader(store, options);
    GateAndThreadCleanup cleanup(store);

    const AssetLoadId a = loader.request("gated/a.bin");
    if (a == 0u || !store->waitForReadsStarted(1u, outFail)) {
        return false;
    }
    const AssetLoadId b = loader.request("gated/b.bin");
    if (b == 0u) {
        outFail = "a second request was rejected below capacity";
        return false;
    }
    std::string error = "unset";
    if (loader.request("gated/c.bin", &error) != 0u || error.empty()) {
        outFail = "request capacity was not enforced";
        return false;
    }
    store->openGate();
    if (!waitForState(loader, a, AssetLoadState::Ready, outFail) ||
        !waitForState(loader, b, AssetLoadState::Ready, outFail)) {
        return false;
    }
    error = "unset";
    if (loader.request("gated/c.bin", &error) != 0u || error.empty()) {
        outFail = "terminal results did not hold their capacity slot";
        return false;
    }
    AssetLoadResult result;
    if (!loader.take(a, result) || result.state != AssetLoadState::Ready) {
        outFail = "taking a terminal result failed";
        return false;
    }
    const AssetLoadId c = loader.request("gated/c.bin");
    if (c == 0u ||
        c <= b ||
        !waitForState(loader, c, AssetLoadState::Ready, outFail)) {
        outFail = "taking a result did not free capacity for a new ID";
        return false;
    }
    if (loader.state(a) != AssetLoadState::Unknown ||
        loader.release(a) ||
        !loader.release(b) ||
        loader.state(b) != AssetLoadState::Unknown) {
        outFail = "reused capacity resurrected an older load ID";
        return false;
    }
    if (loader.residentBytes() != 4u ||
        !loader.take(c, result) ||
        result.bytes != bytesOf("cccc") ||
        loader.residentBytes() != 0u) {
        outFail = "capacity reuse corrupted retained accounting";
        return false;
    }
    if (store->gateTimedOut() || store->callerThreadRead()) {
        outFail = "the controlled provider did not run bounded off-thread reads";
        return false;
    }
    return true;
}

// Queued cancellation never reaches the provider, and the single worker makes a
// later sentinel request a strict witness that the earlier read and its
// publication decision already finished.
bool checkQueuedCancellation(std::string& outFail) {
    const std::shared_ptr<ControlledAssetStore> store =
        std::make_shared<ControlledAssetStore>();
    store->setGateEnabled(true);
    store->addAsset("gated/a.bin", bytesOf("aaaa"));
    store->addAsset("queued/b.bin", bytesOf("bbbb"));
    store->addAsset("sentinel/s.bin", bytesOf("ss"));

    AssetLoadOptions options;
    options.maxRequests = 2u;
    AsyncAssetLoader loader(store, options);
    GateAndThreadCleanup cleanup(store);

    const AssetLoadId active = loader.request("gated/a.bin");
    if (active == 0u || !store->waitForReadsStarted(1u, outFail)) {
        return false;
    }
    const AssetLoadId queued = loader.request("queued/b.bin");
    if (queued == 0u || loader.state(queued) != AssetLoadState::Queued) {
        outFail = "a request behind a paused read was not Queued";
        return false;
    }

    // request/state/take stay usable while a provider read is paused.
    std::string error = "unset";
    if (loader.request("sentinel/s.bin", &error) != 0u || error.empty()) {
        outFail = "capacity was not enforced while a read was paused";
        return false;
    }
    AssetLoadResult result = sentinelResult();
    if (loader.take(queued, result) || !isSentinel(result)) {
        outFail = "taking a nonterminal load modified the output result";
        return false;
    }

    if (!loader.cancel(queued) ||
        loader.state(queued) != AssetLoadState::Cancelled ||
        loader.cancel(queued)) {
        outFail = "cancelling a queued load did not reach Cancelled";
        return false;
    }
    AssetLoadResult cancelled;
    if (!loader.take(queued, cancelled) ||
        cancelled.state != AssetLoadState::Cancelled ||
        !cancelled.bytes.empty()) {
        outFail = "a cancelled queued result did not resolve as Cancelled";
        return false;
    }
    // The terminal cancellation held its slot until take, which now admits a
    // sentinel behind the paused read.
    const AssetLoadId sentinel = loader.request("sentinel/s.bin");
    if (sentinel == 0u || sentinel <= queued) {
        outFail = "taking a cancelled queued result did not free capacity";
        return false;
    }

    store->openGate();
    if (!waitForState(loader, sentinel, AssetLoadState::Ready, outFail)) {
        return false;
    }
    if (store->countReads("queued/b.bin") != 0u) {
        outFail = "cancelled queued work still called the provider";
        return false;
    }
    if (loader.residentBytes() != bytesOf("aaaa").size() + bytesOf("ss").size()) {
        outFail = "cancelled queued work leaked resident bytes";
        return false;
    }
    if (!loader.take(active, result) ||
        result.state != AssetLoadState::Ready ||
        result.bytes != bytesOf("aaaa") ||
        !loader.take(sentinel, result) ||
        result.bytes != bytesOf("ss") ||
        loader.residentBytes() != 0u) {
        outFail = "the paused read did not complete after queued cancellation";
        return false;
    }
    if (store->gateTimedOut() || store->callerThreadRead()) {
        outFail = "the controlled provider did not run bounded off-thread reads";
        return false;
    }
    return true;
}

// Repeated request/cancel/release cycles behind a paused read must not
// accumulate stale queued jobs, and every stale job must stay unread.
bool checkQueueChurn(std::string& outFail) {
    const std::shared_ptr<ControlledAssetStore> store =
        std::make_shared<ControlledAssetStore>();
    store->setGateEnabled(true);
    store->addAsset("gated/a.bin", bytesOf("aaaa"));
    store->addAsset("queued/cycle.bin", bytesOf("cycle"));
    store->addAsset("sentinel/s.bin", bytesOf("ss"));

    AssetLoadOptions options;
    options.maxRequests = 3u;
    AsyncAssetLoader loader(store, options);
    GateAndThreadCleanup cleanup(store);

    const AssetLoadId active = loader.request("gated/a.bin");
    if (active == 0u || !store->waitForReadsStarted(1u, outFail)) {
        return false;
    }
    for (int cycle = 0; cycle < 8; ++cycle) {
        const AssetLoadId queued = loader.request("queued/cycle.bin");
        if (queued == 0u || loader.state(queued) != AssetLoadState::Queued) {
            outFail = "churn cycle " + std::to_string(cycle + 1) +
                " did not admit a Queued request";
            return false;
        }
        if (cycle % 2 == 0) {
            if (!loader.cancel(queued) ||
                loader.state(queued) != AssetLoadState::Cancelled) {
                outFail = "churn cycle " + std::to_string(cycle + 1) +
                    " did not cancel its queued request";
                return false;
            }
        }
        if (!loader.release(queued) ||
            loader.state(queued) != AssetLoadState::Unknown) {
            outFail = "churn cycle " + std::to_string(cycle + 1) +
                " did not release its queued request";
            return false;
        }
    }

    const AssetLoadId sentinel = loader.request("sentinel/s.bin");
    if (sentinel == 0u) {
        outFail = "capacity was exhausted by cancelled queued churn";
        return false;
    }
    store->openGate();
    if (!waitForState(loader, sentinel, AssetLoadState::Ready, outFail)) {
        return false;
    }
    if (store->countReads("queued/cycle.bin") != 0u) {
        outFail = "stale queued work survived cancellation and ran";
        return false;
    }
    if (store->readsStarted() != 2u) {
        outFail = "churn left unexpected provider reads in the queue";
        return false;
    }
    if (loader.residentBytes() != bytesOf("aaaa").size() + bytesOf("ss").size()) {
        outFail = "churn corrupted retained byte accounting";
        return false;
    }
    AssetLoadResult result;
    if (!loader.take(active, result) ||
        result.bytes != bytesOf("aaaa") ||
        !loader.take(sentinel, result) ||
        result.bytes != bytesOf("ss") ||
        loader.residentBytes() != 0u) {
        outFail = "churn results did not drain cleanly";
        return false;
    }
    if (store->gateTimedOut() || store->callerThreadRead()) {
        outFail = "the controlled provider did not run bounded off-thread reads";
        return false;
    }
    return true;
}

// Cancel and take happen while the read is still paused; the sentinel request
// that follows is the single worker's proof that the earlier publication
// decision already passed.
bool checkActiveCancellationAndRelease(std::string& outFail) {
    const std::shared_ptr<ControlledAssetStore> store =
        std::make_shared<ControlledAssetStore>();
    store->setGateEnabled(true);
    store->addAsset("gated/a.bin", bytesOf("aaaa"));
    store->addAsset("gated/b.bin", bytesOf("bbbb"));
    store->addAsset("sentinel/s.bin", bytesOf("ss"));
    store->addAsset("follow/f.bin", bytesOf("ff"));

    AssetLoadOptions options;
    options.maxRequests = 2u;
    AsyncAssetLoader loader(store, options);
    GateAndThreadCleanup cleanup(store);

    const AssetLoadId a = loader.request("gated/a.bin");
    if (a == 0u || !store->waitForReadsStarted(1u, outFail)) {
        return false;
    }
    if (loader.state(a) != AssetLoadState::Loading) {
        outFail = "the cancellation target was not in an active read";
        return false;
    }
    if (!loader.cancel(a) || loader.state(a) != AssetLoadState::Cancelled) {
        outFail = "cancelling an in-flight load did not reach Cancelled";
        return false;
    }
    AssetLoadResult cancelled;
    if (!loader.take(a, cancelled) ||
        cancelled.state != AssetLoadState::Cancelled ||
        !cancelled.bytes.empty() ||
        cancelled.error.empty()) {
        outFail = "an in-flight cancellation could not be taken while paused";
        return false;
    }
    if (loader.state(a) != AssetLoadState::Unknown ||
        loader.take(a, cancelled) ||
        loader.residentBytes() != 0u) {
        outFail = "a taken in-flight cancellation was not released";
        return false;
    }
    // Capacity is reusable immediately, with a new ID, while the read is paused.
    const AssetLoadId sentinel = loader.request("sentinel/s.bin");
    if (sentinel == 0u || sentinel <= a) {
        outFail = "an in-flight take did not free capacity";
        return false;
    }
    store->openGate();
    if (!waitForState(loader, sentinel, AssetLoadState::Ready, outFail)) {
        return false;
    }
    if (loader.state(a) != AssetLoadState::Unknown ||
        loader.take(a, cancelled) ||
        !loader.take(sentinel, cancelled) ||
        cancelled.bytes != bytesOf("ss")) {
        outFail = "a cancelled in-flight load was resurrected later";
        return false;
    }

    // Release during an active read, using the same sentinel ordering.
    store->setGateEnabled(true);
    const AssetLoadId b = loader.request("gated/b.bin");
    // The third read in this store: the cancelled read, the sentinel, then this.
    if (b == 0u || !store->waitForReadsStarted(3u, outFail)) {
        return false;
    }
    if (loader.state(b) != AssetLoadState::Loading) {
        outFail = "the release target was not in an active read";
        return false;
    }
    if (!loader.release(b) ||
        loader.state(b) != AssetLoadState::Unknown ||
        loader.release(b)) {
        outFail = "releasing an in-flight load did not discard it";
        return false;
    }
    const AssetLoadId follow = loader.request("follow/f.bin");
    if (follow == 0u || follow <= sentinel) {
        outFail = "an in-flight release did not free capacity";
        return false;
    }
    store->openGate();
    if (!waitForState(loader, follow, AssetLoadState::Ready, outFail)) {
        return false;
    }
    if (loader.state(b) != AssetLoadState::Unknown ||
        loader.take(b, cancelled) ||
        loader.residentBytes() != bytesOf("ff").size()) {
        outFail = "a released in-flight load was resurrected later";
        return false;
    }
    if (!loader.take(follow, cancelled) ||
        cancelled.bytes != bytesOf("ff") ||
        loader.residentBytes() != 0u) {
        outFail = "release results did not drain cleanly";
        return false;
    }
    if (store->gateTimedOut() || store->callerThreadRead()) {
        outFail = "the controlled provider did not run bounded off-thread reads";
        return false;
    }
    return true;
}

bool checkRequestStop(std::string& outFail) {
    const std::shared_ptr<ControlledAssetStore> store =
        std::make_shared<ControlledAssetStore>();
    store->addAsset("ready/r.bin", bytesOf("rrrr"));
    store->addAsset("late/payload.bin", bytesOf("late"));
    store->addAsset("queued/q.bin", bytesOf("qqqq"));

    AssetLoadOptions options;
    options.maxRequests = 3u;
    auto loader = std::make_unique<AsyncAssetLoader>(store, options);
    GateAndThreadCleanup cleanup(store);

    const AssetLoadId ready = loader->request("ready/r.bin");
    if (ready == 0u ||
        !waitForState(*loader, ready, AssetLoadState::Ready, outFail)) {
        return false;
    }
    if (loader->residentBytes() != 4u) {
        outFail = "a Ready result was not retained before the stop";
        return false;
    }

    store->setGateEnabled(true);
    const AssetLoadId active = loader->request("late/payload.bin");
    if (active == 0u || !store->waitForReadsStarted(2u, outFail)) {
        return false;
    }
    const AssetLoadId queued = loader->request("queued/q.bin");
    if (queued == 0u || loader->state(queued) != AssetLoadState::Queued) {
        outFail = "a request behind a paused read was not Queued";
        return false;
    }

    loader->requestStop();

    // Synchronous stop effects, observed while the provider read is still paused.
    if (loader->state(active) != AssetLoadState::Cancelled ||
        loader->state(queued) != AssetLoadState::Cancelled ||
        loader->state(ready) != AssetLoadState::Ready) {
        outFail =
            "requestStop did not cancel pending work while preserving terminals";
        return false;
    }
    std::string error = "unset";
    if (loader->request("ready/r.bin", &error) != 0u || error.empty()) {
        outFail = "a stopped loader admitted a new request";
        return false;
    }
    loader->requestStop();
    loader->requestStop();
    if (loader->state(active) != AssetLoadState::Cancelled ||
        loader->state(queued) != AssetLoadState::Cancelled ||
        loader->state(ready) != AssetLoadState::Ready) {
        outFail = "a repeated requestStop changed terminal states";
        return false;
    }

    AssetLoadResult result;
    if (!loader->take(queued, result) ||
        result.state != AssetLoadState::Cancelled ||
        !result.bytes.empty() ||
        loader->release(queued)) {
        outFail = "a stopped queued request did not drain as Cancelled";
        return false;
    }
    if (!loader->take(active, result) ||
        result.state != AssetLoadState::Cancelled ||
        !result.bytes.empty() ||
        loader->state(active) != AssetLoadState::Unknown ||
        loader->residentBytes() != 4u) {
        outFail = "a stopped in-flight request did not drain as Cancelled";
        return false;
    }
    if (!loader->take(ready, result) ||
        result.state != AssetLoadState::Ready ||
        result.bytes != bytesOf("rrrr") ||
        loader->take(ready, result) ||
        loader->residentBytes() != 0u) {
        outFail = "a Ready result was not drainable after the stop";
        return false;
    }

    // Releasing the paused read reaps the in-flight request; the destructor join
    // then witnesses that the stopped queue can no longer start any read.
    store->openGate();
    if (!store->waitForReadsCompleted(2u, outFail)) {
        return false;
    }
    if (loader->state(active) != AssetLoadState::Unknown ||
        loader->take(active, result)) {
        outFail = "a stopped in-flight request was resurrected";
        return false;
    }
    loader.reset();
    if (store->countReads("queued/q.bin") != 0u ||
        store->readsStarted() != 2u ||
        store->readsCompleted() != 2u) {
        outFail = "stopped work still reached the provider";
        return false;
    }
    if (store->gateTimedOut() || store->callerThreadRead()) {
        outFail = "the controlled provider did not run bounded off-thread reads";
        return false;
    }
    return true;
}

// Destructor join, with requestStop as the synchronous stop boundary and
// condition-variable completion signals for ordering.
bool runDestructionCycle(std::string& outFail) {
    const std::shared_ptr<ControlledAssetStore> store =
        std::make_shared<ControlledAssetStore>();
    store->setGateEnabled(true);
    store->addAsset("gated/a.bin", bytesOf("aaaa"));
    store->addAsset("gated/b.bin", bytesOf("bbbb"));

    // Declaration order is the lifetime contract: the flag mutex and condition
    // variable outlive the joined thread, the thread outlives the guard that
    // joins it, and the guard outlives every early return so the paused read is
    // always released before a loader is destroyed.
    std::mutex flagMutex;
    std::condition_variable flagCv;
    bool destroyed = false;
    std::thread destroyer;

    auto loader = std::make_unique<AsyncAssetLoader>(store);
    GateAndThreadCleanup cleanup(store, destroyer);

    const AssetLoadId active = loader->request("gated/a.bin");
    if (active == 0u || !store->waitForReadsStarted(1u, outFail)) {
        return false;
    }
    const AssetLoadId queued = loader->request("gated/b.bin");
    if (queued == 0u) {
        outFail = "a request was rejected before destruction";
        return false;
    }
    loader->requestStop();

    destroyer = std::thread([&] {
        loader.reset();
        {
            std::lock_guard<std::mutex> lock(flagMutex);
            destroyed = true;
        }
        flagCv.notify_all();
    });

    // Supplementary bounded observation: destruction must not return while the
    // read is paused. Ordering comes from the join below.
    {
        std::unique_lock<std::mutex> lock(flagMutex);
        if (flagCv.wait_for(lock, kNegativeObservation, [&] {
                return destroyed;
            })) {
            outFail =
                "the destructor returned while a provider read was still paused";
            return false;
        }
    }
    if (store->readsStarted() != 1u) {
        outFail = "destruction started a queued provider read";
        return false;
    }
    store->openGate();
    if (!store->waitForReadsCompleted(1u, outFail)) {
        return false;
    }
    destroyer.join();
    if (!destroyed) {
        outFail = "the destructor did not complete for a processable read";
        return false;
    }
    if (store->readsStarted() != 1u || store->readsCompleted() != 1u) {
        outFail = "destruction left provider reads outstanding";
        return false;
    }
    if (store->gateTimedOut() || store->callerThreadRead()) {
        outFail = "the controlled provider did not run bounded off-thread reads";
        return false;
    }
    return true;
}

bool checkDestructionAndProviderLifetime(std::string& outFail) {
    for (int cycle = 0; cycle < 2; ++cycle) {
        if (!runDestructionCycle(outFail)) {
            outFail = "destruction cycle " + std::to_string(cycle + 1) +
                ": " + outFail;
            return false;
        }
    }

    std::shared_ptr<ControlledAssetStore> store =
        std::make_shared<ControlledAssetStore>();
    const std::weak_ptr<ControlledAssetStore> observer = store;
    store->setGateEnabled(true);
    store->addAsset("lifetime/payload.bin", bytesOf("payload"));

    auto loader = std::make_unique<AsyncAssetLoader>(store);
    std::thread destroyer;
    // The guard is constructed after the loader so the gate opens before loader
    // destruction, and after the thread handle so the handle outlives it.
    GateAndThreadCleanup cleanup(store, destroyer);
    const AssetLoadId id = loader->request("lifetime/payload.bin");
    if (id == 0u || !store->waitForReadsStarted(1u, outFail)) {
        return false;
    }
    loader->requestStop();
    // The loader now holds the only strong reference to the provider.
    store.reset();

    destroyer = std::thread([&] { loader.reset(); });
    // Post-thread failure paths must release the paused read before joining.
    const auto failWhileDestroying = [&](std::string message) {
        if (const std::shared_ptr<ControlledAssetStore> alive = observer.lock()) {
            alive->openGate();
        }
        destroyer.join();
        outFail = std::move(message);
        return false;
    };

    std::shared_ptr<ControlledAssetStore> alive = observer.lock();
    if (!alive) {
        return failWhileDestroying(
            "the provider was released with a read still in flight");
    }
    if (alive->readsStarted() != 1u) {
        return failWhileDestroying(
            "a stopped loader started another provider read");
    }
    alive->openGate();
    if (!alive->waitForReadsCompleted(1u, outFail)) {
        return failWhileDestroying(
            "the provider did not complete its in-flight read");
    }
    alive.reset();
    destroyer.join();
    if (!observer.expired()) {
        outFail = "the loader released its provider before the worker exited";
        return false;
    }
    return true;
}

} // namespace

bool test_async_asset_loader_contract(std::string& outFail) {
    if (!checkConstructorAndEmptyBudget(outFail)) {
        return false;
    }
    if (!checkSuccessAndFailure(outFail)) {
        return false;
    }
    if (!checkResidentBudget(outFail)) {
        return false;
    }
    if (!checkCapacityAccounting(outFail)) {
        return false;
    }
    if (!checkQueuedCancellation(outFail)) {
        return false;
    }
    if (!checkQueueChurn(outFail)) {
        return false;
    }
    if (!checkActiveCancellationAndRelease(outFail)) {
        return false;
    }
    if (!checkRequestStop(outFail)) {
        return false;
    }
    if (!checkDestructionAndProviderLifetime(outFail)) {
        return false;
    }
    return true;
}
