# M1-A worker report: asynchronous asset bytes and public CPU fixture

Status: ready_for_review
Task: M1-A (`/root/cyberpunk_m1_assets`), implementation worker
Workspace: `D:/Projects/Phlosion/PhlosionEngine`
Baseline: `c17d2122bd36b3c3bb49c4e703900f40cccfbd76` (main)
Date: 2026-09-20
Revision note: includes the Astra review correction cycle (`requestStop()`,
bounded and guarded test gates, sentinel-based ordering, CTest timeout, exact
fixture comparisons) and a targeted second correction for a guard/thread
lifetime ordering defect in the concurrency fixtures.

## Changed paths and behavior

New loader in namespace `engine::assets`:

- `src/engine/assets/AsyncAssetLoader.h`: `AssetLoadId`, `AssetLoadState`, `AssetLoadOptions`, `AssetLoadResult` and a noncopyable/nonmovable PImpl `AsyncAssetLoader` with `request`, `state`, `cancel`, `take`, `release`, `requestStop` and `residentBytes`.
- `src/engine/assets/AsyncAssetLoader.cpp`: one background worker with queued handoff, provider I/O outside the state lock, monotonic never-reused IDs, capacity that counts terminal results, cancel/release publication suppression, overflow-safe retained-byte accounting, and a single shared stop path.

`requestStop()` is the explicit lifecycle boundary, not a test hook. Under the
state lock it rejects later requests with `0` and a diagnostic, clears queued
work, marks every `Queued`/`Loading` request `Cancelled`, and suppresses late
publication; `Ready`/`Failed`/`Cancelled` results stay drainable through
`take`/`release` with their accounting intact. It is idempotent, nonblocking, and
never joins the worker. The destructor now calls exactly this path and then
joins the active reader before provider and state are destroyed.

New tests and documentation:

- `tests/TestAsyncAssetLoader.cpp`: deterministic controllable provider plus the concurrency, failure, budget, stop and lifetime contracts listed below.
- `tests/TestPublicAssetFixture.cpp`: synthetic PHRC scene/prefab archives built with the existing encoders, served by an immutable in-memory `IAssetStore`, loaded through `AsyncAssetLoader` and decoded on the caller with `SceneArchiveStore` and `PrefabArchiveStore`.
- `tests/TestMain.cpp`: registers `async_asset_loader_contract` and `public_asset_fixture_contract`.
- `CMakeLists.txt`: adds the loader to `engine_core`, both test sources to `PhlosionEngineTests`, `find_package(Threads REQUIRED)` with `Threads::Threads` as a public `engine_core` dependency, and a 120 second CTest timeout on `PhlosionEngineContracts`.
- `docs/ASYNC_ASSET_LOADING.md`: service model, state and ID lifecycle, stopping, retained-budget caveat, destruction rules, verification scope and deferred work.
- `docs/VERIFICATION.md`: contract count and coverage entry, the CTest timeout backstop, a note that the loader tests are CPU only, and a dated local evidence entry.

Not touched: `IAssetStore` ABI, renderers, application/editor paths, dependency
manifests, game repositories, and prior roadmap/checkpoint/index material. The
pre-existing dirty `docs/README.md` and the untracked
`docs/CYBERPUNK_ENGINE_READINESS.md` plus `docs/agent-work/cyberpunk-readiness/*`
are unchanged.

## Review findings and how they were addressed

1. Unbounded gate waits and unsafe joins: the controlled provider now pauses only
   in a bounded `wait_for` that records a timeout diagnostic and returns an
   explicit failure, and `GateAndThreadCleanup` opens the gate before joining any
   tracked helper thread. Every early-return path now releases the paused read
   before loader destruction, and no join happens while a caller-held mutex (the
   destruction flag mutex) is held.
2. Stop-ordering assumption removed: `requestStop()` is the synchronous stop
   boundary, so queued work can no longer start regardless of when the destroying
   thread reaches the destructor. The 250 ms "reset before gate" ordering claim is
   gone; the remaining bounded negative observation only asserts that destruction
   did not return while a read was paused, and the real witnesses are the joined
   destroyer thread and the provider completion signal.
3. Late-publication proof replaced: `expectStateStable` windows are removed. A
   sentinel request admitted behind a paused read now proves, through the single
   worker's queue order, that the earlier read and its publication decision
   already finished. The cancellation test cancels and takes the in-flight
   request while the read is still blocked, immediately reuses the freed capacity
   with a new ID, opens the gate, and checks that the old ID never resurrects.
   Eight queued request/cancel/release cycles behind a paused read exercise stale
   queue removal and prove those paths never reached the provider. The provider
   records the calling thread id and the test asserts no read ran on the
   requesting thread. A provider that writes partial bytes and returns false was
   added next to the throwing case; both must produce `Failed` with no bytes.
4. `PhlosionEngineContracts` now has a 120 second CTest `TIMEOUT` as a regression
   backstop; no new test target or change to other tests.
5. The fixture now compares parsed prefab metadata against the supplied JSON with
   nlohmann and compares the embedded material JSON exactly, instead of checking
   only that a key appears.
6. Helper-thread guard lifetime: `GateAndThreadCleanup` stored a `std::thread*`
   that could outlive the thread handle. Both helper-thread sites now declare a
   default `std::thread` before the guard and start it later, so declaration order
   is the contract: the flag mutex and condition variable outlive the joined
   thread, the thread handle outlives the guard that joins it, and the guard
   outlives every early return. In `runDestructionCycle` the guard is also
   constructed before the first early-return path, so a paused read is always
   released before a loader can be destroyed. The four non-threaded guard sites
   were audited and already declare the guard after the loader. The
   `requestStop()` header comment was corrected: only the worker finishing a
   started read or the destructor's join reaps it, while `take`/`release` remove
   the handle and its result, and the editorial "not a test hook" wording was
   removed from the header (the same overstatement was fixed in the loading doc).

## Verification

All commands ran in `D:/Projects/Phlosion/PhlosionEngine` with native PowerShell.
Full logs are under the ignored `artifacts/m1-a/`, including the earlier failing
iterations.

| Command | Exit | Result |
| --- | --- | --- |
| `cmake --build build-presentation --config Debug --parallel 4` | 0 | Reconfigured in place (same VS2026 generator, editor/tests/WX enabled, warnings as errors) and built every target, including the new `engine_core` source, with no errors or warnings. Log: `build-lifetime.log` |
| `ctest --test-dir build-presentation -C Debug --output-on-failure` | 0 | 5 of 5 entries passed; `PhlosionEngineContracts` passed all fourteen contracts, including the two new ones, in 0.75 s. Log: `ctest-lifetime.log` |
| `build-presentation/Debug/PhlosionEngineTests.exe`, 4 consecutive runs (pre-lifetime-fix revision) | 0 each | 0 failures. Logs: `repeat-final-*.log` |
| `git diff --check` | 0 | No whitespace errors (only Git LF/CRLF notices). |
| New-doc relative link scan for `docs/ASYNC_ASSET_LOADING.md` and `docs/VERIFICATION.md` | 0 | Every relative link target resolves. |

Failures found and fixed during the loop: the earlier inverted `take`
expectation; a resident-byte assertion that ignored the paused read; a
cancellation test that assumed cancel frees the capacity slot before `take`
(terminal results hold their slot by contract); and a cancellation result
assertion that expected an empty diagnostic.

## Contract coverage in the new tests

Success, zero bytes, missing asset, provider false with partial bytes, throwing
provider with partial bytes, recovery, empty-path and capacity admission
diagnostics, null provider and `maxRequests == 0` constructor rejection, unknown
and taken ID behavior, preserved output on an unsuccessful `take`, terminal and
queued cancellation, in-flight cancel and release with immediate capacity reuse
and a later sentinel witness that the old ID never resurrects, eight churn cycles
proving stale queued work is removed and never read, capacity that includes
terminal results, multi-asset and oversize retained-byte budget with take/release
freeing, a zero-byte budget with an empty asset, `requestStop` semantics
(synchronous cancellation of queued and in-flight work, preserved drainable
terminals, rejected admission, repeated stops, no provider call for stopped
work), and destruction that joins an in-flight read, keeps the provider alive
until the worker exits, and repeats cleanly.

The fixture proves real PHRC integration without private content: exact encoded
bytes through the loader, decoded scene ID and exact scene JSON and geometry,
prefab ID/kind, parsed metadata equal to the supplied JSON, exact embedded
material JSON, mesh bytes, reload after release, missing-content failure, and
corrupt archive rejection by the caller-side decoder while the loader still
transfers the bytes verbatim.

## Limitations and risks

- `maxResidentBytes` bounds retained `Ready` payloads only. The provider's
  temporary read vector, allocator capacity, provider caches, request metadata
  and post-`take` consumer bytes are outside it; this is not a hard peak-memory
  or process limit. Recorded in the header and in `docs/ASYNC_ASSET_LOADING.md`.
- Cancellation and `requestStop()` cannot interrupt a blocked OS or provider
  read; destruction can wait for the current synchronous read.
- A stopped loader keeps terminal results in its map until `take`/`release`, so
  their slots stay occupied after the stop, matching the capacity contract.
- No GPU or visual-parity run was performed. This iteration adds no rendering
  behavior, so renderer parity coverage is unchanged and unverified here.

## Decisions for Astra

1. `requestStop()` does not join the worker, per the amended contract, so a
   stopped loader can briefly still own a finishing read until `take`, `release`
   or destruction reaps it. Tests assert that path through completed provider
   signals and the destructor join rather than through timing windows.
2. `Threads::Threads` was added as a public `engine_core` dependency for standard
   C++ thread linkage; no new third-party dependency was introduced.
3. `docs/README.md` documentation-index integration is left to Astra, so the new
   `docs/ASYNC_ASSET_LOADING.md` is currently linked only from
   `docs/VERIFICATION.md`.

## Next checkpoint

None: the M1-A bundle is implemented and locally verified. Remaining M1 work
(rendered fixture, private roster, dependency resolution, renderer uploads,
caller migration) stays with Astra.
