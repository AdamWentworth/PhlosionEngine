// src/engine/assets/AsyncAssetLoader.h
#pragma once

#include "engine/core/IAssetStore.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine::assets {

// Identifies one load request owned by a loader. 0 is never a valid ID and a
// loader never reuses an ID, including after take() or release().
using AssetLoadId = std::uint64_t;

enum class AssetLoadState {
    Unknown,
    Queued,
    Loading,
    Ready,
    Failed,
    Cancelled,
};

struct AssetLoadOptions {
    // Maximum admitted, unreleased requests. Terminal results keep their slot
    // until take() or release() frees it.
    std::size_t maxRequests = 64u;
    // Retained-payload budget for Ready results that are still owned by the
    // loader. Provider temporary reads, allocation capacity, provider caches,
    // request metadata and consumer copies are outside this budget; it is not a
    // hard peak-memory limit.
    std::size_t maxResidentBytes = 64u * 1024u * 1024u;
};

struct AssetLoadResult {
    AssetLoadState state = AssetLoadState::Unknown;
    std::vector<std::uint8_t> bytes;
    std::string error;
};

// Renderer-independent asynchronous byte loading over an existing IAssetStore.
//
// One background worker serializes this loader's provider reads, so reads never
// run on the request/poll caller and the state lock is not held during provider
// I/O. The loader holds the provider alive by shared ownership; a caller must
// not mutate the provider or access a non-thread-safe provider outside this
// loader while it is alive. Decoding, model creation and uploads stay on the
// consuming thread and are not made thread-safe here.
//
// Destruction requires no concurrent public calls, drops queued work, suppresses
// publication, and joins the active read before provider and state are destroyed.
class AsyncAssetLoader {
public:
    explicit AsyncAssetLoader(
        std::shared_ptr<const engine::IAssetStore> store,
        AssetLoadOptions options = {});

    ~AsyncAssetLoader();

    AsyncAssetLoader(const AsyncAssetLoader&) = delete;
    AsyncAssetLoader& operator=(const AsyncAssetLoader&) = delete;
    AsyncAssetLoader(AsyncAssetLoader&&) = delete;
    AsyncAssetLoader& operator=(AsyncAssetLoader&&) = delete;

    // Admits a nonempty virtual path verbatim under the provider's path rules.
    // Returns 0 on an empty path, full request capacity or exhausted IDs and
    // writes a diagnostic to outError when provided.
    AssetLoadId request(std::string virtualPath, std::string* outError = nullptr);

    AssetLoadState state(AssetLoadId id) const;

    // Succeeds only for Queued or Loading requests. Queued work never reaches the
    // provider; a started synchronous read may finish but can no longer publish.
    bool cancel(AssetLoadId id);

    // Moves a terminal result out and frees its slot and resident accounting.
    // Nonterminal and unknown IDs return false without modifying outResult.
    bool take(AssetLoadId id, AssetLoadResult& outResult);

    // Discards a known request, cancelling queued/in-flight publication and
    // freeing retained bytes and the capacity slot. Unknown IDs return false.
    bool release(AssetLoadId id);

    // Explicit lifecycle boundary that stops the loader without destroying it:
    // queued work is dropped, every Queued or Loading request becomes Cancelled,
    // and later requests are rejected with 0 and a diagnostic. Already
    // Ready/Failed/Cancelled results stay available to take()/release() with
    // their accounting intact. Idempotent and nonblocking; it never joins the
    // worker, so a read that already started may still be running here. Only that
    // worker finishing the read or the destructor's join reaps such a read;
    // take()/release() only remove the handle and its result.
    void requestStop();

    // Logical bytes of Ready results still owned by this loader.
    std::size_t residentBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::assets
