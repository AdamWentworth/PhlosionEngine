# M1-D implementation report (correction cycle + pre-commit organization)

Worker status: ready_for_review. Astra acceptance is recorded in CHECKPOINT.md.
Task: `/root/cyberpunk_m1_fixture` (native `astra_flash_builder`)
Workspace: `D:/Projects/Phlosion/PhlosionEngine`
Baseline: engine main `83e02ffb66ccd28e006db1f2f4a3fc0d37ed0fd9` plus this
uncommitted M1-D patch and the root-owned brief.
Date: 2026-09-20

## Review corrections

1. **Matrix completeness.** `PublicAssetFixtureGuard matrix` now validates the
   exact unique Cartesian set `{runtime,editor} x {opengl,vulkan,d3d12} x
   {a-frame0,a-frame30,failed-replacement,valid-replacement-b}` before reading
   per-entry evidence. Unknown hosts/APIs, duplicate tuples, missing tuples and
   any count other than 24 fail. Skipped pairwise/cross-case comparisons are
   recorded as failures, and comparison errors increment the overall failure
   count. Manifest thresholds must be present, finite, positive and no looser
   than the approved 0.01 / 0.08 / 0.05 / tolerance 8 values. Astra's
   `review-duplicate-manifest.json` now exits 1 with
   `manifest contains a duplicate matrix tuple: runtime|opengl|a-frame0`
   (`artifacts/m1-d/review-duplicate-fixed.log`).
2. **Checker texture proof.** The expected-content gate no longer accepts the
   union of the two board colors alone. It now classifies magenta/cyan and
   orange/yellow separately and checks a 2x2 quadrant arrangement (A:
   cyan/magenta/magenta/cyan, B: orange/yellow/yellow/orange) with both colors
   required and the opposite color bounded per quadrant. Unit tests now use the
   same 2x2 arrangement and reject solid-color boards, flipped orientation,
   removed board, removed hidden ring, opaque/missing blend, depth failure and
   stale A inside a B frame.
3. **Runner process handling.** `run_capture_matrix.ps1` no longer writes a
   batch wrapper. It launches children directly through
   `System.Diagnostics.Process` with `CreateNoWindow`, exact Windows command-line
   quoting, asynchronous stdout/stderr draining, captured exit codes and a
   finite wait. Timeout cleanup uses `Process.Kill(true)` when available and
   falls back to `taskkill /T /F` while the parent is still alive, so a
   grandchild cannot survive. API/host/frame/time arguments are validated before
   any launch. Per-process environment overrides replace global env mutation, so
   nothing needs restoring and the caller environment is untouched.
4. **Provenance and frame gate.** `manifest.json` now records the source
   revision and dirty state, build configuration, PowerShell/.NET versions,
   requested capture/editor frames, SHA-256 plus byte count and modification
   time for the runtime/editor/plugin/guard binaries, and per-entry exit code,
   requested capture frame, parsed observed capture frame, screenshot backend
   and screenshot dimensions. The guard re-reads the raw stdout/stderr logs,
   parses the existing `[Screenshot][API] WROTE ... size=... frame=N` lines and
   requires exactly one matching `WROTE`, no `FAILED`, the requested native
   backend, exact path, requested frame and matching dimensions. It also
   requires `[ParityContract][API] PASS` with no `FAIL` (children run with
   `PHLOSION_PARITY_CONTRACT_FATAL=1`), rejects `using OpenGL compatibility
   mode`, and for editor entries cross-checks `metrics.json`
   `renderer.backend`, frame count and viewport against the handshake.
5. **Build policy.** New `PHLOSION_BUILD_PUBLIC_ASSET_FIXTURE` option defaults
   to `ON` for a standalone engine configure and `OFF` when the engine is
   consumed with `add_subdirectory`. The shared library builds when the option
   or tests are enabled; runtime/guard/plugin build only with the option (the
   plugin also requires the editor). CPU tests keep working with the hosts
   disabled.
6. **Fixture validation and docs.** `validateFixtureSnapshot` now compares
   declared and decoded texture dimensions, rejects empty/duplicate object
   ids and roles, checks every vertex/camera/motion/material float for
   finiteness, validates camera field of view and clip planes, rejects
   unsupported material values and bounds the alpha cutoff. Tests stage
   malformed candidates (camera range and finiteness, motion finiteness,
   duplicate id and role, texture dimension mismatch, unsupported material
   mode, empty id, non-finite vertex, invalid clip planes) and prove the typed
   decoder still stages them while fixture validation rejects them. One test
   activates A, stages a malformed B and proves the failed validation leaves
   the exact active A snapshot in place. Documentation now states the real
   validation scope, the farther-hidden-object depth direction and the
   activation count for a failed replacement (one publication).

## Organization adjustment (pre-commit)

The user asked for semantically correct locations before commit; the approved
layout is PLAN.md's "Organization adjustment before commit" section. The
relocation is a move/wiring/docs pass with no behavior change:

| From | To | Hash state |
| --- | --- | --- |
| `examples/public_asset_fixture/FixtureImageGuard.h/.cpp` | `tools/public_asset_fixture/FixtureImageGuard.h/.cpp` | moved byte-identical |
| `examples/public_asset_fixture/FixtureGuardMain.cpp` | `tools/public_asset_fixture/FixtureGuardMain.cpp` | moved byte-identical |
| `examples/public_asset_fixture/FixtureProcess.psm1` | `tools/public_asset_fixture/FixtureProcess.psm1` | moved byte-identical |
| `examples/public_asset_fixture/run_capture_matrix.ps1` | `tools/public_asset_fixture/run_capture_matrix.ps1` | moved with an EngineRoot default fix (the parameter default now resolves `$PSScriptRoot` in the body) |
| `examples/public_asset_fixture/test_fixture_harness.ps1` | `tests/public_asset_fixture/test_fixture_harness.ps1` | moved with a module-path/default fix only |
| `examples/public_asset_fixture/FixtureData/Session/Presentation`, `FixtureRuntimeMain`, `FixtureEditorPlugin` | unchanged | unchanged in place |
| `tests/TestPublicAssetFixtureRendering.cpp` | unchanged | unchanged in place (both library include directories still resolve the same header names) |
| `CMakeLists.txt` | edited in place | example `public_asset_fixture` vs verification `public_asset_fixture_verification` library split and the relocated harness path |
| `examples/public_asset_fixture/README.md` | new | directory ownership map |

The before/after SHA-256 of every affected file is recorded in
`artifacts/m1-d/reorganization-result.json`. Executable target names
(`PublicAssetFixtureRuntime`, `PublicAssetFixtureGuard`,
`PublicAssetFixtureEditorPlugin`) and the `PHLOSION_BUILD_PUBLIC_ASSET_FIXTURE`
option policy are unchanged. The runtime and editor plugin link only the
example library; the guard and the CPU contract link the verification library.
The fixed 0.4 interior / 0.35 edge thresholds, the quadrant checker checks and
the parity limits are untouched.

## Changed paths

| Path | Change |
| --- | --- |
| `tools/public_asset_fixture/FixtureGuardMain.cpp` (moved from `examples/`) | Exact matrix completeness and threshold validation, provenance/frame/parity/metrics gates, `synth` and `echo` helper commands |
| `tools/public_asset_fixture/FixtureImageGuard.h/.cpp` (moved from `examples/`) | Per-color board classes and 2x2 checker arrangement checks, dark-green hidden ring class, synthetic capture builder |
| `tools/public_asset_fixture/FixtureProcess.psm1` (moved from `examples/`) | Shared process launcher with quoting, async drain, exit codes, bounded tree cleanup, screenshot-log parser and PNG-size helper |
| `tools/public_asset_fixture/run_capture_matrix.ps1` (moved from `examples/`) | Direct native launches, argument validation, provenance recording, screenshot/parity/metrics verification, richer manifest |
| `tests/public_asset_fixture/test_fixture_harness.ps1` (moved from `examples/`) | Adversarial CLI/validator, evidence-parser and process-launcher tests |
| `examples/public_asset_fixture/FixtureData.cpp` | Texture-dimension comparison, finiteness and range validation, unique id/role checks, dark-green hidden ring color |
| `examples/public_asset_fixture/FixtureSession.h` | Corrected activation-count comment |
| `examples/public_asset_fixture/README.md` (new) | Example/verification/test directory ownership map |
| `tests/TestPublicAssetFixtureRendering.cpp` | Malformed-archive coverage, active-snapshot preservation, checker/ring/blend adversarial image tests aligned with the fixture |
| `CMakeLists.txt` | `PHLOSION_BUILD_PUBLIC_ASSET_FIXTURE` option, example vs verification library split, conditional targets, relocated harness CTest registration |
| `docs/PUBLIC_ASSET_FIXTURE.md` | Updated schema, build option, directory ownership, provenance, guard thresholds/checker proof, runner requirements, evidence and limits |
| `docs/agent-work/cyberpunk-m1-fixture/REPORT.md` | This report |

No production renderer, editor, engine core, application, archive format,
dependency manifest or other repository was modified. Root-owned
`PLAN.md`/`CHECKPOINT.md`, `docs/VERIFICATION.md`, `docs/README.md` and
`docs/CYBERPUNK_ENGINE_READINESS.md` were left untouched.

## Verification

| Command | Exit | Result | Log |
| --- | --- | --- | --- |
| `cmake --build build-presentation --config Debug --parallel 4` (post-relocation) | 0 | editor/tests enabled, warnings as errors; example and verification libraries plus all hosts rebuilt warning-clean | `artifacts/m1-d/reorg-build-final.log` |
| `ctest --test-dir build-presentation -C Debug --output-on-failure` (post-relocation) | 0 | 6 of 6 entries, including the relocated harness importing the relocated process module (19.9 s) | `artifacts/m1-d/reorg-ctest-final.log` |
| `.\tools\public_asset_fixture\run_capture_matrix.ps1 -RunId reorg-run02` | 0 | 24 of 24 captures; new script root, generated project descriptor, plugin copy and guard path all resolved | `artifacts/m1-d/reorg-run02.log` |
| `PublicAssetFixtureGuard matrix --require-complete` (reorg-run02) | 0 | 24 entries, 24 pairwise comparisons, 18 cross-case checks, `failure_count = 0` | `artifacts/m1-d/reorg-run02/matrix.json` |
| Source hash comparison vs `reorganization-baseline.json` | 0 | 4 byte-identical moves, 2 moved-with-edit scripts, 6 unchanged in place, `CMakeLists.txt` edited, README new | `artifacts/m1-d/reorganization-result.json` |
| `build-presentation/Debug/PhlosionEngineTests.exe` | 0 | 17 of 17 C++ contracts, including the fixture lifecycle/validation/image tests | `artifacts/m1-d/fix-contracts-02.log` (pre-relocation record; the unchanged source re-ran in the post-relocation CTest) |
| `.\examples\public_asset_fixture\run_capture_matrix.ps1 -RunId fix-run03` (historical pre-relocation path) | 0 | 24 of 24 captures, all exit 0, requested/observed frames recorded, guard matrix passed | `artifacts/m1-d/fix-run03.log` |
| `PublicAssetFixtureGuard matrix --require-complete` (fix-run03, historical) | 0 | 24 entries, 24 pairwise comparisons, 18 cross-case checks, `failure_count = 0` | `artifacts/m1-d/fix-run03/matrix.json` |
| `test_fixture_harness.ps1` (historical pre-relocation path) | 0 | Positive synthetic matrix plus duplicate/missing/unknown, loosened/invalid thresholds, exit-code/frame/backend/logical-frame tampering, blank content, quoting and timeout tree cleanup | `artifacts/m1-d/harness-05.log`; re-run as `PhlosionPublicAssetFixtureHarness` in the post-relocation CTest |
| Guard on Astra's `review-duplicate-manifest.json` | 1 | Rejected with `manifest contains a duplicate matrix tuple: runtime|opengl|a-frame0` | `artifacts/m1-d/review-duplicate-fixed.log` |
| Fixture option `OFF`, tests `ON` (`build-presentation`) | 0 | `PhlosionEngineTests` builds and 5 of 5 CPU tests pass; the harness test is not registered | `artifacts/m1-d/cmake-off-ctest.log` |
| Fresh child `add_subdirectory` configure with tests/editor ON | 0 | `PHLOSION_BUILD_PUBLIC_ASSET_FIXTURE=OFF` by default; `public_asset_fixture` and `PhlosionEngineTests` present, zero fixture host project files | `artifacts/m1-d/cmake-child-evidence.log` |

### Capture metrics (`reorg-run02`, identical to the historical `fix-run03`)

| Check | Result |
| --- | --- |
| Pairwise APIs within `runtime` | worst normalized MAE 0.0000023, worst RMSE 0.00012, changed ratio 0.0 |
| Pairwise APIs within `editor` | worst normalized MAE 0.0000052, worst RMSE 0.00024, changed ratio 0.0 |
| `a-frame30` vs `failed-replacement`, every host/API | pixel-identical, `max_channel_diff = 0`, changed ratio 0.0 |
| `a-frame0` vs `a-frame30` | 1.25-1.40% changed pixels (moving marker) |
| `a-frame30` vs `valid-replacement-b` | 30.9-34.6% changed pixels |
| Checker proof (runtime A frame 30 example) | TL cyan 1.00, TR magenta 0.81, BL magenta 0.82, BR cyan 1.00; opposite-color fractions <= 0.19 |
| Hidden ring (same example) | dark-green fraction 0.51, bright-green leak 0.0, occluder leaks 0.0 |
| Provenance | revision `83e02ff...`, `source_dirty = true`, 4 binary SHA-256 identities, requested/observed frames `6/6` runtime and `8/8` editor for all 24 entries |

The post-relocation `reorg-run02` matrix reproduced the same numbers with all
24 captures, per-entry provenance, exact screenshot-frame and parity gates.
Captures, per-case handshakes, editor metrics, stdout/stderr logs,
`manifest.json`, `matrix.json` and the contact sheet are under
`artifacts/m1-d/reorg-run02/`; the pre-relocation `fix-run03` copy remains
under `artifacts/m1-d/fix-run03/` as historical evidence. The post-relocation
contact sheet and full editor screenshots were inspected; the dark-green
hidden ring, 2x2 board checker, blend patch and moving white marker are all
visible, and the failed replacement is visually identical to A frame 30.

## Risks and limits

- Evidence is one Windows host with one NVIDIA GTX 1070. GPU runs remain
  opt-in outside CTest; no other GPU/driver, operating system or performance
  budget is claimed.
- Editor crop detection still depends on the fixture's saturated-green
  viewport background; a fixture redesign or editor theme that removes that
  signal would need the guard updated. Full editor screenshots are retained.
- The harness process tests use Windows PowerShell and `taskkill` as the
  .NET Framework fallback; PowerShell 7 / .NET Core uses `Process.Kill(true)`.
  The retained final CTest run exercises Windows PowerShell 5.1 and its
  taskkill fallback; it does not independently qualify every PowerShell/runtime.
- The fixture host uses its own fixed camera and rigid marker motion. It does
  not claim skeletal animation, production PBR/material interoperability,
  private Pokemon assets, streaming, physics, UI/audio or game readiness.

## Decisions for Astra

- Post-relocation repository surface is `examples/public_asset_fixture/`
  (runnable sample), `tools/public_asset_fixture/` (verification support),
  `tests/public_asset_fixture/` (harness), `tests/TestPublicAssetFixtureRendering.cpp`,
  `CMakeLists.txt` and `docs/PUBLIC_ASSET_FIXTURE.md`. Root-owned docs were not
  touched. The game repository has concurrent uncommitted changes from other
  work; nothing there was modified by this task.
- Root owns the commit and the integration of this pass into
  `docs/VERIFICATION.md`, `docs/README.md` and the M1 roadmap/index.

## Next checkpoint

None. The correction cycle and the approved pre-commit organization pass are
complete and ready for Astra's review and commit.
