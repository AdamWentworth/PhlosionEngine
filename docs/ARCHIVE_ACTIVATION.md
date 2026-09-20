# Staged archive activation

Status: Implemented CPU contract (M1-C)
Last updated: 2026-09-20

## Goal

`engine::assets::phlosion::PhlosionArchiveSession` keeps one scene or prefab
archive usable while a replacement is loaded. It stages only a complete
required graph whose root also passes the existing typed archive decoder, and
activation is explicit: a failed, cancelled or discarded candidate never
changes the active archive.

The implementation is
[PhlosionArchiveSession.h](../src/engine/assets/phlosion/PhlosionArchiveSession.h)
and
[PhlosionArchiveSession.cpp](../src/engine/assets/phlosion/PhlosionArchiveSession.cpp).
It composes one unchanged
[PhlosionAssetGraphLoader](ASSET_DEPENDENCY_LOADING.md) for required-closure
loading and the existing `SceneArchiveStore` and `PrefabArchiveStore` from
[PhlosionSceneArchive.h](../src/engine/assets/phlosion/PhlosionSceneArchive.h)
for typed decode. It is renderer independent: the same CPU contract runs with
no graphics context and no game checkout.

## Session model

```cpp
engine::assets::phlosion::PhlosionArchiveSession session(store, resolver);

session.begin("registry/scene-a", engine::assets::phlosion::ArchiveKind::Scene);

// One pump per caller update. pump() never waits for provider I/O.
session.pump();

if (session.state() == engine::assets::phlosion::ArchiveSessionState::Staged) {
    const std::shared_ptr<const engine::assets::phlosion::ArchiveSnapshot>
        staged = session.staged();
    if (session.activate()) {
        // staged is now active, and keeps the same identity and content.
    }
}
```

The session state describes the candidate, independently of the active
snapshot. An idle session can still hold an active snapshot after activation or
discard.

| State | Meaning |
| --- | --- |
| `Idle` | No candidate. The only state that accepts `begin`. |
| `Loading` | The candidate graph is resolving, reading or decoding. |
| `Staged` | Graph, hashes and the typed root decode all passed. |
| `Failed` | A candidate graph or typed decode failed; no staged snapshot. |
| `Cancelled` | `cancel()` ended a `Loading` candidate. |

`begin` admits a nonempty root ID and a known `ArchiveKind` from `Idle` only. It
enters `Loading` without resolving, reading or decoding anything; the first
`pump` starts that work. Any other call is rejected with a diagnostic and
leaves the candidate, the active snapshot and `error()` unchanged; a rejected
begin is not a failed candidate.

`pump` is a no-op outside `Loading`. It calls the graph pump once per
invocation, never spins or waits for I/O, and when the graph becomes `Ready` it
decodes the root in the same invocation. The decode runs on the owner thread
and has no frame-time bound.

Only a graph that is `Ready` and a root that decodes becomes `Staged`. The
staged snapshot keeps the original `Ready` result with every required encoded
asset, validated hash and skipped optional record, plus exactly one non-null
typed store. Scene and prefab embedded files are read through the existing
`IAssetStore` interface; dependency lookup stays explicit by ID in
`snapshot.graph.assets`, with no merged path namespace.

## Activation, cancellation and reuse

- `staged()` is non-null only in `Staged`. `active()` is never changed by
  `begin`, `pump` or any candidate failure.
- `activate` succeeds only in `Staged`. It moves the staged snapshot pointer to
  `active` without re-reading, re-decoding or copying payloads, clears the
  candidate and returns to `Idle`. Previously obtained snapshots keep their
  identity and content. In every other state it returns false and changes
  nothing.
- `cancel` succeeds only in `Loading`, releases the graph candidate and enters
  `Cancelled` with a root-specific diagnostic. The active snapshot is
  preserved and a synchronous read that already started is never waited for.
- `discard` works from any state, drops only the candidate graph, data and
  error, and returns to `Idle` while preserving `active`. It composes the graph
  reset path, so a new `begin` is admitted even while a released read is still
  running.
- `unload` discards the candidate and drops the session's active reference.
  Snapshots already returned to callers stay usable after `unload` or session
  destruction.

A graph or typed-decode failure makes the candidate `Failed`: a resolver, read,
hash, limit, root-kind or archive contract error drops the candidate payloads
and exposes no staged snapshot. The diagnostic names the requested root and
keeps the underlying graph or decoder message, including the dependency ID when
one was supplied. Process-level allocation failure is not a recoverable
candidate outcome, and other PHRC asset classes are not recursively decoded by
this session.

## Limits and ownership

`AssetGraphOptions` caps apply per candidate graph, exactly as documented for
the graph loader: `maxAssets`, `maxDependencyEdges` and `maxEncodedBytes` are
per-candidate retained-accounting budgets. The cap is not a total residency or
peak-memory bound. An active and a staged snapshot can coexist, a typed archive
store duplicates the embedded payloads it decoded, consumer-held snapshots can
outlive the session, and provider reads, decoder allocations and reader staging
stay outside the cap. There is no global budget, cache or eviction policy in
this iteration.

The session is noncopyable and nonmovable. Every method, the resolver and the
decode run on one owner thread; no call may overlap or reenter the session from
the resolver. Provider reads run on the graph loader's single background
worker, and the loader's provider ownership, concurrency, constructor
rejection and blocked-I/O destructor rules apply unchanged. The destructor
joins that reader, so it can wait for a read the provider already started and
cannot interrupt a blocked provider; published snapshots do not retain the
reader, the session or the provider.

## Boundary

This bundle integrates the accepted dependency graph with the engine's existing
typed archive stores and proves the resource transition through public CPU
fixtures. The engine has no application scene/archive call site to migrate:
`Application` drives `GameLoop`, the editor hosts project scene catalogs and
plugins, and the actual archive consumers live in game-owned adapters. Project
call-site migration, cooking, and any renderer or GPU upload remain separate
work.

## Verification

[TestPhlosionArchiveSession.cpp](../tests/TestPhlosionArchiveSession.cpp)
drives public in-memory fixtures built with the real PHRC and archive encoders.
It covers:

- real scene and prefab staging with explicit activation, an opaque registry
  alias that differs from the embedded scene ID, exact embedded bytes through
  `IAssetStore`, a preserved required dependency with its validated hash and a
  skipped optional record with no resolver or provider call;
- a prefab requiring a real scene archive that in turn requires a raw leaf, read
  in dependency order, both with an embedded file and with no embedded file at
  all (dependency-only), staged and activated either way;
- scene A active while scene B loads and stages, pointer-identity switch on
  activation, and a retained snapshot that stays readable afterwards;
- wrong requested archive type, unsupported root schema, a valid PHRC envelope
  with an invalid file record, a root resolved as an opaque byte leaf, missing
  required content, a corrupt required dependency whose stored bytes fail the
  required hash, and a graph byte-cap failure, each leaving the active snapshot
  and exposing no staged snapshot while the diagnostic names the requested root
  and the failing dependency;
- failed and cancelled discard, retry after failure, and invalid
  `begin`/`activate`/`cancel` calls that leave the state, error and snapshots
  intact, plus the forwarded constructor rejections;
- a gated in-flight read while the active archive stays readable, cancel and
  discard, a replacement admitted while the released read is still blocked, and
  a late completion that cannot stage or activate, with recorded bounded gate
  waits, count-based waits that cannot accept an earlier read of the same path,
  and a gate cleanup guard declared after the session so it always opens the
  gate before the session destructor joins the reader;
- `unload` from `Idle`, `Staged` and during candidate work, snapshots that
  outlive the session, and two `weak_ptr` checks: dropping the caller's handle
  releases the snapshot, and dropping every external provider reference keeps
  the provider alive through the session reader, then releases it after session
  destruction while a retained snapshot stays readable.

The tests are CPU only. Every wait is bounded, a gate that hits its bound is
recorded at the provider and asserted after the session finishes, and the gate
cleanup guard is declared after the session so a paused read is always released
before the reader joins. No GPU, rendering or visual-parity claim is made by
this document, and no engine test needs a graphics context.

See [VERIFICATION.md](VERIFICATION.md) for the current suite scope and the
dated evidence record.

## Deferred work

Still out of scope for this iteration: game or editor call-site migration,
production scene/prefab cooking, renderer or GPU resource transitions,
cross-session caching and hot reload, streaming or ranged reads, typed-profile
validation beyond the existing archive decoders, cancellation of blocked
provider I/O, and any global residency budget.
