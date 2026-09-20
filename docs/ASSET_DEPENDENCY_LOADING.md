# Dependency-aware asset loading

Status: Implemented CPU contract (M1-B)
Last updated: 2026-09-20

## Goal

`engine::assets::phlosion::PhlosionAssetGraphLoader` loads one root asset and
its required transitive dependencies through the existing asynchronous byte
reader, verifies container integrity and dependency hashes, and publishes the
complete closure or nothing. It is renderer independent: the same CPU contract
runs with no graphics context and no game checkout. Production scene
integration, asset cooking and renderer uploads stay with the consuming
project.

The implementation is
[PhlosionAssetGraphLoader.h](../src/engine/assets/phlosion/PhlosionAssetGraphLoader.h)
and
[PhlosionAssetGraphLoader.cpp](../src/engine/assets/phlosion/PhlosionAssetGraphLoader.cpp).
It builds on
[AsyncAssetLoader](ASYNC_ASSET_LOADING.md) for byte reads and on the
[PHRC resource container](../src/engine/assets/phlosion/PhlosionResourceContainer.h)
for document decode and hashing.

## Session model

```cpp
engine::assets::phlosion::PhlosionAssetGraphLoader loader(store, resolver);

loader.start("prefabs/crate-a");   // Idle -> Loading

// One pump per caller update (frame, tick or editor refresh). pump() never
// waits for provider I/O; the session needs more updates until it is terminal.
loader.pump();

// Consume only terminal results, and only treat Ready as usable content.
engine::assets::phlosion::AssetGraphResult result;
switch (loader.state()) {
case engine::assets::phlosion::AssetGraphState::Loading:
    break;                         // pump again on the next caller update
case engine::assets::phlosion::AssetGraphState::Ready:
    if (loader.take(result)) {
        // result.assets is owned by the caller now.
    }
    break;
case engine::assets::phlosion::AssetGraphState::Failed:
case engine::assets::phlosion::AssetGraphState::Cancelled:
    if (loader.take(result)) {
        // Report result.rootAssetId and result.error. A failed or cancelled
        // result carries no assets and no skipped optional edges.
    }
    break;
case engine::assets::phlosion::AssetGraphState::Idle:
    break;
}
```

Call `pump` from the caller's regular updates. Each call consumes at most one
completed read; decoding has no frame-time guarantee. A session left in
`Loading` needs another caller update.

The session owns one `AsyncAssetLoader` configured for a single admitted
request and the graph's encoded-byte cap. Only provider reads run on that
background worker. Every session method, and the resolver, runs on one owner
thread; the resolver must not reenter the same session. The provider
lifetime and destruction rules from [asynchronous asset loading](ASYNC_ASSET_LOADING.md)
apply unchanged.

| State | Meaning |
| --- | --- |
| `Idle` | No session. The only state that accepts `start`. |
| `Loading` | Resolving, reading, decoding or traversing the required closure. |
| `Ready` | The full required closure, every hash constraint and the cycle checks passed. |
| `Failed` | An asset-specific error ended the session; no assets escape. |
| `Cancelled` | `cancel()` ended the session in `Loading`; no assets escape. |

`start` admits a nonempty root asset ID from `Idle` only and enters `Loading`
without resolving or reading anything; the first `pump` begins that work. An
empty ID or any other state is rejected with a diagnostic and leaves an
existing session untouched.

`pump` never waits for I/O. In one call it resolves or schedules work, or it
consumes at most one completed read, decodes it on the calling thread and may
schedule the next read. Decoding and traversal are not frame-time bounded.
Outside `Loading` it is a no-op.

Asset IDs are opaque. The resolver maps an ID to an
`AssetLocation` (virtual path plus `AssetKind`); the session never infers a path
from an ID and never inspects a filename extension. Each required unique ID is
resolved and read once per session, so shared dependencies (diamonds) load
once. There is no cross-session cache, deduplication or hot reload.

## Kinds, hashes and required edges

- `AssetKind::Phrc` is decoded with the existing PHRC decoder, which validates
  container version, section ranges and chunk integrity. The asset hash is the
  validated `Document.contentHash`; dependency constraints are compared against
  that document hash, never against a whole-file hash.
- `AssetKind::Bytes` is an opaque leaf. Its hash is `phrc::contentHash64` over
  the entire payload and it contributes no edges.
- Only dependencies with `phrc::kDependencyRequired` are traversed. Flags
  outside that bit are rejected with a diagnostic before anything is read for
  that edge.
- An edge without the required bit is optional: its target is never resolved or
  read, and the parent ID plus the original dependency record is reported in
  `AssetGraphResult::skippedOptionalDependencies`. This holds even when another
  required edge loads the same ID, and such an edge imposes no hash constraint.
- A nonzero `expectedContentHash` on a required edge must equal the target's
  hash under its resolved kind. Zero means unspecified. Constraints are checked
  when the target loads, when the target is already loaded and a later edge adds
  a constraint, and across parents: conflicting nonzero constraints fail. Hash
  diagnostics name the parent and target asset IDs.
- Required cycles, including a self-cycle, fail before `Ready`. Optional edges
  take no part in cycle detection. Traversal and cycle detection are bounded and
  iterative, so raising `maxAssets` does not build a native recursion stack.

`Ready` means the required closure is complete, hashes match and no required
cycle exists. It does not imply typed-profile or schema validation, embedded
material, model or JSON semantics, or that a consumer will accept the content.

## Limits

`AssetGraphOptions` bounds three things, all checked without overflow and before
a new payload is retained. Exceeding any of them fails the whole graph and
releases every staged payload.

| Option | Counts | Zero |
| --- | --- | --- |
| `maxAssets` | Unique required assets, including the root | Rejected at construction |
| `maxDependencyEdges` | Every encountered dependency record, including duplicate and optional records | Valid |
| `maxEncodedBytes` | Cumulative retained encoded payload bytes for the session | Valid |

The encoded-byte cap is a retained accounting budget, not a peak-memory bound.
Outside it are the provider's temporary read buffer, the `AsyncAssetLoader`
completed-read staging, decoder copies and allocations, provider caches, graph
metadata, vector capacity, and any bytes after they are transferred by `take`.
The decoder can allocate from a document before the graph inspects its edge
limit. Streaming or ranged reads are not part of this interface.

`loadedBytes()` reports the accepted encoded payload bytes this session still
owns: the retained closure while `Ready`, and zero once the result is taken,
released, failed or cancelled.

## Termination, cancellation and reuse

- A terminal result is published only through `take`, which succeeds for
  `Ready`, `Failed` or `Cancelled`, moves the result out and returns the session
  to `Idle`. Otherwise it returns false and leaves the output unchanged. A
  `Ready` result carries the asset map and the skipped optional edges; a failed
  or cancelled result carries the root ID and the error and nothing else.
- `cancel` succeeds only in `Loading`. It becomes `Cancelled` immediately,
  releases the underlying request and staged data, and suppresses every later
  publication. It does not interrupt synchronous provider I/O, so a read that
  already started may still be running when it returns.
- `reset` works from any state, releases active or staged data and returns to
  `Idle`. It never calls the reader's terminal `requestStop`, so a later `start`
  is admitted even while a released read is still in flight; a released read can
  never publish into the new session.
- The destructor joins the background reader and can therefore wait for a read
  the provider has already started. It cannot interrupt a blocked provider.

## Verification

[TestPhlosionAssetGraphLoader.cpp](../tests/TestPhlosionAssetGraphLoader.cpp)
drives public synthetic fixtures built with the real PHRC and archive encoders
and an in-memory `IAssetStore`. It covers:

- root `Phrc`, non-empty `Bytes` and empty `Bytes` success, exact
  `loadedBytes()` accounting and the `take` output contract;
- multi-level closure, diamond deduplication by resolver and read counts, hash
  success for both kinds, zero unspecified hashes, a later constraint on an
  already-loaded node, a late constraint on a shared node, conflicting
  constraints, an ordinary mismatch, and rejection of a whole-file hash used
  where a PHRC document hash belongs;
- optional edges recorded without resolver or provider calls, an optional edge
  to a shared loaded node that imposes no constraint, an ignored optional cycle,
  and rejected unknown flag bits on required and optional edges alike;
- self-cycles, longer required cycles and a cycle whose closing edge appears at
  a node decoded after the rest of the cycle;
- missing mapping, resolver exception, empty resolved path, invalid
  `AssetKind`, provider failure, provider exception, corrupt and truncated PHRC
  payloads, each with an asset-specific diagnostic and no partial output,
  including required-child failures that happen after a valid root was already
  staged: the failed result keeps the root and the child diagnostic and drops
  the staged payloads, the recorded optional edges and the byte accounting;
- constructor rejection, `start` admission rules, `take`/`cancel` contract
  violations, reset from `Loading` and from `Ready`, and recovery after a
  failure;
- the asset, edge and encoded-byte limits including equality acceptance,
  duplicate and optional edge records, and valid zero edge/byte limits;
- gated in-flight cancellation and reset/reuse, where the released read finishes
  at the provider before the new session completes and still never appears in
  the new result;
- a dependency-only prefab archive that requires a real scene archive, which in
  turn requires a raw asset, decoded afterwards with `PrefabArchiveStore` and
  `SceneArchiveStore` to assert identities, metadata and exact embedded content.

The tests are CPU only. Every wait is bounded, a gate that hits its bound is
recorded at the provider and asserted after the session finishes (so a timing
claim cannot be silently satisfied by a timeout), and a cleanup guard opens the
provider gate before a loader can be destroyed, so a paused read is always
released before the reader joins. No GPU, rendering or visual-parity claim is
made by this document.

See [VERIFICATION.md](VERIFICATION.md) for the current suite scope.

## Deferred work

Still out of scope for this iteration: production scene or prefab integration,
typed-profile and schema validation, embedded model, material or JSON semantics,
cross-session caching and hot reload, streaming or ranged reads, cancellation of
blocked provider I/O, and the rendered M1 fixture. `AssetGraphOptions` values for
hardware and presentation budgets remain open.
