# M1-K implementation report (worker)

STATUS: historical first Flash handoff, superseded by Astra correction.

This report describes `m1-k-final4` before review. Astra rejected that handoff
for the issues in [ASTRA_REVIEW.md](ASTRA_REVIEW.md). Flash began correcting
them, stopped with provider HTTP 402, and the user explicitly authorized Astra
to finish. See [CHECKPOINT.md](CHECKPOINT.md) and [ACCEPTANCE.md](ACCEPTANCE.md)
for the final disposition and evidence; the older results below do not qualify
the corrected sources.
Task: `/root/cyberpunk_m1_integration` (astra_flash_builder, DeepSeek V4.1
Flash/high) in `D:/Projects/Phlosion/PhlosionEngine`.
Baseline: `cc600e1f9722b7da7c198102a26763a469e8ee4c` (main, unchanged). All work
is uncommitted; nothing is staged.

## What changed

Engine (new):

- `src/engine/render/gltf/GltfSceneAsset.{h,cpp}`: CPU-owned, synchronous glTF
  scene snapshot. Publishes only on success (failure leaves the caller's shared
  asset untouched), owns geometry/indices/nodes/skins/clips/materials/pixels,
  exposes source identity + generation, bounded load options with checked
  arithmetic, per-feature rejection diagnostics, and pose sampling that reuses
  `animation::samplePose` and forms per-primitive palettes as
  `inverse(meshGlobal) * jointGlobal * inverseBind`.
- `src/engine/render/gltf/GltfSceneRenderer.{h,cpp}`: generic world-renderer
  adapter that submits the owned vertices/indices, five map roles and scalar
  factors through `drawWorldIndexedMeshTexturedCached`, binds palettes with
  `gpuSkinningMode = 0`, composes the instance transform with each node global,
  and reports draw/triangle/palette counts. It retains no renderer pointer.

Engine (narrow edit):

- `ModelFastGltfTextures.{h,cpp}`: additive `BaseColorDecodePolicy`
  (`LegacyBakeFactor` default = existing behavior; new `PreserveSource` returns
  source pixels unchanged and white for an absent base map). No existing caller,
  cache entry, assertion, shader, backend or ABI changes.

Sample, verification and tests:

- `examples/scene_fixture/` (case catalog, authored content/analytic rig,
  shared session/draw/handshake, runtime host, editor plugin, README).
- `tools/scene_fixture/` (oracle + matrix guard CLI and the capture runner).
- `tests/TestGltfSceneAsset.cpp`, `tests/TestGltfSceneAssetRenderer.cpp`,
  `tests/TestSceneFixture.cpp`, `tests/scene_fixture/test_fixture_harness.ps1`.
- CMake/TestMain registration (fixture option, library, verification library,
  hosts, CTest harness).
- Docs: `docs/GLTF_SCENE_ASSETS.md`, `docs/SCENE_FIXTURE.md`; bounded accuracy
  note in `docs/GLTF_TEXTURE_VERIFICATION.md`.

## Verification (all on the final Debug binaries)

| Command | Exit | Result |
| --- | --- | --- |
| `cmake --build build-presentation --config Debug --parallel 8` | 0 | Full Debug build, warnings as errors, no warnings |
| `build-presentation/Debug/PhlosionEngineTests.exe` | 0 | 29/29 CPU contracts (26 existing + glTF scene asset, glTF scene draw adapter, scene fixture) |
| `ctest --test-dir build-presentation -C Debug --output-on-failure` | 0 | 9/9: contracts, 4 CMake contract tests, archive/animation/material/scene harnesses |
| `tools/scene_fixture/run_capture_matrix.ps1` (`m1-k-final4`) | 0 | 36 unique captures (6 cases x 3 APIs x 2 hosts), guard PASS, 0 failures |
| `tools/public_asset_fixture/run_capture_matrix.ps1` (`m1-k-archive`) | 0 | 24 archive captures regenerated on the final binaries |
| `tools/animation_fixture/run_capture_matrix.ps1` (`m1-k-animation`) | 0 | 36 animation captures regenerated |
| `tools/material_fixture/run_capture_matrix.ps1` (`m1-k-material`) | 0 | 18 material captures regenerated |
| `tests/scene_fixture/test_fixture_harness.ps1` | 0 | Pristine synthetic matrix accepted; 17 adversarial mutations rejected; pristine re-accepted |

Scene matrix metrics (`artifacts/m1-k/matrix/m1-k-final4/validation.json`):
`capture_rows = 36`, failures `0`; limb-tip analytic coverage `1.0` in all 36
captures; bind-vs-pose limb-region changed ratio `0.214`; replacement-vs-original
character-region changed ratio `0.549`; head-region luminance `218.3` (emission
on) vs `77.3` (emission off).

Recorded binary identities for that run: runtime
`2c07c98eea20ac7cf13891c2dd0a4ddced72b189676bcca27c2912fe7fa75ed8`
(4,880,896 bytes), editor
`b8522977678af50925b3869827b1149cd5cb5eb2be1858cb2fe4ff8ebb0018cb`
(7,219,200), plugin
`25dec6b1fc5e3a19149fa240ddc5cbedd2f50fa23b54e1be2c957499a0e53d86`
(2,343,424), guard
`b61ce7283ec2c174e9e979434b17f4e7fece9362d41a8d9ab20c1499cf18bf0b`
(2,138,624); the guard re-hashes those files and rejects a mismatch.

Additional cross-case check on the regenerated d3d12 editor captures: the
`failed-replacement` capture is pixel-identical to the `a-pose` capture
(changed ratio 0), `emission-off` differs from `a-pose` only in the emissive
surface (0.005), and `unloaded` differs from `a-pose` by 0.114 as the scene
content disappears.

Evidence retained under the ignored `artifacts/m1-k/`: `matrix/m1-k-final3`
(superseded) and `matrix/m1-k-final4` (manifest, validation, contact sheet,
per-capture PNG/handshake/stdout/stderr/
editor metrics, guard log), `regression/{archive,animation,material}` with logs,
and the historical `m1-k-final1`/`m1-k-final2` runs kept for provenance. Contact
sheets were inspected: the character, weighted limb, wall, floor and prop appear
in every content case, B shows the changed geometry/material, and the unloaded
case shows the background only.

`artifacts/m1-k/{probe,probe2,smoke}` hold the exploratory single-capture images
used while diagnosing the background-coverage and editor-viewport alignment
issues; they are raw ignored evidence, not part of the accepted matrix.

## Notes, limits and decisions needing Astra

- No renderer, shader, Model, cache, ABI or schema change was needed; the
  material decision (separate base factor in `vertexColorMul`, implicit-white
  absent emissive map) was satisfiable entirely inside the new scene path.
- Two run-specific integration notes found and fixed inside scope: the authored
  background quad is submitted with depth testing disabled so it always covers
  the viewport on frames whose scene content shrank (for example after unload),
  and the guard locates the authored viewport inside an editor window capture by
  its exact background-bounded origin (runtime and editor renders are then
  pixel-identical).
- The parity thresholds are the standing ones (MAE 0.01, RMSE 0.08, changed
  ratio 0.05 at channel tolerance 8); `maxChannelDiff` is not used as a separate
  gate because isolated anti-aliased edge pixels exceed it while the changed
  ratio stays far below the approved bound. No threshold was loosened.
- Async/archive scene import, GPU cache release for CPU snapshots, arbitrary
  glTF/PBR conformance and private-content qualification remain follow-up work.
- Remaining limits: import is synchronous; parser/temporary memory and file
  reads are outside the load-option accounting; the sample is small public
  content.
- The guard contains a small self-contained SHA-256 and path-containment helper
  instead of depending on the material fixture's verification library, so the
  scene fixture can be built without the material fixture enabled.
- The sample scopes the shared decoder's diagnostic environment flags off around
  every load, so an ambient `PHLOSION_GLTF_*` setting cannot write debug dumps
  outside the run-owned staging directory or affect a capture. The engine scene
  path itself still inherits whatever the shared decoder honors when a consumer
  sets those flags; that is recorded in the scene-assets guide.
