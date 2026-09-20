# M1-C: staged archive activation

Date: 2026-09-20
Status: M1-C accepted by Astra; see CHECKPOINT.md for evidence and limits
Coordinator: Astra; implementation: native `astra_flash_builder`.

## Objective, evidence and boundaries

Consume the accepted dependency loader in an engine-owned scene/prefab resource
session. Keep a current archive usable while loading a replacement. Stage only
a complete required graph whose root passes the existing typed archive decoder,
then explicitly activate it. Failed/cancelled/discarded candidates must leave
the active resource unchanged; retained snapshots must survive later switches.

Baseline: clean engine main `b2e03c4ca702589311c7cdac068f8858f01d760e` in
`D:/Projects/Phlosion/PhlosionEngine`. M1-B was committed at the user's request
before this pass. No further worker commit/staging/push is authorized.

M1-A supplies background byte I/O; M1-B supplies required graph validation.
`PhlosionSceneArchive.h/.cpp` already decodes PHSC scenes and PHLO prefabs into
IAssetStore implementations. Those decoders validate their profile/schema,
manifest, identity and embedded-file records. Graph Ready alone does not.
Use fresh candidate stores so a failed decoder can never mutate active content.

Source inspection found no engine application scene/archive call site to
migrate: the actual archive consumers live in game-owned adapters. Application
drives GameLoop, and EditorShell hosts project scene catalogs/plugins. Therefore
this bundle integrates the graph with existing engine archive consumers and
proves the resource transition through public CPU fixtures; it does not claim
a game/editor call-site migration or a renderer/GPU transition. This explicit
resource boundary is chosen over changing project ABI or touching the game.

Read `docs/ENGINE_BOUNDARIES.md`, `docs/ASSET_DEPENDENCY_LOADING.md`,
`docs/VERIFICATION.md`, and relevant sources/tests progressively. No repository
AGENTS.md or parent AGENTS.md was found; the root user's Windows workflow rules
apply. Use native PowerShell, preserve edits, and use existing tooling.

## File ownership

You are not alone in the workspace. Do not revert others' edits; accommodate
them. Astra owns PLAN/CHECKPOINT, docs index and roadmap progress. Flash owns:

- new `src/engine/assets/phlosion/PhlosionArchiveSession.h/.cpp`;
- new `tests/TestPhlosionArchiveSession.cpp`;
- `tests/TestMain.cpp` and `CMakeLists.txt` for registration only;
- new `docs/ARCHIVE_ACTIVATION.md`;
- `docs/VERIFICATION.md` for test scope/count and final evidence;
- `docs/agent-work/cyberpunk-m1-activation/REPORT.md`.

Do not modify the graph/byte loaders, archive formats/decoders, existing tests,
renderer, editor, runtime Application, game repositories, dependencies or ABI.
Raise a concrete blocker to Astra if existing behavior requires scope expansion.
No recursive agents or orchestration-skill invocation by the worker.

## Public contract

Namespace `engine::assets::phlosion`. Use the following surface; ordinary
includes, PImpl and private implementation are worker discretion.

```cpp
enum class ArchiveKind { Scene, Prefab };
enum class ArchiveSessionState { Idle, Loading, Staged, Failed, Cancelled };
struct ArchiveSnapshot {
    ArchiveKind kind = ArchiveKind::Scene;
    AssetGraphResult graph;
    std::shared_ptr<const SceneArchiveStore> scene;
    std::shared_ptr<const PrefabArchiveStore> prefab;
};
class PhlosionArchiveSession {
public:
    explicit PhlosionArchiveSession(std::shared_ptr<const engine::IAssetStore>,
                                   AssetResolver, AssetGraphOptions = {});
    ~PhlosionArchiveSession();
    bool begin(std::string rootAssetId, ArchiveKind,
               std::string* outError = nullptr);
    void pump();
    ArchiveSessionState state() const;
    const std::string& error() const;
    std::shared_ptr<const ArchiveSnapshot> staged() const;
    std::shared_ptr<const ArchiveSnapshot> active() const;
    bool activate();
    bool cancel();
    void discard();
    void unload();
};
```

1. Noncopyable/nonmovable session, all public methods and resolver on one owner
   thread, no concurrent calls/reentrancy. Own and compose one unchanged
   PhlosionAssetGraphLoader. Forward its provider ownership/concurrency rules,
   constructor argument rejection, limits and blocked-I/O destructor caveat.
   No custom worker thread, callbacks or GPU work.
2. Session state describes the candidate, independently of the active snapshot.
   Initial state Idle, active/staged null, error empty. Idle can still have an
   active snapshot after activation or discard.
3. `begin` succeeds only from Idle with nonempty root and known ArchiveKind;
   rejects all other cases with a diagnostic and leaves existing candidate,
   active snapshot and error() unchanged. Success clears candidate error and
   enters Loading by starting the graph. No resolver/I/O/decode during begin.
   Root IDs stay opaque; embedded sceneId/prefabId need not equal registry ID.
4. `pump` is a no-op outside Loading. Call the graph pump once per invocation;
   never spin or wait for I/O. At graph Ready, take the full result, require its
   root to be Phrc, then decode the root with a fresh SceneArchiveStore or
   PrefabArchiveStore as requested. No extension inference or raw fallback.
   Decode occurs on the owner thread and has no frame-time bound. The single
   pump may finish the graph and decode the root in that invocation.
5. Only when both graph and root decode pass, construct a snapshot and enter
   Staged. Its graph is the original Ready result with every required encoded
   asset, validated hash and skipped optional record retained. Exactly one
   typed pointer is nonnull according to kind. Scene/prefab embedded files are
   read through the existing IAssetStore interface; dependency lookup remains
   explicit by ID in snapshot.graph.assets (no implicit merged path namespace).
6. `staged()` returns null until fully Staged. `active()` is never changed by
   begin/pump or any candidate failure. Publish only shared_ptr<const> snapshots
   and typed stores; do not retain a mutable alias that later changes published
   data. A caller may hold a staged snapshot even if the candidate is discarded.
   These handles own the data; dropping a session reference is not forced eviction.
7. Graph/resolver/read/hash/limit failure, root Bytes kind, wrong typed magic or
   schema, invalid manifest/embedded-file record, or ordinary typed-decode
   exception makes the candidate Failed. error() must identify the requested
   root and preserve the underlying diagnostic (including dependency ID when
   supplied). Drop candidate graph/typed payload and expose no staged snapshot.
   Do not claim recovery from process OOM. Do not broaden existing profile
   validation or recursively decode all other PHRC asset classes in this pass.
8. `activate()` succeeds only in Staged. Transfer the staged snapshot pointer to
   active without re-reading/re-decoding or copying payloads, clear staged/error,
   and return candidate state to Idle. This is atomic with respect to the single
   owner-thread API, not a multi-thread/GPU synchronization promise. Previously
   obtained snapshots retain their identity/content and remain readable. In
   all other states activate returns false without changing anything.
9. `cancel()` succeeds only in Loading: cancel/release the graph candidate,
   clear candidate data, enter Cancelled with a root-specific diagnostic.
   Active snapshot is preserved. Never wait for an active synchronous read.
   Other states return false without mutation (use discard for Staged).
10. `discard()` from any state resets only the candidate graph/data/error to
    Idle and preserves active. It must allow a new begin even if a released old
    read is still running; compose graph reset, not terminal requestStop.
11. `unload()` discards the candidate and drops the session's active reference.
    Already returned shared snapshots remain usable after unload or session
    destruction. Destructor still joins the provider reader; snapshots do not
    retain the reader/session/provider unnecessarily.
12. AssetGraphOptions caps apply per candidate graph. Active and staged snapshots
    may coexist; typed archive stores duplicate embedded payloads; consumer-held
    snapshots can outlive the session. These and existing decode/provider/reader
    temporaries mean the cap is not a total residency/peak-memory cap. Document
    this accurately; do not invent global budget, cache or eviction semantics.

## Acceptance and focused verification

Use compact meaningful tests with public in-memory fixtures built by actual
archive/PHRC encoders. Avoid reimplementing every M1-B test or large repeated
case boilerplate. Cover the new transition boundary:

- Real scene and prefab staging and explicit activation, identity (including
  opaque registry alias), embedded exact bytes through IAssetStore, preserved
  required dependency and skipped optional metadata. Include dependency-only
  prefab requiring a scene and raw leaf. No reads for optional targets.
- Activate scene A, begin B: A stays active through Loading and Staged; activate
  B changes active pointer, retained A stays valid. Holding staged B before
  activate must yield the same pointer after activation (no payload copy).
- A graph-valid root with wrong requested archive type, unsupported root
  schema or bad manifest fails typed validation. Include a valid PHRC envelope
  whose file-record contract is invalid, so root decoding is actually exercised.
  Missing/corrupt required content or graph byte-cap failure cannot replace A.
- Failed/Cancelled discard and retry; invalid begin/activate/cancel calls leave
  existing state/error/active/staged intact. Idle/Loading/Staged/Failed/Cancelled
  invariants and constructor failures. A failed begin is not a failed candidate.
- Gate a candidate read while A remains readable, cancel or discard, admit a
  replacement, open gate, finish replacement, and prove old completion cannot
  stage or activate it. Bounded predicate/gate waits with recorded timeouts
  that cancel/reset cannot hide; cleanup guards outlive resources they use.
  Use next-read/session completion as an ordering proof, not elapsed sleeps.
- unload during candidate work and from Staged/Idle; retained handles remain
  usable. After session destruction retain only snapshots, verify data and use
  weak_ptr to show session/active ownership is released when callers drop them.
  Preserve provider lifetime until reader destruction; avoid a teardown hang.

Integration is a CPU resource contract. Do not claim the game/editor now uses
this session or that M1/rendered readiness is complete. No rendering path is
changed, so no GPU run is required for this bundle. Next rendered work must
qualify OpenGL, Vulkan and Direct3D 12, including editor where affected.

Build `cmake --build build-presentation --config Debug --parallel 4` (existing
VS2026 editor/tests/WX configuration), then
`ctest --test-dir build-presentation -C Debug --output-on-failure`. Save noisy
logs under ignored `artifacts/m1-c/`, preserve exit codes and report failures.
Run final whitespace checks including untracked additions and doc-link checks.
No routine repeat/mutation campaign after checks pass unless a failure warrants
it. Report changed paths, behavior, exact results and limitations in REPORT.
Budget about 25 minutes, including internal discovery/test/debug. Checkpoint if
interrupted or blocked; only Astra accepts the final patch.
