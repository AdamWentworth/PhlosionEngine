# Engine verification

## Automated source and CPU checks

[Engine CI](../.github/workflows/ci.yml) runs on pushes to `main`, pull requests
and manual dispatch. It uses the Windows Visual Studio 2026 runner, restores
the manifest-pinned vcpkg dependencies, builds all enabled Debug targets with
warnings treated as errors, and runs CTest.

The build includes the native editor, plugin probe and Vulkan shader programs.
It does not fetch a game, private assets or optional package repositories.
No test in the current engine CTest suite needs a graphics context.

| CTest entry | Coverage |
| --- | --- |
| `PhlosionEngineContracts` | Twenty-five C++ contracts: material profiles, containers, typed static meshes, scenes, environment patches, asynchronous asset loading, the dependency-aware asset graph session, staged scene/prefab archive activation, public archive/render-fixture data and image guards, existing glTF scene/skeleton/clip import, material metadata, real PNG/JPEG texture decoding, temporary-fixture ownership, shared LINEAR/STEP/CUBICSPLINE pose sampling, model-cache sampler codec/version identity, animation fixture/palette/image guards, descriptors, renderer preference, camera pan, plugin ABI, preview routing, reload and performance accounting |
| `PhlosionEngineSemanticBoundary` | Removed game systems, identifiers, materials and former source paths cannot return to engine code/shaders |
| `PhlosionPackedNormalContract` | Shared packed-normal source contract |
| `PhlosionMaterialDebugViewContract` | Material debug-view source contract |
| `PhlosionReviewLightingProfileContract` | Generic review-lighting source contract |
| `PhlosionPublicAssetFixtureHarness` | CPU-only synthetic image/manifest negative tests, capture provenance and native process handling; enabled when fixture hosts and PowerShell are available |
| `PhlosionAnimationFixtureHarness` | CPU-only skeletal fixture image/provenance/transition rejection checks and process handling; enabled when animation fixture hosts and PowerShell are available |

Local equivalent:

```powershell
cmake --preset vs2026 -DPHLOSION_BUILD_EDITOR=ON -DPHLOSION_BUILD_TESTS=ON -DPHLOSION_WARNINGS_AS_ERRORS=ON
cmake --build --preset debug --parallel 4
ctest --preset debug
```

Tests enforce particular contracts, not a blanket claim of correctness. Review
the changed implementation and relevant consumer behavior as well.

The asynchronous loading contract and its retained-byte-budget limits are
described in [ASYNC_ASSET_LOADING.md](ASYNC_ASSET_LOADING.md). The loader tests
are CPU only: they coordinate a synthetic provider with condition variables and
never create a graphics context. `PhlosionEngineContracts` carries a 120 second
CTest timeout as a regression backstop so a blocked loader cannot hang the suite
unboundedly.

The dependency-aware graph session, its required-closure and hash rules, and its
accounting limits are described in
[ASSET_DEPENDENCY_LOADING.md](ASSET_DEPENDENCY_LOADING.md). That contract is also
CPU only: it drives a synthetic in-memory provider, gated reads and public PHRC
fixtures, and creates no graphics context.

The staged scene/prefab activation session, its candidate lifecycle and its
snapshot ownership rules are described in
[ARCHIVE_ACTIVATION.md](ARCHIVE_ACTIVATION.md). That contract is CPU only as
well: it composes the graph session with the existing typed archive stores,
drives a synthetic in-memory provider with gated reads, and creates no graphics
context. It makes no game, editor or renderer integration claim.

The [static mesh contract](MESH_ASSETS.md) covers deterministic PHMS encoding,
literal little-endian payload bytes, strict schema/UTF-8/geometry validation,
limits, multi-submesh slot preservation and unchanged outputs on failure.
Its fixture consumer also exercises two-slot activation and preservation of
active A when candidate bindings or mesh semantics fail. These are CPU checks;
the rendered matrix below uses the existing single-slot visual scene.

The [glTF import contracts](GLTF_IMPORT_VERIFICATION.md) exercise the existing
file loader, scene/skeleton/clip importer and CPU material resolver using tiny
authored public fixtures. They verify imported values and fixture ownership;
M1-H also samples imported cubic translation/rotation/scale curves. The separate
[texture contract](GLTF_TEXTURE_VERIFICATION.md) added in M1-I decodes actual
PNG/JPEG data through the existing five CPU helpers, checks sampler metadata,
and feeds real decoded alpha into the material resolver. These CPU checks do
not deform GPU vertices or qualify GPU sampling, UV binding or material
appearance. Native rendering requires separate evidence.

The [animation contracts](ANIMATION_VERIFICATION.md) exercise the shared CPU
pose evaluator, root-motion/loop policies, hierarchy transforms, scale
compensation, cubic tangent fidelity, malformed cubic channels, the production
cache sampler codec/version gate and the public rig's palette space. Synthetic image and manifest
checks validate the native capture guards without a graphics context. The
separate native matrix below supplies bounded GPU evidence.

## Native rendering and editor changes

OpenGL, Vulkan and Direct3D 12 are equal supported targets. Renderer or visual
editor work requires all affected surfaces on all three native APIs, including
expected-content checks at matching scene/simulation frames. Startup logs and
source contracts alone cannot prove visual correctness.

Use the consuming project's scene and material fixtures when the change affects
its rendering. Keep game content and capture harnesses in that project. The
current Autochess consumer documents its [parity contract](https://github.com/AdamWentworth/PokemonAutochess/blob/master/docs/RENDERER_PARITY_CONTRACT.md)
and [test plan](https://github.com/AdamWentworth/PokemonAutochess/blob/master/docs/TEST_PLAN.md).
Generic engine defaults also need independent coverage when changed.

- Keep resolution, simulation time, camera, content and presentation settings comparable.
- Rebuild compatible editor/project-plugin pairs when public interfaces change.
- Check models, materials, animation, scene visibility and UI, not only pixel similarity.
- Record backend, source revision and coverage limits. Do not loosen thresholds
  or silently fall back to another API to turn a failed check green.

Hosted CI is a Windows build/CPU gate. Full GPU qualification and performance
measurement remain local; this workflow does not certify other operating systems,
every GPU/driver, all project content or every editor scenario.

## Recorded evidence

On 2026-09-20, Astra accepted M1-I's [glTF texture decoding
verification](GLTF_TEXTURE_VERIFICATION.md), based on committed M1-H `4ab3ac7`
plus tests and documentation. The production loader feeds real encoded PNG/JPEG
fixtures to the five existing CPU decode helpers. Exact PNG pixels, orientation
and alpha are checked across external/data URI and padded bufferView routes,
including GLB; JPEG uses a flat patch with a per-channel tolerance of 3 out of 255
and exact opaque alpha. Coverage includes role selection, wrapping/filter enums,
defaults and factors, real alpha-mode integration, corrupt images, bounded
invalid selections and independent retained outputs.

The final full Debug build with warnings as errors, 25 CPU contracts and all
seven CTest entries passed (`artifacts/m1-i/final-build.log`, `final-tests.log`,
`final-ctest.log`; CTest 86.70 seconds). Astra reviewed the tracked/new source
and one consolidated correction: corrupt payloads are bound to all five roles
and asserted before decoding; the retention case uses the same source image;
empty-byte defect assertions live only in an artifact probe; debug environment
flags are scoped off. Four deliberate mutations fail on pixel, sampler or
corrupt-image binding errors and are reverted. An ambient-debug probe produces
zero contract texture dumps with the guard and 32 with a temporary inverse
mutation. The retained empty-byte patch applies cleanly to the final source.

This is CPU evidence only, with no production or shader changes and no new
native captures. Remaining gaps include encoded-byte base-factor application,
source-derived factor-only emissive loss on the legacy OpenGL Model path,
inconsistent empty-byte placeholders, absent runtime calls to three decode
helpers, per-role UV binding and compressed image formats. World-backend
material behavior requires separate tracing and native qualification; these
tests do not certify five-map rendering or cross-API material parity.

On 2026-09-20, Astra accepted M1-H's [cubic animation
fidelity](ANIMATION_VERIFICATION.md), based on committed M1-G `afaeb1e` plus the
M1-H working tree. The existing importer retains cubic tangent triplets and the
shared sampler evaluates them for T/R/S. Cache v10 invalidates earlier lossy
results through the production identity gate. Exact/clamped keys bypass tangent
arithmetic; malformed cubic samples skip their channel. Legacy LINEAR/STEP
policies and backend/shader code are unchanged.

The final warning-clean Debug build, seven CTest entries and 24 CPU contracts
passed (`artifacts/m1-h/build-full-02.log`, `ctest-03.log`, `tests-03.log`).
Tests include non-unit intervals, multi-segment translation, two-key scale,
quaternion sign/normalization, extreme finite tangents at exact/clamped keys,
non-finite interpolation rejection, import-to-sampling and cache roundtrip.
The CLI harness independently rejects each linearized cubic image and passes
the pristine matrix after restoring mutations (`harness-02.log`).

`artifacts/m1-h/m1h-run02/` contains all 36 native animation captures (six poses,
three APIs, runtime and real editor), zero failures, expected-content checks
and stable palette transitions. Worst cross-API normalized MAE/RMSE are
0.0000074/0.00039 under unchanged limits. The original 24 images match M1-G
pixel for pixel (`compare24-run02/summary.json`); all 36 match the pre-correction
M1-H run (`compare-run02-vs-run01/summary.json`). Astra inspected the patch,
corrections, contact sheet and final binary hashes.

Archive regression evidence is `artifacts/m1-h/archive-regression/m1h-archive-final/`
with 24 passing final-build captures (`archive-final.log`). Astra found that the
older archive guard checks digest format and file size, but does not re-hash
binaries: its earlier PASS did not establish final-build provenance. The older
archive capture was superseded, and Astra independently checked all four hashes
in each final manifest against the actual binaries. Future archive evidence
reuse still requires that explicit hash check.

This qualifies one public two-joint rig on Windows/GTX 1070 Debug. Native cubic
coverage is one two-key Z-rotation curve sampled twice; broader T/R/S cases rely
on CPU contracts. Full Model cache integration, legacy Model GPU drawing,
private characters, morphs, blending, instanced/indirect skinning, materials and
performance remain outside this evidence. M1-H was committed as `4ab3ac7`.

On 2026-09-20, Astra accepted M1-G's [pose-sampling and skeletal
verification](ANIMATION_VERIFICATION.md), based on `9c0d812` plus the M1-G
working tree, subsequently committed as `afaeb1e`. Model delegates to its existing CPU evaluator through
ModelPoseSampler; the extraction preserves the inspected algorithm and public
behavior. No backend, shader, cache schema or editor-host code changed.

The final warning-clean VS2026 Debug build, seven CTest entries and all 23 C++
contracts passed: ignored `artifacts/m1-g/build-full-05.log`, `ctest-05.log` and
`tests-fix-03.log`. The new harness rejects 24 negative evidence/content cases
(`harness-fix-02.log`). Child-project checks confirm fixture hosts default off
and the shared sampler works without a graphics context. The retained extraction
comparison checks 183 normalized statements; Astra also reviewed the delegated
helpers and source-array wiring.

The final `artifacts/m1-g/m1g-run04/` records all 24 native animation captures:
four poses on OpenGL/Vulkan/Direct3D 12 in the standalone host and actual editor
plugin. Expected pixel content checks cover the anchored base, moved tip and
blended middle, independently of sampled pose results. Both hosts update one
palette allocation across the bind-to-posed transition; all recorded addresses
and changed-content checks agree. Worst cross-API normalized MAE/RMSE are
0.0000038/0.00029 under unchanged 0.01/0.08/0.05 limits and channel tolerance 8.
Final images match the previous corrected run pixel for pixel. Astra inspected
the native images and rechecked all four final binary hashes.

The archive regression captured 24 passing cases under
`artifacts/m1-g/archive-regression/m1g-archive-02/`; its binaries stayed unchanged
during review corrections, so the retained matrix was revalidated rather than
recaptured (`archive-revalidation.log`). That rigid fixture does not test poses.
Animation evidence is one public two-joint rig on Windows/GTX 1070 Debug, through
the world-mesh-textured path. It excludes private characters, the legacy
OpenGL-only Model draw path, instanced/indirect skinning, cubic interpolation,
morphs, production materials and performance budgets.

On 2026-09-20, Astra accepted M1-F's [existing glTF import
verification](GLTF_IMPORT_VERIFICATION.md), committed as `9c0d812` from baseline
`9d094c9`. Three new CPU contracts exercise real file loading and imported
scene/skeleton/clip values, material metadata and temporary-fixture ownership.
Production sources are unchanged. Review corrections made the synthetic rig
structurally coherent and isolated every test-owned temporary directory.

The final VS2026 Debug build with warnings as errors, all six CTest entries and
all twenty-one C++ contracts passed. Evidence is retained locally in ignored
`artifacts/m1-f/build-full-08.log`, `ctest-all-04.log` and
`tests-from-ctest-dir-04.log`. The helper-only two-process probe also passed
(`helper-probe-concurrent-02.log`); deliberate regressions failed before being
reverted (`regression-probe-0{1..5}.log`). No GPU run was needed for this
tests/documentation-only pass, and none is claimed. Pose sampling, skeletal
deformation, texture decoding and material appearance remain outside these
original contracts. CUBICSPLINE tangent loss was an audited limitation closed
by M1-H above; absent morph-weight channels remain unsupported.

On 2026-09-20, Astra accepted M1-E's [typed static mesh resource](MESH_ASSETS.md)
after one consolidated review-fix cycle. The reusable contract lives in
engine_core; the example consumes six required PHMS resources from its staged
dependency graph. It preserves submesh ranges, slot bindings and tangents without
changing renderer or editor-host APIs. Existing Model/glTF/cache paths remain
compatible. M1-E was committed as `9d094c9`, based on
`68aed76373c169494d9238d267cd0e50cd885ff2`.

The final VS2026 Debug build with warnings as errors and all six CTest entries
passed (eighteen C++ contracts). A child project linking only Phlosion::Core
encoded/decoded a mesh without graphics dependencies. Logs are in ignored
`artifacts/m1-e/build-full-03.log`, `ctest-03.log` and
`link-proof-{build,run}-02.log`. Regressions cover malformed UTF-8, fixed binary
bytes, a valid two-slot consumer and missing/duplicate bindings; deliberate
binding/tangent regressions failed their checks before being reverted.

The final `artifacts/m1-e/m1e-run03/{manifest,matrix}.json` and `guard.log`
record all 24 native runtime/editor cases, unchanged thresholds, zero matrix
failures and matching final binary hashes. Every capture is pixel-identical to
the accepted M1-D `reorg-run02` counterpart; comparisons are retained in
`artifacts/m1-e/m1e-run03-vs-m1d/`. Worst cross-API normalized MAE/RMSE remain
0.0000052/0.00024. Failed replacement preserves A30 exactly. This is one
Windows/GTX 1070 Debug host, not qualification of private game assets, skeletal
animation, production materials, other hardware or performance budgets. Tangent
forwarding has CPU evidence; the fixture materials do not visibly consume it.

On 2026-09-20, M1-D added the [public asset fixture](PUBLIC_ASSET_FIXTURE.md):
shared archive-driven geometry/texture presentation through a standalone host and
the actual editor project-plugin ABI. Four activation/replacement cases passed
on native OpenGL, Vulkan and Direct3D 12 in both hosts (24 captures). Expected
content checks cover checker orientation, depth occlusion, blending, rigid marker
position and changed GPU cache identities. Failed replacement is pixel-identical
to A30; cross-API worst normalized MAE/RMSE are 0.0000052/0.00024 under unchanged
0.01/0.08/0.05 parity limits and channel tolerance 8. Host frames 6/8 and logical
frames 0/30 are recorded separately; backend/frame/log/metric checks reject
fallbacks and incomplete or duplicated coverage.

The final VS2026 Debug build with warnings as errors passed, as did six CTest
entries (17 C++ contracts plus the CPU harness). Fixture hosts disabled with
tests enabled also passed five CPU entries, and a child-project configure
confirmed fixture hosts default off. Native captures and original binary
provenance are in ignored `artifacts/m1-d/fix-run03/`. Astra restored the original
board-coverage minimums (0.4 interior, 0.35 edge) during review, rebuilt and reran
CTest, then revalidated all retained captures with that stricter guard:
`artifacts/m1-d/astra-{build,ctest,matrix}.log`, `astra-matrix.json` and
`astra-manifest.json`. No additional renderer run was needed for a guard-only
change. Evidence is one Windows/NVIDIA GTX 1070 host; it does not qualify private
game content, skeletal animation, production material pipelines or budgets.

Before committing M1-D, the sample and its verification machinery were separated:
examples/public_asset_fixture holds the runnable sample, tools/public_asset_fixture
holds guards/capture tools, and tests/public_asset_fixture holds the PowerShell
harness. Rebuild and six CTest entries passed after relocation. The moved runner
captured all 24 native cases again with identical reported metrics and unchanged
guards: `artifacts/m1-d/reorg-run02/`, `reorg-build-final.log` and
`reorg-ctest-final.log`. Implementations were checked against pre-move hashes;
the sample no longer links the verification library.

On 2026-09-20, the M1-C staged archive activation iteration added
`PhlosionArchiveSession` with candidate staging over the accepted graph loader,
the existing typed archive decoders and explicit activation. The active
snapshot is preserved on candidate graph/decode failure, cancellation and
discard; `unload` clears the session's active reference,
and snapshots already handed to callers stay readable afterwards, as does a
retained snapshot after the provider itself is released. The existing
`build-presentation` Debug tree (VS2026, editor/tests enabled, engine warnings
treated as errors) rebuilt cleanly, and the full CTest set passed (5 of 5
entries, including all sixteen C++ contracts). Logs are local under
`artifacts/m1-c/`. This iteration is pure CPU and produces no rendering or
visual-parity evidence.

On 2026-09-20, the M1-B dependency-aware asset iteration added
`PhlosionAssetGraphLoader` with its required-closure traversal, hash
constraints, cycle checks, bounded asset/edge/byte limits, cancellation and
reset lifecycle, and the dependency-only prefab CPU fixture. The existing
`build-presentation` Debug tree (VS2026, editor/tests enabled, engine warnings
treated as errors) rebuilt cleanly, and the full CTest set passed (5 of 5
entries, including all fifteen C++ contracts). Logs are local under
`artifacts/m1-b/`. This iteration is pure CPU and produces no rendering or
visual-parity evidence.

On 2026-09-20, the M1-A asynchronous asset iteration added
`AsyncAssetLoader` with its explicit `requestStop()` lifecycle boundary, the two
CPU contracts above and their documentation. The existing `build-presentation`
Debug tree (VS2026, editor/tests enabled, engine warnings treated as errors) rebuilt cleanly
with the new `engine_core` source and standard thread linkage, and the full CTest
set passed (5 of 5 entries, including all fourteen C++ contracts). Logs are local
under `artifacts/m1-a/`. This iteration is pure CPU and produces no rendering or
visual-parity evidence.

On 2026-09-19, the presentation review built a separate `build-presentation`
Debug tree with MSVC, editor/tests enabled and warnings treated as errors.
All five CTest entries passed, including all twelve C++ contracts and the
semantic boundary. Logs are local under `artifacts/presentation-review/`.
This review changes documentation, media and CI rather than runtime behavior.

The material extraction at engine commit `4e555f2` has separate
[consumer verification](https://github.com/AdamWentworth/PokemonAutochess/blob/master/docs/CHARACTER_MATERIALS.md#boundary-verification-2026-09-19):
listed editor and native game cases across three APIs, default-engine checks,
CPU contracts and scoped performance measurements. Consult that dated record
for its exact limits. Subsequent consumer-specific qualification failures belong
in the [game's issue register](https://github.com/AdamWentworth/PokemonAutochess/blob/master/docs/OUTSTANDING_ISSUES.md).
