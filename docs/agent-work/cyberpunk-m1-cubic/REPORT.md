# M1-H completion report

STATUS: accepted by Astra after review corrections and final evidence integration.

- Task ID: M1-H (`cyberpunk_m1_cubic`), one native DeepSeek V4.1 Flash builder.
- Workspace: `D:/Projects/Phlosion/PhlosionEngine`, HEAD `afaeb1ec470434fe3da10f7c5f3c8238b2fc5068` (committed M1-G), tree dirty with this pass only.
- No commit, stage, push, deploy, dependency, credential, backend/shader or
  router change. Nothing outside this repository was edited. The worker left
  root-owned planning/integration docs untouched; Astra integrated them afterward.

## What changed

Production (behavior fix, narrow scope):

- `src/engine/render/ModelAnimationTypes.h`: `AnimationSampler` documents the
  shipped contract - one (x, y, z, w) output per key for LINEAR/STEP and the raw
  per-key glTF `[in-tangent, value, out-tangent]` triplets for CUBICSPLINE
  (`outputs.size() == 3 * inputs.size()`), rotation always xyzw.
- `src/engine/render/gltf/ModelFastGltfSceneData.cpp`: the importer no longer
  drops CUBICSPLINE tangents; it stores the raw accessor outputs unchanged.
- `src/engine/render/ModelPoseSampler.cpp`: CUBICSPLINE channels clamp the
  wrapped sample time into their own key range and evaluate componentwise cubic
  Hermite interpolation with tangents scaled by the real interval in seconds;
  rotations use all four components and normalize without a sign flip; clamped
  endpoints and exact keys return the authored value directly, before any
  interpolation, so finite FLT_MAX tangents cannot overflow a key sample.
  Malformed cubic data (fewer than two keys, not exactly three outputs per key,
  non-finite or non-increasing times, non-finite relevant components, a
  zero-length or non-finite sampled quaternion) skips that channel and leaves
  the destination's current local component untouched. The LINEAR and STEP
  lambdas and their channel application behavior are unchanged.
- `src/engine/render/ModelCacheAnimation.h/.cpp` (new, namespace
  `engine::render::model_cache`): the narrow CPU seam holding the packed-header
  identity constants plus `readAnimationSampler` / `writeAnimationSampler` over
  standard streams. No GL, no `Model`. A failed read returns false and leaves
  the caller's sampler untouched.
- `src/engine/render/ModelCache.cpp`: cache version 9 -> 10 (shared constant),
  identity check through the helper before any cached state is published, and
  real sampler read/write through the helper. The sampler byte layout, field
  order/widths and every non-animation part of the cache are unchanged.

Tests, fixture, tools and docs:

- `tests/TestModelPoseSampler.cpp`: five new cubic contracts (translation with
  two segments/non-unit intervals/asymmetric tangents and literal Hermite
  values, cubic scale, rotation normalization with a scaled curve, a
  sign-sensitive curve that must not flip, and malformed cubic data leaving the
  current local component alone) plus a LINEAR reference that must differ. A
  sixth contract samples clamped endpoints and exact first/interior/last keys
  with finite FLT_MAX tangents (authored values must come back, not NaN/skips)
  and proves a strictly interpolated non-finite result skips the channel.
- `tests/TestGltfSceneImport.cpp`: new authored cubic glTF fixture (JSON plus
  explicit little-endian bytes) loaded through `loader::tryLoad` and
  `buildSceneData`; the raw tangent triplets and xyzw order are checked, then
  `samplePose` is compared with independently hand-computed values.
- `tests/TestModelCacheAnimation.cpp` (new) and `tests/TestMain.cpp`: production
  codec contract - raw tangent roundtrip, sampling after the roundtrip,
  LINEAR/STEP one-output-per-key layout, truncated stream failing without
  publishing a partial sampler, and v10 accepted / v9 and wrong magic rejected.
- `examples/animation_fixture/`: catalog grew from four to six cases (the
  original four are unchanged), the rig gained the authored `cubic` clip, and
  the catalog now records the analytic LINEAR fallback angle per case.
- `tools/animation_fixture/`: `SynthDefect::LinearizedCubic` plus
  `--defect=linearized-cubic`, matrix acceptance is 36 unique entries, the
  pairwise/cross-case checks include the cubic cases, and the capture runner
  defaults to `artifacts/m1-h`.
- `tests/animation_fixture/test_fixture_harness.ps1`: 36-entry hermetic matrix,
  updated incomplete/duplicate mutations, and two new CLI negatives that prove
  the guard rejects a linearized cubic pose on `cubic-midpoint` and
  `cubic-early` while accepting it on the LINEAR case where it is identical.
  Each negative restores its own mutated capture in a `finally`, and the
  pristine 36-entry matrix is run again afterwards to prove the negatives did
  not depend on leftover state.
- `docs/ANIMATION_VERIFICATION.md`, `docs/GLTF_IMPORT_VERIFICATION.md`,
  `examples/animation_fixture/README.md`, this report.

## Review correction (consolidated pass)

1. `sampleCubicVec4` no longer evaluates Hermite for clamped endpoints or exact
   keys. After validating the cubic layout and a finite sample time it returns
   the authored per-key value directly (rotations are normalized by the
   wrapper), so finite FLT_MAX tangents cannot overflow a sample that does not
   need interpolation. Strictly interpolated samples still reject non-finite
   results and skip the channel. Clip wrapping, the positive-exact-duration
   `nextafter` policy and the LINEAR/STEP paths are unchanged. New contract
   `checkCubicExactKeysWithExtremeTangents` samples clamped pre-first/post-last
   and exact first/interior/last keys with FLT_MAX tangents (authored values must
   come back) and separately proves a strictly interpolated non-finite result
   skips the channel; the malformed table gained non-finite key-time,
   non-finite out-tangent and non-finite in-tangent cases.
2. `tests/animation_fixture/test_fixture_harness.ps1` restores each mutated cubic
   capture inside its own iteration (`try`/`finally`) and then runs the pristine
   36-entry matrix again (`pristine-after-linearized`) to prove the negatives did
   not pass from leftover corruption. Each cubic defect is now independently
   rejected through the real CLI.
3. Misleading comments corrected: the import test's Hermite weight is
   `h01 + 0.5*h10 - 0.25*h11` (both the translation and scale paragraphs), the
   sign-sensitive quaternion key is the -240 degree rotation about +Z
   (equivalently +120 degrees, `z = -sqrt(3)/2`, `w = -1/2`), the cache test no
   longer calls a sample at an exact multiple of the clip duration an "exact
   last key" (it is the `nextafter` policy sampling just inside the last
   segment), and the guides scope the no-key/tail policy bullets to the legacy
   LINEAR/STEP path, pin the mechanical extraction proof to historical M1-G
   (afaeb1e) and describe the legacy GL draw path as unqualified rather than
   behavior-preserved.

## Verification (final binaries; all commands from the engine root)

| Command | Exit | Result |
| --- | --- | --- |
| `cmake --build build-presentation --config Debug --parallel 4` (`artifacts/m1-h/build-full-02.log`) | 0 | Warning-clean full build of runtime, editor, plugin, guard and tests |
| `ctest --test-dir build-presentation -C Debug --output-on-failure` (`ctest-03.log`) | 0 | 7/7 entries: 24 C++ contracts, four source contracts, both fixture harnesses |
| `build-presentation/Debug/PhlosionEngineTests.exe` (`tests-03.log`) | 0 | 24/24 contracts pass, including the new extreme-tangent exact-key contract and the extended malformed table |
| `powershell -File tests/animation_fixture/test_fixture_harness.ps1 -EngineRoot <root> -GuardExe <guard>` (`harness-02.log`) | 0 | 36-entry hermetic matrix, per-iteration restores, both linearized-cubic negatives and the pristine matrix re-run |
| `powershell -File tools/animation_fixture/run_capture_matrix.ps1 -RunId m1h-run02` (`runner-run02.log`) | 0 | 36 native captures (6 cases x 3 APIs x 2 hosts), `matrix=ok entries=36 pairwise=36 cross_case=48` |
| `powershell -File tools/public_asset_fixture/run_capture_matrix.ps1 -RunId m1h-archive-final -ArtifactsRoot artifacts/m1-h/archive-regression` (`archive-final.log`, Astra integration) | 0 | 24-case final-build archive matrix passes; supersedes the earlier run |
| `AnimationFixtureGuard compare` over the 24 original cases, m1h-run02 vs retained `m1g-run04` (`compare24-run02/summary.json`) | 0 | 24/24 comparisons pixel-identical: worst MAE 0, RMSE 0, changed ratio 0, max channel difference 0 |
| `AnimationFixtureGuard compare` over all 36 cases, m1h-run02 vs pre-correction `m1h-run01` (`compare-run02-vs-run01/summary.json`) | 0 | 36/36 comparisons pixel-identical, so the sampler correction does not change the fixture's interpolated samples |
| `AnimationFixtureGuard synth/expect` smoke for all six cases (`smoke/`) | 0 | `--defect=none` passes on all six; `--defect=linearized-cubic` is rejected on both cubic cases and accepted on `linear-intermediate` |
| Independent SHA-256 checks of both final manifests (`final-provenance.json`, Astra integration) | 0 | All four identities in each manifest match actual binaries; no size-only inference |

The linearized-cubic smoke fails with concrete metrics rather than only a nonzero
exit: on `cubic-midpoint` the authored tip landmark measures
`tip_landmark_rig_fraction = 0.0` (expected >= 0.3) and
`tip_landmark_yellow_fraction = 0.0` (expected >= 0.5), i.e. the linearized tip
is absent from the authored -120 degree tip position (it sits on the -60 degree
linear pose instead).

Native matrix numbers from `artifacts/m1-h/m1h-run02` (identical to `m1h-run01`):

- `failure_count = 0`; worst cross-API pair normalized MAE 0.0000074, RMSE
  0.00039, max channel difference 10, under the unchanged 0.01 / 0.08 / 0.05
  limits at tolerance 8.
- Authored differences: bind vs the posed cases 5.4-9.7% changed pixels, bind vs
  `cubic-midpoint` 7.4% (runtime) / 8.3% (editor), bind vs `cubic-early` 8.6% /
  9.6%, `cubic-early` vs `cubic-midpoint` 5.0% / 5.6%, all above the authored
  minima.
- `runtime/opengl/cubic-midpoint` handshake: `clip=cubic`, `clip_index=2`,
  `sample_time_sec=1.0`, `expected_angle_degrees=-120`, one palette address for
  bind and requested draws with `palette_contents_changed=true`; measured
  content has `middle_landmark_rig_fraction=1.0`, `tip_landmark_rig_fraction=0.6`,
  `tip_landmark_yellow_fraction=1.0` and rigid alternatives on background.
- Archive regression reproduced its historical numbers (editor A0 vs A30 1.40%,
  editor A30 vs B30 34.55%, runtime 1.25% / 30.90%, failed replacement
  pixel-identical) with the rebuilt binaries.

Visual QA: the 36-cell contact sheet and full-resolution captures were opened,
including `m1h-run02/runtime/d3d12/cubic-midpoint/capture.png` (tip folded to the
authored -120 degrees), `m1h-run02/runtime/vulkan/cubic-early/capture.png` and
`m1h-run02/editor/vulkan/cubic-early/capture.png` (editor chrome,
`Renderer: vulkan`, -76.43 degree pose). No fallback, no blank or displaced rig.

## Decisions and limits

- Native cubic coverage is one two-key Z-rotation curve sampled at two times on
  all three APIs. Multi-segment cubic translation (keys 0/1.5/4 s, asymmetric
  tangents), two-key cubic scale, the FLT_MAX-tangent exact-key/clamped-endpoint
  behavior, the sign-sensitive quaternion curve and every malformed-data rule
  are CPU-tested only, so the matrix qualifies cubic rotation playback, not every
  cubic authoring shape.
- The cache codec contract exercises the exact production helper (identity,
  read, write) but not a full `Model` cache round-trip, which needs an OpenGL
  context; no end-to-end cache capture is claimed.
- Astra found all four recorded archive binary hashes differed after the final
  build. The earlier `archive-revalidation-02.log` PASS was insufficient: that
  guard checks digest format and file size, without hashing actual contents.
  Astra recaptured all 24 archive cases and explicitly hashed both final
  manifests' binaries. Future reuse of archive captures requires that extra
  check; a matching size or a previous deterministic relink proves no equivalence.
- `cubic-early`'s analytic angle is the authored literal
  -76.4264214034764 (2*atan2(-0.541265877365274, 0.6875), derived from the
  authored quarter-interval components and documented in the case catalog), verified
  against the sampler and used independently by the guard.
- Astra updated the roadmap's cache version and animation coverage, the
  documentation index, verification record and accepted checkpoint.
- The legacy OpenGL-only `Model::drawAnimated` path is not exercised by the
  fixture. Its draw code is unchanged and it samples through the shared
  corrected evaluator, so its CUBICSPLINE playback changes with the rest of the
  engine by design; the native legacy draw path itself remains unqualified.
  This pass also does not qualify private characters, other GPUs/drivers, morph
  targets, blend graphs, animation events, attachments or performance budgets.

## Evidence locations

- Raw logs: `artifacts/m1-h/` (`build-01..04`, `build-full-01`, `build-full-02`,
  `tests-01..03`, `ctest-01..03`, `harness-01`, `harness-02`, `runner-run01`,
  `runner-run02`, `archive-run01`, `archive-revalidation-02`, `archive-final`).
- Animation matrix: `artifacts/m1-h/m1h-run02/` (`manifest.json`, `matrix.json`,
  `guard.log`, `contact-sheet.png`, per-entry captures/handshakes/logs); the
  earlier `m1h-run01` is retained for the 36/36 pre-correction comparison.
- Final archive regression: `artifacts/m1-h/archive-regression/m1h-archive-final/`
  plus `artifacts/m1-h/archive-final.log` and `final-provenance.json`. Earlier
  archive captures/revalidation are retained only as superseded evidence.
- Baseline comparisons: `artifacts/m1-h/compare24-run02/summary.json` and
  `artifacts/m1-h/compare-run02-vs-run01/summary.json`; guard smoke synthetics:
  `artifacts/m1-h/smoke/`.

## Next checkpoint

Accepted by Astra; work remains uncommitted for the user's next review/commit
instruction. See CHECKPOINT.md for the acceptance boundary and remaining M1 work.
