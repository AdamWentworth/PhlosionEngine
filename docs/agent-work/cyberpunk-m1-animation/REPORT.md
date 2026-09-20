# M1-G completion report (post-review fix pass)

STATUS: ready_for_review

- Task ID: M1-G (`cyberpunk_m1_animation`), single native DeepSeek V4.1 Flash builder.
- Workspace: `D:/Projects/Phlosion/PhlosionEngine`, HEAD `9c0d812ce2520f0fbfae6dd5bcccc0e42e118339`, tree dirty with this pass only (plus root's own doc edits, see below).
- No commit, stage, push, deploy, dependency, setting or credential change. Nothing outside this repository was edited.
- Scope: the six consolidated review items below. No production behavior change; `src/engine/render/ModelPoseSampler.*` is byte-identical to the reviewed extraction.

## Review items and disposition

1. **One palette allocation across the bind -> requested transition.** Added `FixturePaletteStorage` and `FixturePaletteTransition` to `examples/animation_fixture/AnimationFixturePresentation.h/.cpp`. Both hosts allocate one palette buffer for the whole run, write the bind values, then overwrite the same buffer in place for the requested pose, and keep a snapshot of the bind values for diagnostics. `renderAnimationFixture` now takes that storage instead of a pose, so the submitted pointer cannot come from a per-pose vector. The handshake records `presentation.palette_address`, `bind_draw_palette_address`, `pose_draw_palette_address` and `palette_contents_changed`; the guard requires all three addresses to be equal, the host's flag to match the case, and (independently) requires the requested palette values to differ from the recorded bind palette on posed cases. `m1g-run04` shows one address for both draws with `changed=True` on every posed case and `changed=False` on `bind`. Harness negatives added: `palette-address` (swapped allocation) and `palette-stays-bind` (requested palette still holding bind values).
2. **Provenance hardening.** `binaries` and each of its four identities are now mandatory (`missing-binaries`, `missing-binary` harness negatives). Every entry's `requested_capture_frame` must equal `manifest.capture_frames[host]` (`entry-frame`). The handshake must record an explicit `requested_backend` (no default hiding a missing field; `missing-requested-backend`) and its `host` must match the tuple (`host-mismatch`), its `clip_index` must match the case catalog (`clip-index`), and submitted vertex Z is validated with X/Y (`vertex-z`). Palette, mesh transform, timing and vertex values are checked finite. Thresholds are unchanged and cannot be loosened.
3. **Synthetic defects model the real failure modes.** `DisabledSkinning` now draws `meshTransform * bindVertex` (what the renderer does when skinning is off), so it is distinguishable even on the `bind` case; `StalePalette` remains `mesh * bindPalette * vertex == bind point`. The CPU contract's `mustFail` table was updated accordingly, and the harness adds `disabled-skinning-bind`, which the guard rejects. `ExpectedContentResult` now reports measured image values under `measured_content` and authored landmark distances under `expected_geometry`, so published numbers never mix the two (see `m1g-run04/measured-content-summary.txt` and `expected-geometry-summary.txt`).
4. **LINEAR scale contract.** `checkLinearScaleInterpolation` adds a changing nonuniform-scale clip with literal per-axis expectations (`(1,2,3)` -> `(3,4,6)`, midpoint `(2,3,4.5)`), the composed global matrix, a transformed unit point and a quarter-key sample.
5. **Capture runner and harness hygiene.** `-ExpectedBaseline` now defaults to the resolved HEAD and only enforces an explicitly supplied revision (resolved to a commit); the inaccurate "both PowerShell versions exercised" comment was replaced with the version actually used. The harness acquires its diagnostic work directory with `New-Item` without `-Force` (GUID names, collision retries) and documents that the directory is retained on purpose.
6. **Guide corrections.** `trsToMat4` is described as declared in the header and defined in the cpp; "two-API-consistent" is corrected to three APIs; the archive-fixture regression claim is narrowed to that fixture's unchanged rigid-content images (it never samples a pose, so it cannot evidence sampler behavior).

## What changed

Production (unchanged behavior, unchanged since the first review):

- `src/engine/render/ModelPoseSampler.h/.cpp` (new): `samplePose`, `PoseSource`, `trsToMat4`, extracted verbatim from `Model::buildPoseMatrices`.
- `src/engine/render/ModelAnimation.cpp`: `buildPoseMatrices` delegates to the sampler; draw helpers, diagnostics and public behavior untouched.
- `src/engine/render/Model.cpp`: `trsToMat4` delegates to the shared definition (one line plus the include). Approved by review.

Build, tests, fixture and tools:

- `CMakeLists.txt`: `PHLOSION_BUILD_ANIMATION_FIXTURE` option, `animation_fixture`, `animation_fixture_verification`, runtime/guard/editor-plugin targets, new CPU contract, `PhlosionAnimationFixtureHarness` CTest entry.
- `examples/animation_fixture/`: case catalog (now with explicit clip indices), rig, palette storage/presentation, runtime host, editor plugin, README.
- `tools/animation_fixture/`: guard library + CLI, capture runner (shared process launcher).
- `tests/TestModelPoseSampler.cpp`, `tests/TestAnimationFixture.cpp`, `tests/animation_fixture/test_fixture_harness.ps1`.
- `docs/ANIMATION_VERIFICATION.md`, this report.

Not touched: `PLAN.md`, `CHECKPOINT.md`, `docs/README.md`, `docs/VERIFICATION.md`, `docs/CYBERPUNK_ENGINE_READINESS.md`. The last two show as modified in `git status` because root updated M1-F checkpoint wording in this working tree; this pass made no edit to them.

## Verification (final binaries, after all six fixes)

| Command | Exit | Result |
| --- | --- | --- |
| `cmake --build build-presentation --config Debug --parallel 4` (`build-full-05.log`) | 0 | Warning-clean full build; 0 warning lines |
| `ctest --test-dir build-presentation -C Debug --output-on-failure` (`ctest-05.log`) | 0 | 7/7 entries: 23 C++ contracts, four source contracts, both fixture harnesses |
| `build-presentation/Debug/PhlosionEngineTests.exe` (`tests-fix-03.log`) | 0 | 23/23 contracts, including the new LINEAR scale case and the hardened handshake contract |
| `powershell -File tests/animation_fixture/test_fixture_harness.ps1` (`harness-fix-02.log`, CTest #7) | 0 | Positive synthetic matrix plus 24 negative guard runs rejected (including the nine new provenance/transition/capture ones) and process/env checks |
| `powershell -File tools/animation_fixture/run_capture_matrix.ps1 -RunId m1g-run04` (`runner-run04.log`) | 0 | 24 native captures (4 cases x 3 APIs x runtime/editor), `failure_count = 0` |
| `AnimationFixtureGuard compare` m1g-run04 vs m1g-run03 (`run04-vs-run03/`) | 0 | Every capture pixel-identical (worst max channel difference 0) |
| `PublicAssetFixtureGuard matrix --require-complete` on the retained archive manifest (`archive-revalidation.log`) | 0 | 24 entries pass; archive runtime/editor/plugin/guard hashes unchanged by this pass, so no archive recapture was required |
| `artifacts/m1-g/diff/extraction-diff.ps1` (`diff/extraction-diff.log`) | 0 | Baseline `Model::buildPoseMatrices` and extracted `samplePose` reduce to 183 identical normalized statements, SHA-256 `0b8876217e01e5b897c70ccbf106d8587946a2928cf3da32f9067c27a04a0a6f` |
| Child-project configure/build/run (`artifacts/m1-g/child-default-off/`) | 0 | Fixture option OFF by default with hosts absent; with engine tests enabled the fixture libraries exist and hosts stay absent; a child linking only `Phlosion::Engine` runs the shared sampler with no graphics context |

Numbers from `m1g-run04`:

- Worst cross-API pair: normalized MAE 0.0000038, RMSE 0.00029, changed ratio 0.000029, max channel difference 10-11, under the unchanged 0.01 / 0.08 / 0.05 limits at tolerance 8.
- Authored case differences 4.77-9.73% changed pixels (`cross-case-summary.txt`), above the authored minima (0.01 / 0.003 / 0.005).
- Expected geometry: `base_anchor_shift = 0`, `tip_motion` 0.077 / 0.134 / 0.160, `tip_vertical_rise` -0.076 / -0.124 / -0.124, rigid base separation 0.389-0.711, rigid middle separations 0.116-0.243.
- Measured content: background green 0.946-0.953, rig coverage 0.047-0.053, middle landmark on rig 1.0, base/tip landmark rig fractions 0.4-0.61, red base and yellow tip dominance 1.0, rigid base/middle alternative background 1.0.
- Palette transition: one address per host process for bind/requested draws; contents differ on all posed cases, identical on `bind`; step case reports `clip_index = 1`, all other cases `0`.
- All four host/guard binaries recorded in `m1g-run04/manifest.json` still hash-match the on-disk files.

Visual QA: contact sheets and full-resolution runtime/editor captures were inspected (`m1g-run04/contact-sheet.png`, `m1g-run04/runtime/d3d12/linear-near-endpoint/capture.png`, `m1g-run04/editor/vulkan/linear-intermediate/capture.png`).

## Limits and risks

- Coverage is a public two-joint strip rig on one Windows/NVIDIA GTX 1070 Debug host. It does not qualify private characters, production materials, morph targets, CUBICSPLINE tangents, instanced or indirect skinned paths not exercised, other GPUs/drivers, or performance budgets.
- The legacy OpenGL-only `Model::drawAnimated` path is not exercised by the native fixture; its behavior is preserved by the shared sampler plus unchanged draw code, supported by the extraction differential proof and M1-F import contracts.
- Synthetic captures and CLI mutations are hermetic guard checks; only the 24 native images exercise real backends. The harness retains its diagnostic temp directory by design.
- At zero degrees a stale bind palette and the rigid tip transform are the identity, so those two defects are only distinguishable on posed cases; disabled skinning, a mesh-space palette and blank frames are distinguishable on all cases.

## Decisions requiring Astra

- The archive fixture was not recaptured: its host/editor/plugin/guard binaries are byte-identical to the retained `m1g-archive-02` manifest and the retained matrix revalidates. If you want a fresh archive capture anyway, say so and I will run it.
- The new guard keeps its own small image primitives as agreed; no shared extraction was attempted.
- `docs/VERIFICATION.md` / `docs/README.md` integration remains yours; `docs/ANIMATION_VERIFICATION.md` is written to be linked from them.

## Evidence locations

- Builds/tests: `artifacts/m1-g/build-full-05.log`, `ctest-05.log`, `tests-fix-03.log`, `harness-fix-02.log`.
- Animation matrix: `artifacts/m1-g/m1g-run04/` (`manifest.json`, `matrix.json`, `guard.log`, `contact-sheet.png`, per-entry captures/logs/handshakes/metrics, `measured-content-summary.txt`, `expected-geometry-summary.txt`, `cross-case-summary.txt`).
- Pixel-identity to the pre-fix captures: `artifacts/m1-g/run04-vs-run03/`; earlier runs `m1g-run01`, `m1g-run02`, `m1g-archive-01`, `m1g-archive-02` retained.
- Archive revalidation: `artifacts/m1-g/archive-revalidation.log`, `artifacts/m1-g/archive-regression/m1g-archive-02/matrix-revalidated.json`.
- Extraction proof: `artifacts/m1-g/diff/`; child project: `artifacts/m1-g/child-default-off/`; harness negatives: the retained temp directory printed by `harness-fix-02.log`.

## Next checkpoint

None pending from this pass. Awaiting Astra's review of the fix diff, the hardening negatives and the retained `m1g-run04` evidence.
