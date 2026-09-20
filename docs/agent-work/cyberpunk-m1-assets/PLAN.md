# M1-A: asynchronous asset bytes and public CPU fixture

Date: 2026-09-20
Status: M1-A accepted by Astra; see CHECKPOINT.md for verification and limits
Coordinator: Astra; implementation: native `astra_flash_builder`.

## Objective and relationship to the roadmap

Implement the first coherent M1 iteration: a reusable, renderer-independent
asynchronous byte-loading service over the existing `IAssetStore`, demonstrated
by public synthetic scene/prefab archive content. The same CPU contract runs
without a graphics context or a game checkout. This is M1-A, not completion of
M1's rendered fixture or private character roster. No production caller is
migrated yet; PHRC dependency resolution and renderer uploads are later bundles.

M0 provisional decisions for this bundle: existing Windows/C++20 toolchain,
source integration, all three renderer support unchanged, no new dependencies.
Hardware and game presentation budgets remain open and do not affect this
standalone CPU contract. Reuse the existing PHRC/scene archive APIs and formats.

## Baseline and file ownership

Repo: `D:/Projects/Phlosion/PhlosionEngine`, main at
`c17d2122bd36b3c3bb49c4e703900f40cccfbd76`.
Pre-existing changes: one docs/README.md roadmap row, untracked
docs/CYBERPUNK_ENGINE_READINESS.md and docs/agent-work/cyberpunk-readiness/*.
Preserve all. The assigned existing code/build/test files are clean relative to
HEAD. No staged changes. Astra owns this PLAN and CHECKPOINT, the prior audit,
and documentation-index integration.

Flash owns only:
- `src/engine/assets/AsyncAssetLoader.h` and `.cpp` (new);
- `tests/TestAsyncAssetLoader.cpp` and `tests/TestPublicAssetFixture.cpp` (new);
- `tests/TestMain.cpp` (register the new contracts);
- `CMakeLists.txt` (add new core source/tests and standard thread linkage if needed);
- `docs/ASYNC_ASSET_LOADING.md` (new), `docs/VERIFICATION.md` (test scope/count);
- `docs/agent-work/cyberpunk-m1-assets/REPORT.md` (worker evidence).

Do not modify IAssetStore or existing resource/archive formats, renderers,
application/editor paths, dependency manifests, game repositories, or other
workers' changes. No commits, staging, pushes, recursive agents, source resets,
or private asset access. Ask Astra about any material contract expansion.

## Public contract (Astra architecture decision)

Namespace `engine::assets`. Use these names and semantics (ordinary include,
PImpl and constructor declaration details are implementation discretion):

```cpp
using AssetLoadId = std::uint64_t; // 0 is invalid, IDs are never reused
enum class AssetLoadState { Unknown, Queued, Loading, Ready, Failed, Cancelled };
struct AssetLoadOptions {
    std::size_t maxRequests = 64;
    std::size_t maxResidentBytes = 64 * 1024 * 1024;
};
struct AssetLoadResult {
    AssetLoadState state = AssetLoadState::Unknown;
    std::vector<std::uint8_t> bytes;
    std::string error;
};
class AsyncAssetLoader {
public:
    explicit AsyncAssetLoader(std::shared_ptr<const engine::IAssetStore> store,
                              AssetLoadOptions options = {});
    ~AsyncAssetLoader();
    AssetLoadId request(std::string virtualPath, std::string* outError = nullptr);
    AssetLoadState state(AssetLoadId id) const;
    bool cancel(AssetLoadId id);
    bool take(AssetLoadId id, AssetLoadResult& outResult);
    bool release(AssetLoadId id);
    void requestStop(); // nonblocking, idempotent; rejects further requests
    std::size_t residentBytes() const;
};
```

- Service is noncopyable/nonmovable and owns one background worker. All public
  operations are synchronized; destruction requires no concurrent public calls.
  Reads never execute on the request/poll caller and the state mutex is not held
  during provider I/O. No callbacks run on the worker. Decode/model/GPU operations
  stay on the consuming thread and are not silently made thread-safe.
- Loader holds its provider alive by shared ownership. It serializes its own
  provider reads. Caller must not mutate the provider or concurrently access a
  non-thread-safe provider outside this loader. Existing loaded archive stores
  may be used immutably. Do not imply `const` guarantees arbitrary provider safety.
- `request`: accepts a nonempty virtual path verbatim under the provider's path
  rules; empty path/full capacity/ID exhaustion reject with 0 and a diagnostic.
  Capacity covers every admitted, unreleased request, including terminal results.
  IDs monotonically increase and must not wrap/reuse. No dedup/cache/resolver.
- Successful admission becomes Queued -> Loading -> Ready or Failed. Failure
  includes provider false or an exception; partial provider bytes must not leak
  into failed results. An empty successful asset is valid. Catch read exceptions
  so they cannot terminate the process; do not claim recovery from process OOM.
- `cancel` succeeds only for Queued/Loading. It makes status Cancelled immediately;
  queued work must not call the provider, while an already started synchronous
  read may finish but can never publish success/error after cancellation.
  Ready/Failed/Cancelled/Unknown cancellation returns false.
- `take` moves a terminal result out and releases its slot and resident accounting.
  Nonterminal/unknown returns false without modifying the output. Released IDs
  become Unknown. Taking a Cancelled in-flight result must not resurrect it later.
- `release` discards any known request, cancelling queued/in-flight publication,
  removes retained bytes and returns its capacity. Unknown returns false. Avoid
  accumulating stale queued jobs under repeated request/cancel/release cycles.
- `residentBytes` counts logical bytes of Ready results still owned by the loader.
  Publishing a result exceeding available `maxResidentBytes` fails with diagnostic
  and no retained bytes; use overflow-safe accounting. `take`/`release` free budget.
  A zero byte budget is valid for empty assets. Zero maxRequests/null provider
  are invalid constructor arguments (`std::invalid_argument`).
- This is a retained-payload budget, NOT a hard peak/process-memory limit.
  IAssetStore reads whole vectors: one temporary provider read, allocation capacity,
  provider caches, request metadata and transferred consumer bytes are outside it.
  Record that limitation clearly. Do not invent streaming/ranged I/O guarantees.
- Destructor drops queued work, suppresses publication, requests stop, and joins
  the active reader before destroying state/provider. It can wait for the current
  synchronous read; there is no claim it can interrupt blocked OS/provider I/O.
- Review amendment: expose `requestStop()` as the explicit lifecycle boundary.
  Under the state lock it closes admission, removes queued work, marks all
  Queued/Loading requests Cancelled, and suppresses their late publication.
  Ready/Failed/already-Cancelled results remain available to take/release with
  ordinary slot/accounting rules. Stop is terminal and idempotent. No joining in
  requestStop; destructor invokes this same implementation before joining.
  This supports deterministic shutdown coordination and future scene lifecycle
  integration without a test-only callback or wall-clock ordering assumption.

One worker with explicit handoff is chosen over a job graph/cache rewrite to
avoid concurrency in unknown render/model code and keep ownership reviewable.
Future dependency resolution can compose requests without changing archive ABI.

## Required verification and fixture

1. Deterministic controllable test store: use condition variables/latches, not
   arbitrary sleeps, to pause a read and prove request/state/cancel remain usable.
   Bounded waits/timeouts must fail clearly rather than hang CI.
2. Success (including zero bytes), missing asset, throwing provider, partial failure,
   recovery on a subsequent request; constructor/admission failures; ID validity;
   output preservation on unsuccessful take; terminal cancellation semantics.
3. Queued cancellation never invokes I/O; active cancellation/release discards late
   results; capacity reuse cannot resurrect old IDs. Exercise interleavings safely.
4. Capacity includes terminal results; multiple retained results hit byte budget;
   oversize asset fails; take/release free budget; empty asset succeeds at zero cap.
5. Destruction joins an in-flight controlled read, never begins queued reads after
   stop, and preserves provider lifetime until worker exit. Repeated cleanup works.
6. Public fixture: generate a small scene archive and prefab archive in memory
   using existing encoders (generic synthetic identities/content, no Pokemon
   vocabulary). Expose them through an immutable test asset store; load through
   AsyncAssetLoader and decode on the caller with existing SceneArchiveStore and
   PrefabArchiveStore. Check exact identity and embedded content, reload/release,
   missing content and corrupt archive rejection. This validates actual PHRC
   integration, not a duplicate read/write stub. No GPU/material correctness claim.

Use existing boolean test functions and TestMain registration conventions.
Build the editor and contract targets with warnings-as-errors using the existing
`build-presentation` tree (VS2026, editor/tests/WX already enabled):
`cmake --build build-presentation --config Debug --parallel 4`
then `ctest --test-dir build-presentation -C Debug --output-on-failure`.
Reconfigure this tree if required, preserving its toolchain and enabled checks.
Inspect every result. Save full build logs under ignored artifacts if noisy and
return exit status plus useful error/tail context. Do not weaken existing checks.
Run `git diff --check` and new-doc link checks. No GPU run required for this pure
CPU addition; any unexpected rendering change is outside the brief.

Report changed files, API behavior, exact commands/results, failure/lifetime
tests, retained-budget caveat and deferred M1 coverage. Astra performs acceptance.
Budget: roughly 25 minutes, continue ordinary test/fix loops; checkpoint in REPORT
if blocked or interrupted. Do not treat the budget as permission to omit tests.
