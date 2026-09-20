# M1-F worker report: verify the existing glTF character-import path

Status: ready_for_review
Task: M1-F (`/root/cyberpunk_m1_gltf`), single native astra_flash_builder
Workspace: `D:/Projects/Phlosion/PhlosionEngine`
Baseline: `9d094c91b9b801459069dedfd8eaaac213c821b2` (`main`, clean apart from
this pass's untracked `docs/agent-work/cyberpunk-m1-gltf/` planning files)
Date: 2026-09-20

## Changed paths and behavior

Tests, registration and documentation only. No `src/`, shader, example, tool,
package or other-repository file was edited, and no production behavior changed.
For review: `tests/TestGltfSceneImport.cpp` and
`tests/TestGltfMaterialImport.cpp` hold the two import contracts,
`tests/TestGltfImportFixture.cpp` holds the fixture-ownership regression,
`tests/gltf_import/GltfImportTestSupport.h` holds the shared fixture helpers,
`CMakeLists.txt` and `tests/TestMain.cpp` register all three contracts, and
`docs/GLTF_IMPORT_VERIFICATION.md` records the CUBICSPLINE limitation.

Astra's second focused correction changed only `tests/TestGltfImportFixture.cpp`
and this report. The shared helper, the two import contracts, the guide,
`CMakeLists.txt` and `tests/TestMain.cpp` are unchanged from the previous
revision; the helper was edited only for a temporary, reverted regression probe.

- `tests/TestGltfSceneImport.cpp` (new): `test_gltf_scene_import_contract`
  authors a tiny public character document (JSON plus explicit little-endian
  `.bin` data) in its own temporary directory, loads it through the production
  `engine::render::gltf::loader::tryLoad`, runs `fastgltf::validate` on the
  parsed asset and inspects the production
  `engine::render::gltf::model::buildSceneData` output.
- `tests/TestGltfMaterialImport.cpp` (new):
  `test_gltf_material_import_contract` loads an authored materials document the
  same way, validates it and calls the production
  `engine::render::gltf::model::resolveMaterialRenderInfo` with literal
  `Model::CPUTexture` pixel arrays.
- `tests/TestGltfImportFixture.cpp` (new): `test_gltf_import_fixture_contract`
  is the narrow regression for fixture-helper ownership: simultaneous and
  concurrent instances with one prefix receive distinct directories beneath the
  system temporary directory, a destroyed instance removes only its own
  directory, task-owned sentinel content survives creation, parallel creation
  and cleanup, and unsafe prefix/child-name components are refused. Its sentinel
  directories are uniquely named and created by the contract itself; no fixed
  global temp path is inspected, written or removed.
- `tests/gltf_import/GltfImportTestSupport.h` (new): shared helpers only - an
  RAII fixture directory that reserves its unique name with `create_directory`
  (retrying collisions, never predeleting a preexisting path, and cleaning up
  only what it created), an explicit little-endian byte writer, an `expect`
  helper and small value comparisons/formatting. No importer or math is
  duplicated.
- `CMakeLists.txt` (modified, +3 lines) and `tests/TestMain.cpp` (modified, +5
  lines): the three new test sources, declarations and registrations on the
  existing `PhlosionEngineTests` target.
- `docs/GLTF_IMPORT_VERIFICATION.md` (new): the public guide for what exists,
  what these CPU contracts execute, the recorded engine policies and the
  evidence limits.

## Coverage delivered against the PLAN test contract

Scene contract:

- Named hierarchy with a translated root, a rotated and nonuniformly scaled
  joint, authored child ordering (`[3, 2]`), per-node mesh/skin indices and
  scene roots for three cases: absent `scene` member (first scene `[0, 4]`),
  authored `"scene": 1` (second scene `[0]`) and a document with no scene list.
  Every authored scene root list holds parentless nodes only: the hierarchy root
  plus the spare root in the first scene, and the hierarchy root alone in the
  second.
- The positive fixture is structurally coherent rather than merely parseable:
  the triangle carries JOINTS_0 (VEC4 UNSIGNED_BYTE) and WEIGHTS_0 (VEC4 float),
  joint indices address the two joints of skin 0, weights are normalized per
  vertex, the skinned node references mesh 0 and skin 0 together, and
  `fastgltf::validate` accepts every positive document. That validation is a
  parse-level structural check and is recorded as such; it is not spec
  conformance, appearance or rendering evidence.
- Skin joint ordering (`[2, 1]`), nonidentity inverse bind matrices asserted
  per column, and an omitted-inverse-bind skin resolving to identity.
- Two clips (`Idle`, `Walk`) with LINEAR and STEP translation/rotation/scale
  samplers, authored key times and values including quaternion xyzw order,
  sampler/target/path associations, clip names and duration taken from the last
  key across all samplers (1.5 s and 2.0 s).
- Re-import into prefilled containers with a null name list: exact container
  sizes and contents, no accumulation of stale nodes/skins/clips.
- Loader-boundary failures: missing file, malformed JSON and a missing required
  external buffer all return no value from `tryLoad`. A valid document with no
  content imports empty containers; a document without scenes imports nodes
  with empty scene roots.

Material contract:

- Invalid material index (`-1`, material count, `999`) and the `DefaultOpaque`
  material both return the documented default render info.
- `AuthoredPbr` asserts metallic `0.3`, roughness `0.6`, normal scale `2.5`,
  occlusion strength `0.4`, emissive factor `(0.25, 0.5, 0.125)` with
  `KHR_materials_emissive_strength` `2.0` applied once to `(0.5, 1.0, 0.25)`,
  OPAQUE, default cutoff `0.5` and double-sided `true`.
- `Cutout` asserts MASK mapping with cutoff `0.35`, unchanged by an opaque base
  texture; `Glass` asserts BLEND with cutoff `0.25`.
- The current blend-normalization policy is recorded with literal 2x2 textures:
  no base pixels and intermediate alpha stay BLEND, opaque alpha becomes OPAQUE,
  mostly binary alpha becomes MASK, and the binary-cutout path clamps an
  authored cutoff of `5.0` into `0.9`. The test comments and the guide state
  that this is engine policy, not a glTF guarantee.

Fixture-helper contract:

- Two simultaneous instances with one prefix, and eight concurrently created
  instances, receive distinct directories directly beneath the system temporary
  directory; each writer's marker exists while both live, and a destroyed
  instance removes only its own directory.
- Every sentinel path in this contract is a directory the test itself reserves
  with a unique name and `create_directory` before any fixture instance exists;
  its marker content survives instance creation, parallel creation and cleanup,
  and the sentinel directory is removed only by the RAII owner that created it,
  on all returns. No fixed global temp path is inspected, overwritten or
  removed, and nothing is deleted when acquisition fails.
- Unsafe prefixes (`..`, `a/b`, `with space`, dot or colon names) are refused
  before any filesystem call with no path exposed, and unsafe child names
  (`../x`, `sub/x`, empty, whitespace) are refused without creating entries or
  writing anywhere. A refused relative traversal is checked against a uniquely
  owned task target whose content must stay unchanged, rather than against an
  assumption about an unrelated global path.

All three contracts are CPU only: no `Model` construction, texture upload,
graphics context or GPU work is involved.

## Verification

| Command | Exit | Result |
| --- | --- | --- |
| `cmake --build build-presentation --config Debug --parallel 4` (VS2026, editor/tests on, warnings as errors) | 0 | Whole tree rebuilt; `PhlosionEngineTests` linked with all three new sources |
| `ctest --test-dir build-presentation -C Debug --output-on-failure` | 0 | 6/6 entries passed: `PhlosionEngineContracts` (21 C++ contracts), semantic boundary, packed normal, material debug view, review lighting profile, public asset fixture harness |
| `build-presentation/Debug/PhlosionEngineTests.exe` run from `build-presentation` (the CTest build directory) | 0 | All contracts `[PASS]`, including `gltf_scene_import_contract`, `gltf_material_import_contract` and `gltf_import_fixture_contract` |
| Helper-only concurrency harness (`artifacts/m1-f/helper-probe-concurrent.ps1`, uncommitted): two independent processes with one unique owned prefix and sentinel, short-lived process exiting first | 0 | Both processes created four distinct directories each, the long-lived process kept every marker across the other process's exit and cleanup, the sentinel directory and its content were unchanged, and no temp leftovers remained |
| Deliberate regression probes (six changes across five runs, each reverted before the final build): child order expected as `[2, 3]` plus emissive expected as strength applied twice; inverse-bind bytes written row-major; helper reverted to fixed-name predelete (twice, before and after the sentinel rewrite); one JOINTS_0 index authored out of range | 1 | Every probe failed with a precise message, e.g. `hips children must keep the authored [3, 2] order`, `AuthoredPbr: emissive factor (0.5, 1, 0.25) != (1, 2, 0.5)`, `skin 0 inverse bind 0 columns[(2, 0, 0, 1), ...] != columns[(2, 0, 0, 0), ...]`, `simultaneous instances must use distinct directories` and `JOINTS_0 indices must address the two joints of skin 0` |
| `git diff --stat 9d094c9 -- src assets examples tools` | 0 | Empty: production, shader, example and tool sources are unchanged |
| `git diff --check` | 0 | No whitespace errors |

Retained evidence under ignored `artifacts/m1-f/`:

- `build-full-08.log`: final warnings-as-errors Debug build of the whole tree.
- `ctest-all-04.log`: final six-entry CTest run.
- `tests-from-ctest-dir-04.log`: final direct contract run from the CTest build
  directory.
- `helper-probe-configure.log`, `helper-probe-build.log`,
  `helper-probe-concurrent-0{1,2}.log`: the helper-only cross-process
  concurrency validation.
- `regression-probe-0{1..5}.log` with their build logs: the deliberate failing
  probes that show the contracts and the fixture regression discriminate.
- `build-full-{01,02,03,05,06,07}.log`, `build-tests-04.log`,
  `build-corrections-0{1,2,3,4}.log`, `tests-direct-{01,02}.log`,
  `tests-direct-corrections-0{1..4}.log`, `ctest-all-0{1,2,3}.log`,
  `tests-from-ctest-dir-0{1,2,3}.log`: iteration history from all three passes,
  including the compile, fixture-layout and temporary-root failures, all fixed
  in scope.

No GPU run was performed and none is claimed. No other repository was modified;
pre-existing modifications in the game and package repositories were left
untouched.

## Limits and outstanding risks

- The contracts prove parsing and mapping, not appearance: no pose sampling,
  skinning evaluation, raster output, texture decode or per-backend parity is
  exercised. The guide records this explicitly.
- The positive fixture is coherent but still not deformation evidence: nothing
  transforms a vertex by a skin, so JOINTS_0/WEIGHTS_0 presence is structural
  only.
- `buildSceneData` keeps only the value of each CUBICSPLINE key and drops in/out
  tangents, while `ModelAnimation` samples non-STEP channels with
  `glm::mix`/`glm::slerp`. This pass documents the limitation in
  `docs/GLTF_IMPORT_VERIFICATION.md` instead of freezing the lossy behavior in a
  test; restoring cubic fidelity is a later bounded runtime change with native
  multi-API qualification.
- Morph-target weight channels remain unrepresented (`ChannelPath` has
  translation, rotation and scale only), as do advanced blending, events,
  root-motion consumption and managed attachments.
- `resolveMaterialRenderInfo` also returns normal, metallic, roughness and
  occlusion factors; the current `Model` path forwards emissive factor, alpha
  mode, cutoff and double-sided state to `Submesh`, so the other fields are CPU
  metadata in this contract.

## Decisions for Astra

- Root-owned integration is visible in the working tree while this report was
  finalized: `docs/README.md` links the new guide, and `docs/VERIFICATION.md`
  now records twenty-one C++ contracts plus the glTF paragraph. This worker did
  not edit those files.
- The blend-normalization policy is pinned by `caseBlendNormalizationPolicy` as
  the PLAN required. Only the CUBICSPLINE tangent loss was deliberately recorded
  in documentation instead of frozen in a test, per the PLAN's instruction not
  to enshrine the loss as desired behavior.
- No further decision is outstanding from this worker; whether the CUBICSPLINE
  tangent loss becomes a bounded runtime change (with native multi-API visual
  qualification) stays with Astra.
