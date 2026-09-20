# Asynchronous Asset Loading

Status: Implemented CPU contract (M1-A)
Last updated: 2026-09-20

## Goal

`engine::assets::AsyncAssetLoader` moves whole-asset byte reads off the caller
that requests and polls them, without introducing a job system, a cache or a
dependency resolver. It is renderer independent: the same CPU contract runs
with no graphics context and no game checkout. Decoding, model creation and GPU
uploads stay on the consuming thread.

The implementation is
[AsyncAssetLoader.h](../src/engine/assets/AsyncAssetLoader.h) and
[AsyncAssetLoader.cpp](../src/engine/assets/AsyncAssetLoader.cpp).

## Service Model

One loader owns one background worker over a caller-supplied `IAssetStore`.
Public operations are synchronized; the state lock is never held across
provider I/O and reads never execute on the requesting or polling thread. No
callbacks run on the worker.

The loader holds its provider alive by shared ownership and serializes its own
provider reads. A caller must not mutate the provider while the loader is alive
and must not use a non-thread-safe provider outside this loader. `const` on the
provider interface does not by itself make an arbitrary provider safe for
concurrent use. Destruction requires that no other thread is inside a public
loader call.

## Requests

```cpp
engine::assets::AsyncAssetLoader loader(store);
std::string error;
const engine::assets::AssetLoadId id = loader.request("scenes/demo.phscene", &error);

// Later, on the consuming thread, once state(id) reports a terminal state.
engine::assets::AssetLoadResult result;
if (loader.take(id, result)) {
    // result.state, result.bytes and result.error are owned by the caller now.
}
```

`request` accepts a nonempty virtual path verbatim under the provider's path
rules. An empty path, a full request capacity or exhausted IDs return `0` with a
diagnostic. There is no deduplication, caching or dependency resolution; two
requests for the same path perform two provider reads.

| State | Meaning |
| --- | --- |
| `Unknown` | Never admitted by this loader, or already taken/released |
| `Queued` | Admitted, waiting for the worker; no provider call has happened |
| `Loading` | The worker is inside the provider read or publishing its result |
| `Ready` | Bytes were read successfully and are retained until `take`/`release` |
| `Failed` | The provider returned false, threw, or the payload exceeded the byte budget |
| `Cancelled` | Cancelled by the caller while queued or in flight |

IDs increase monotonically and are never reused, including after `take` and
`release`, so a stale ID can never address a later request. A successful
admission becomes `Queued` then `Loading` then `Ready` or `Failed`. An empty
successful asset is valid. Provider exceptions are caught and reported as
`Failed` with a diagnostic; partial provider output never leaks into a result.

`cancel` succeeds only for `Queued` or `Loading`. It makes the status
`Cancelled` immediately. Queued work never reaches the provider. A read that has
already started may run to completion, but it can never publish a success or
error afterwards. `take` moves a terminal result out of the loader and frees its
capacity slot and retained bytes; nonterminal and unknown IDs return false
without modifying the output. `release` discards a known request, cancels any
queued or in-flight publication, frees retained bytes and returns the slot.

Cancelling does not by itself free the capacity slot: every admitted, unreleased
request occupies one, including a terminal `Cancelled` or `Failed` result, until
`take` or `release` returns it.

## Stopping

`requestStop()` is the explicit lifecycle boundary. It stops the loader without
destroying it: queued work is dropped, every `Queued` or `Loading` request becomes
`Cancelled`, and late publications are suppressed, while results that are
already `Ready`, `Failed` or `Cancelled` stay available to `take` and `release`
with their byte accounting intact. Requests admitted after a stop are rejected
with `0` and a diagnostic.

The call is idempotent and nonblocking. It never joins the worker, so a read that
already started may still be running when it returns; it can no longer publish,
and only the worker finishing that read or the destructor's join reaps it.
`take` and `release` remove the handle and its result, not the running read.

```cpp
loader.requestStop();          // stop accepting and publishing work
loader.take(id, result);       // drains results that were already terminal
// ... destructor joins the worker here when the loader goes out of scope.
```

## Retained Byte Budget

`AssetLoadOptions::maxResidentBytes` bounds the logical bytes of `Ready` results
that the loader still owns; `residentBytes()` reports the current total and a
zero budget is valid for empty assets. This is a retained-payload budget, not a
hard peak or process memory limit. Outside it are the provider's own temporary
vector during the read, allocator capacity, provider caches, request metadata,
and the bytes after they are transferred to a consumer. Publishing beyond the
available budget fails the request with a diagnostic and retains nothing, using
overflow-safe accounting. Streaming or ranged I/O guarantees are not part of
this interface; `IAssetStore` reads whole vectors.

`AssetLoadOptions::maxRequests` bounds admitted, unreleased requests, including
terminal results that have not been taken or released yet.

## Destruction

The destructor applies the same stop path as `requestStop()` and then joins the
worker before provider and state are destroyed. It can wait for the current
synchronous read to finish; it cannot interrupt a blocked OS or provider read.

## Verification

[TestAsyncAssetLoader.cpp](../tests/TestAsyncAssetLoader.cpp) covers admission
and constructor failures, ID validity, success including zero bytes, missing
providers, failing providers that write partial bytes, throwing providers,
recovery, capacity that includes terminal results, the retained byte budget,
queued cancellation without provider I/O, cancellation and release during an
active read, `requestStop` semantics, and destruction that joins the worker while
keeping the provider alive.

The tests drive a gated synthetic provider through condition variables. Every
wait is bounded and reports a diagnostic instead of hanging, and cleanup guards
release a paused read before a loader can be destroyed. Ordering claims come from
those signals, from joined threads, from the single worker's queue order, or from
`requestStop` as a synchronous stop boundary: for example, a later sentinel
request reaching `Ready` proves that an earlier read and its publication decision
already finished. The provider also records the calling thread id, so a read on
the requesting thread fails the contract.

[TestPublicAssetFixture.cpp](../tests/TestPublicAssetFixture.cpp) generates
synthetic scene and prefab archives in memory with the existing PHRC encoders,
exposes them through an immutable in-memory `IAssetStore`, loads the archive
bytes through the loader, and decodes them on the caller with
`SceneArchiveStore` and `PrefabArchiveStore`. It checks exact identity, embedded
content, reload after release, missing content and corrupt archive rejection. The
fixture uses generic identities and contains no game or private content, and it
makes no GPU or material correctness claim.

See [VERIFICATION.md](VERIFICATION.md) for the current suite scope.

## Deferred Work

This iteration ships the byte-loading service and its CPU fixture only. Still
out of scope: migrating production callers, PHRC dependency resolution,
renderer uploads, streaming or ranged reads, cancellation of blocked provider
I/O, and the rendered M1 fixture. `AssetLoadOptions` values for hardware and game
presentation budgets remain open.
