# M1-C implementation report

Status: ready_for_review
Task: `/root/cyberpunk_m1_activation` (native `astra_flash_builder`)
Workspace: `D:/Projects/Phlosion/PhlosionEngine`
Baseline: `b2e03c4ca702589311c7cdac068f8858f01d760e` plus the root-owned
untracked `docs/agent-work/cyberpunk-m1-activation/` plan and checkpoint.
Date: 2026-09-20

## Changed paths and behavior

| Path | Change |
| --- | --- |
| `src/engine/assets/phlosion/PhlosionArchiveSession.h` (new) | Public `ArchiveKind`, `ArchiveSessionState`, `ArchiveSnapshot` and `PhlosionArchiveSession` contract exactly as briefed, with the documented threading, ownership, limit and destructor caveats |
| `src/engine/assets/phlosion/PhlosionArchiveSession.cpp` (new) | Session implementation: `begin`/`pump`/`activate`/`cancel`/`discard`/`unload` over one unchanged `PhlosionAssetGraphLoader`, fresh `SceneArchiveStore`/`PrefabArchiveStore` decode of a `Phrc` root, immutable `shared_ptr<const>` snapshots |
| `tests/TestPhlosionArchiveSession.cpp` (new) | CPU contract coverage on public in-memory fixtures built by the real PHRC and archive encoders |
| `tests/TestMain.cpp` | Declares and registers `phlosion_archive_session_contract` |
| `CMakeLists.txt` | Adds the session source to `engine_core` and the new test source to `PhlosionEngineTests` |
| `docs/ARCHIVE_ACTIVATION.md` (new) | Candidate lifecycle, activation/cancel/discard/unload rules, limit and ownership caveats, boundary statement, verification and deferred work |
| `docs/VERIFICATION.md` | CTest row updated to sixteen C++ contracts, an archive-activation scope paragraph, and a dated M1-C evidence entry |

No graph/byte loader, archive format, decoder, renderer, editor, runtime
Application, ABI or dependency file was modified. The active snapshot is only
ever changed by `activate` or `unload`; `begin`, `pump`, candidate failure,
`cancel` and `discard` leave it untouched. A failed or cancelled candidate
exposes no staged snapshot, and a released in-flight read can never publish
into a later candidate because `cancel`/`discard` use the loader's cancel/reset
path and never the terminal `requestStop`.

## Verification

| Command | Exit | Result | Log |
| --- | --- | --- | --- |
| `cmake --build build-presentation --config Debug --parallel 4` | 0 | Clean rebuild of the existing VS2026 editor/tests Debug tree; both new translation units compiled, zero warnings (engine warnings are errors). | `artifacts/m1-c/build-final.log` |
| `ctest --test-dir build-presentation -C Debug --output-on-failure` | 0 | 5 of 5 CTest entries passed. `PhlosionEngineContracts` 1.56 s (sixteen contracts), semantic boundary 0.71 s, three source contracts passed. | `artifacts/m1-c/ctest-final.log` |
| `build-presentation/Debug/PhlosionEngineTests.exe` | 0 | `[PASS] phlosion_archive_session_contract` alongside the other fifteen contracts. | `artifacts/m1-c/contracts-final.log` |
| `git diff --check` plus a trailing-whitespace/tab/final-newline scan of every new and edited file | 0 | `git diff --check` reported no whitespace errors; no trailing whitespace, tabs or missing final newline. All new files are LF. `CMakeLists.txt` keeps its pre-existing mixed worktree line endings from the checkout plus earlier patch edits; its index blob is LF, so staging normalizes it. | `artifacts/m1-c/checks.log` |
| Relative Markdown link check over `docs/ARCHIVE_ACTIVATION.md`, `docs/VERIFICATION.md` and this report | 0 | Every relative repository link resolves. The only external links are the pre-existing `https://` consumer links in `VERIFICATION.md`. | `artifacts/m1-c/checks.log` |

The contract test covers, in seven case groups: real scene and prefab staging
with explicit activation, an opaque registry alias, exact embedded bytes, a
preserved required dependency with its validated hash, a skipped optional
record with no resolver or provider call, and a prefab over a real scene and raw
leaf in both variants (embedded file and dependency-only, `fileCount() == 0`);
scene A active through B's loading and staging with a pointer-identity switch
and a retained snapshot; wrong requested type, unsupported root schema, a valid
PHRC envelope with an invalid file record, a root resolved as opaque bytes,
missing required content, a corrupt required dependency failing its required
hash, and a byte-cap failure, each preserving A and naming the root and failing
dependency; failed/cancelled discard and retry plus invalid
`begin`/`activate`/`cancel` calls and constructor rejections; gated in-flight
reads with cancel and discard, replacements admitted while a released read is
still blocked, count-based waits that cannot be satisfied by an earlier read of
the same path, read-order proof that the released read reached the provider
first, and a late completion that cannot stage or activate; and `unload` from
`Idle`, `Staged` and during candidate work with two `weak_ptr` lifetime checks
for the snapshot and for the provider.

### Review corrections applied

Astra's acceptance review asked for five evidence corrections; all are in this
revision, with no production API or behavior change:

1. `caseUnloadAndSnapshotLifetime` now captures the earlier read count of the
   reused path and waits for that count plus one, so the gated candidate is
   proven to be a fresh read; the replacement read is asserted not to have
   reached the provider while the released read is blocked, and `readOrder()`
   proves the released read was served first.
2. `casePrefabGraphStaging` runs both variants: the embedded-file prefab and the
   dependency-only prefab that stages and activates with `fileCount() == 0` and
   still retains the prefab -> scene -> raw closure with the scene archive
   re-decodable from the retained bytes.
3. A provider-lifetime block drops every external strong provider reference
   while the session lives: the reader keeps the provider alive, the active
   snapshot stays readable, the provider expires once the session is destroyed,
   and a retained snapshot stays readable afterwards.
4. The failure table gained a corrupt required dependency whose stored bytes
   fail the required hash, asserted through
   `Dependency hash mismatch: 'registry/corrupt-dep' requires
   'session/corrupt-child'` while the active snapshot is preserved.
5. `docs/ARCHIVE_ACTIVATION.md` now states that the gate cleanup guard is
   declared after the session so it opens the gate before the reader joins, and
   `docs/VERIFICATION.md` distinguishes active-snapshot preservation on
   candidate failure/cancel/discard from caller-held snapshots surviving the
   `unload` that clears the session's active reference.

## Risks and limits

- CPU resource contract only. The engine has no application scene/archive call
  site to migrate, so this bundle proves the transition through public
  fixtures. No game/editor call-site migration, renderer upload, GPU run or
  visual-parity claim is made, and no M1 rendered-readiness claim follows.
- `AssetGraphOptions` caps stay per candidate graph. Active and staged
  snapshots may coexist, typed stores duplicate embedded payloads, and
  consumer-held snapshots can outlive the session, so the cap is not a total
  residency or peak-memory bound. This is documented rather than enforced.
- The session itself is single-owner-thread. The destructor still joins the
  graph reader and cannot interrupt a provider read that is already blocked;
  that inherited behavior is documented and tested only through the bounded
  gate guard, not through a real blocked provider.
- The byte-cap failure fixture is caught by the reader's retained-resident
  budget before the graph's own cumulative check, so the test accepts either
  diagnostic; the graph-level limit itself stays covered by the M1-B suite.
- One deliberate contract decision: `cancel()` enters `Cancelled` and a
  replacement `begin` is admitted only after `discard()`, per the brief's state
  rules; the gated reuse test exercises that exact sequence for both `cancel`
  and `discard`.

## Decisions for Astra

- `docs/README.md` (docs index) and `docs/CYBERPUNK_ENGINE_READINESS.md`
  (roadmap progress, currently modified by root) are left untouched; both need
  a root-owned row/update for `docs/ARCHIVE_ACTIVATION.md` and the M1-C status.
- `docs/agent-work/cyberpunk-m1-activation/PLAN.md` and `CHECKPOINT.md` are
  root-owned and were not edited; this report is the worker deliverable.

## Next checkpoint

None. The bundle is complete and awaiting Astra's review; no further work is
planned without an accepted contract change.
