# M1-B worker report: dependency-aware asset loading

Status: ready_for_review
Task: M1-B (`/root/cyberpunk_m1_dependencies`), single implementation writer
Workspace: `D:/Projects/Phlosion/PhlosionEngine`
Baseline: `e2ec9266e21672c8b2bfe6655ce0a26a8e8f3d92` (clean `main`)
Date: 2026-09-20

## Changed paths and behavior

New session in namespace `engine::assets::phlosion`:

- `src/engine/assets/phlosion/PhlosionAssetGraphLoader.h` (142 lines): the
  approved public surface - `AssetKind`, `AssetLocation`, `AssetResolver`,
  `AssetGraphState`, `AssetGraphOptions`, `LoadedAsset`,
  `SkippedOptionalDependency`, `AssetGraphResult` and the
  noncopyable/nonmovable `PhlosionAssetGraphLoader` with `start`, `pump`,
  `state`, `cancel`, `take`, `reset` and `loadedBytes`.
- `src/engine/assets/phlosion/PhlosionAssetGraphLoader.cpp` (545 lines): one
  owner-thread session over an owned `AsyncAssetLoader` configured with
  `maxRequests = 1` and `maxResidentBytes = maxEncodedBytes`.

Behavior, matching the PLAN point by point:

- the constructor rejects a null store, an empty resolver and `maxAssets == 0`
  with `invalid_argument`; `start` admits a nonempty root from `Idle` only and
  enters `Loading` without resolving or reading; the resolver and all reads
  begin in `pump`;
- `pump` never waits for I/O; it consumes at most one completed read per call,
  decodes it on the owner thread, then may schedule the next read. `Phrc` uses
  the existing decoder and its validated `Document.contentHash`, `Bytes` is an
  opaque leaf hashed over the whole payload, and asset IDs stay opaque;
- each required unique ID is resolved and read once per session (diamond
  deduplication); original encoded bytes are retained and decoded chunk payloads
  are not;
- only `kDependencyRequired` edges are traversed; optional edges are recorded
  verbatim in `skippedOptionalDependencies` without resolving or reading them
  and impose no hash constraint; unknown flag bits fail the document;
- nonzero expected hashes are compared with the target hash under its resolved
  kind, including late constraints on shared or already-loaded nodes, with
  parent and target IDs in every diagnostic; zero means unspecified;
- required cycles, including self-cycles, fail before `Ready` through bounded
  iterative reachability over discovered required edges; optional edges take no
  part;
- `maxAssets` counts unique required assets including the root,
  `maxDependencyEdges` counts every encountered record (duplicate and optional
  included), `maxEncodedBytes` caps cumulative retained encoded bytes; all
  checks are overflow-safe and run before a payload is retained; exceeding a
  limit releases every staged payload;
- `Ready` publishes the full closure only; `Failed` and `Cancelled` carry the
  root and error with empty assets, empty skipped optionals and zero
  `loadedBytes`;
- `cancel` succeeds only in `Loading`, becomes `Cancelled` immediately and
  releases the underlying request and staged data; `take` works only for
  terminal states and moves the result out; `reset` works from any state and
  permits a later `start` while a released read is still in flight; the
  destructor joins the reader. Neither `cancel` nor `reset` calls the reader's
  terminal `requestStop`, and old completions cannot reach a later session.

New contract, docs and registration:

- `tests/TestPhlosionAssetGraphLoader.cpp` (2237 lines): eleven case functions
  behind `test_phlosion_asset_graph_loader_contract`, driving public synthetic
  fixtures (real PHRC and archive encoders, in-memory `IAssetStore`, gated
  reads, a resolver fixture with failure injection).
- `tests/TestMain.cpp`: registers `phlosion_asset_graph_loader_contract`.
- `CMakeLists.txt`: adds the loader to `engine_core` and the test source to
  `PhlosionEngineTests`.
- `docs/ASSET_DEPENDENCY_LOADING.md` (213 lines): session model, kinds and hash
  rules, optional edges, limits and their peak-memory caveat, termination and
  reuse rules, verification scope and deferred work.
- `docs/VERIFICATION.md`: contract count `Fourteen` -> `Fifteen`, the new
  coverage entry, a CPU-only scope note and a dated M1-B evidence entry.
- `docs/agent-work/cyberpunk-m1-dependencies/REPORT.md`: this evidence.

Not touched: `IAssetStore`, `AsyncAssetLoader` behavior, PHRC and archive
formats, existing tests, renderers, editor or game paths, dependency manifests,
and the PLAN/CHECKPOINT/roadmap material. `docs/CYBERPUNK_ENGINE_READINESS.md`
was edited concurrently by another writer (Astra) during this pass; that edit is
preserved untouched. No commit, stage, push, deploy or production migration was
performed.

## Verification

Configuration in use: `build-presentation` (Visual Studio 18 2026, Debug,
`PHLOSION_BUILD_EDITOR=ON`, `PHLOSION_BUILD_TESTS=ON`,
`PHLOSION_ENABLE_WARNINGS=ON`, `PHLOSION_WARNINGS_AS_ERRORS=ON`), read from
`build-presentation/CMakeCache.txt`. CMake reconfigured automatically because
`CMakeLists.txt` changed; no manual reconfigure was needed.

| Command | Exit | Salient result | Log |
| --- | --- | --- | --- |
| `cmake --build build-presentation --config Debug --parallel 4` | 0 | Whole tree, including `PhlosionEditor`, the plugin probe and `PhlosionEngineTests`, rebuilt cleanly with warnings as errors | `artifacts/m1-b/build-final.log` |
| `ctest --test-dir build-presentation -C Debug --output-on-failure` | 0 | 5 of 5 CTest entries passed; `PhlosionEngineContracts` passed all fifteen contracts in 1.90 s; semantic boundary and the three source contracts passed | `artifacts/m1-b/ctest-final.log` |
| `build-presentation/Debug/PhlosionEngineTests.exe` | 0 | `[PASS] phlosion_asset_graph_loader_contract` alongside the other fourteen contracts | `artifacts/m1-b/contracts-pass.log` |
| `PhlosionEngineTests.exe` repeated 12 times | 0 | 12 of 12 runs green; no flake in the gated cancellation/reset or diamond tests | `artifacts/m1-b/repeat-1..12.log` |
| `git diff --check` | 0 | No whitespace or conflict errors (only informational LF/CRLF notices) | - |
| Markdown link check on the new document plus `VERIFICATION.md` | 0 | Every relative link in both files resolves; the four absolute links are pre-existing project URLs | - |

After the Astra review corrections below, the required checks were repeated once
on the final tree:

| Command | Exit | Salient result | Log |
| --- | --- | --- | --- |
| `cmake --build build-presentation --config Debug --parallel 4` | 0 | Whole tree rebuilt cleanly with warnings as errors | `artifacts/m1-b/build-correction.log` |
| `ctest --test-dir build-presentation -C Debug --output-on-failure` | 0 | 5 of 5 entries; `PhlosionEngineContracts` passed all fifteen contracts in 1.94 s | `artifacts/m1-b/ctest-correction.log` |
| `build-presentation/Debug/PhlosionEngineTests.exe` | 0 | `[PASS] phlosion_asset_graph_loader_contract` | `artifacts/m1-b/contracts-correction.log` |

## Astra review corrections (cycle 1)

1. The synthetic provider's gated read no longer waits unbounded. It uses a
   bounded `wait_for`; a timeout is recorded in the provider's `gateTimeouts()`
   list and both gated cases assert the list is empty after the session
   finishes. The record lives in the provider, so `cancel`, `reset` and the
   cleanup guard cannot discard it, and a timed-out gate would fail the test
   rather than silently satisfy the ordering claim.
2. The late-constraint success block now exercises the intended path: the root
   names the leaf with an unspecified (zero) hash, and the alpha document, which
   is decoded only after the leaf was read, adds the nonzero expected hash. The
   block asserts the read order `late/root.phrc`, `late/leaf.bin`,
   `late/alpha.phrc`, that the leaf was resolved and read exactly once, and that
   the leaf hash equals the constraint, so the comparison ran against an
   already-loaded asset.
3. Added `caseChildFailures` with two compact required-child failures, both
   after a valid root had already loaded and staged data and both recording an
   optional edge on the root: a required child with no resolver mapping, and a
   required child whose bytes are not a valid PHRC document. Both prove the
   failed result keeps the root ID and names the child, and that the staged
   payload, the recorded optional edge and the byte accounting are dropped
   (empty `assets`, empty `skippedOptionalDependencies`, `loadedBytes() == 0`),
   with the provider read order showing the root was read first and the optional
   edge was never resolved.
4. Documentation and comments: the `ASSET_DEPENDENCY_LOADING.md` sample now
   shows one `pump` per caller update instead of a tight blocking loop, consumes
   assets only when `result.state == Ready`, and demonstrates terminal error
   handling; the reader-member comment in the loader now states the real
   destruction order (the reader is destroyed before the resolver and options it
   was built from, and after the graph state members declared below it); the
   reset-case comment now says the new session completes after the released read
   finishes. The verification note about bounded waits now describes the
   recorded gate timeout.

This cycle added no API, architecture or unrelated cleanup.

Test-sensitivity checks (mutation testing, performed and reverted; the final
tree contains no mutation):

| Mutation | Observed | Log |
| --- | --- | --- |
| Early `return false` in the cycle-reachability helper | Rejected by `/WX` as unreachable code (C4702); the mutation never ran | `artifacts/m1-b/mutation-cycle-build.log` |
| Required edges no longer recorded (equivalent to disabling cycle detection) | `[FAIL] phlosion_asset_graph_loader_contract: cycles and flags: expected a failed graph but the state is Ready` | `artifacts/m1-b/mutation-cycle2-run.log` |
| Recorded hash constraints cleared before checking | `[FAIL] phlosion_asset_graph_loader_contract: hash constraints: expected a failed graph but the state is Ready` | `artifacts/m1-b/mutation-hash-run.log` |

## Issues found and fixed during the loop

1. `Impl::state_`/`stagedBytes_` were private while the public wrappers read
   them directly; added `Impl::state()`/`Impl::loadedBytes()` accessors.
   `artifacts/m1-b/build-01.log` (exit 1, C2248).
2. Two test sources patches landed out of order, leaving case definitions after
   the contract entry point and one address-of-temporary expression; reordered
   the file and bound the expected byte count to a named value.
   `artifacts/m1-b/build-02.log` (exit 1).
3. The optional-edge case asserted that no read happened at all, but the root
   read is expected; the assertion now checks that only the root path was read
   and that the optional ID was never resolved. `artifacts/m1-b/ctest-01.log`
   (exit 8).
4. The lifecycle recovery case started its second session on the deliberately
   unmapped resolver fixture; it now reuses the mapped resolver.
   `artifacts/m1-b/ctest-02.log` (exit 8).

## Risks and limits

- The encoded-byte cap is a retained accounting budget, not a peak-memory
  bound. Provider temporaries, `AsyncAssetLoader` completed-read staging,
  decoder copies (including chunk payloads inside `Document`), provider caches,
  graph metadata, vector capacity and consumer-owned results are outside it.
  The decoder can allocate from a document before the graph inspects its edge
  limit. This is documented rather than promised away.
- Ready is a container-level guarantee only: no typed-profile or schema
  validation, embedded material, model or JSON semantics, no cross-session
  cache, deduplication or hot reload, no streaming or ranged reads and no
  interruption of a blocked provider read.
- Failures attribute per-asset history; when the graph fails, `take` reports the
  first diagnostic and the session drops all history. Consumers that need
  per-asset reporting should re-run the graph or extend the API in a later
  milestone.
- Local verification used the required `build-presentation` tree. The CI
  workflow configures a fresh `build` tree with the same options; that separate
  tree was not rebuilt locally.
- No GPU, renderer or visual-parity evidence is produced or claimed by this CPU
  iteration; the standing three-API renderer requirement is unaffected because
  no rendering path changed.

## Decisions for Astra

- `docs/README.md` has no index entry for `ASSET_DEPENDENCY_LOADING.md`; the
  documentation index is root-owned, so the entry was left to Astra.
- `docs/CYBERPUNK_ENGINE_READINESS.md` was edited by another writer during this
  pass; its roadmap wording was not adjusted to name M1-B, which is also
  root-owned.
- No architectural blockers were hit. Nothing in the accepted contract needed an
  interpretation beyond the PLAN text.

## Next checkpoint

Astra reviews the patch and the logs under `artifacts/m1-b/`. Deferred M1 work
remains production scene/prefab integration, typed-profile and schema
validation, cross-session caching or hot reload, streaming reads and the
rendered fixture across OpenGL, Vulkan and Direct3D 12.
