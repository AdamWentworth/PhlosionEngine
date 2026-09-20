# M1-E worker report: typed static mesh resource and its first consumer

Status: ready_for_review
Task: M1-E (`/root/cyberpunk_m1_mesh`), single implementation writer
Workspace: `D:/Projects/Phlosion/PhlosionEngine`
Baseline: `68aed76373c169494d9238d267cd0e50cd885ff2` (`main`, clean apart from
this pass's untracked `docs/agent-work/cyberpunk-m1-mesh/` planning files)
Date: 2026-09-20

## Changed paths and behavior

New production contract in `engine_core`, namespace
`engine::assets::phlosion`:

- `src/engine/assets/phlosion/PhlosionMeshAsset.h`: `MeshVertex`,
  `MeshBounds`, `MeshSubmesh`, `MeshAsset`, `MeshAssetLimits`, the static v1
  constants and `validateMeshAsset` / `encodeMeshAsset` / `decodeMeshAsset` /
  `loadMeshAsset`. Plain CPU/std types only; the header includes `IAssetStore`
  and nothing from the renderer, platform or editor layers.
- `src/engine/assets/phlosion/PhlosionMeshAsset.cpp`: authored and decoded
  validation, deterministic manifest/`PHMS` encoding through the existing PHRC
  container, strict decode and a synchronous store convenience.

Behavior, matching the PLAN point by point:

- `MeshVertex` is position[3], normal[3], tangent[4], texcoord[2], color[4]
  with the approved defaults (+Z normal, +X tangent with `w = +1`, white), and
  no joint/weight fields. `MeshSubmesh` is `firstIndex`/`indexCount`/
  `materialSlot`; material slots are unique nonempty labels of at most 1024
  UTF-8 bytes, not asset IDs.
- One shared `validateMeshData` path checks authored and decoded data: bounded
  mesh id, at least three vertices and one triangle, index count divisible by
  three, in-range indices, finite attributes, color channels in `[0, 1]`,
  tangent `w` exactly `-1`/`+1`, nonzero normal and tangent `xyz` (unit length
  and orthogonality are deliberately not required), exact bounds, nonempty
  unique slot labels, and an ordered submesh partition of the whole index list
  with no empty, unaligned, out-of-range, overlapping or uncovered range.
  Nothing is normalized, clamped, flipped, repaired or dropped.
- `encode` validates first, writes a `PHMS` document with explicit little-endian
  `float32` component bits and `uint32` indices (never raw struct bytes or
  padding), checks the manifest and final encoded size against
  `maxEncodedBytes`, and publishes only on success.
- `decode` rejects an oversized input, PHRC integrity failures, wrong
  magic/version/flags/dependencies, a chunk set other than exactly
  `VTX0`/`IDX0` with flags 0 and 16-byte alignment, unknown manifest fields
  (which is how skinning/LOD/compression declarations are refused), missing or
  wrongly typed fields, fractional/negative/overflowing integers, manifest and
  chunk-count disagreement, payload truncation or trailing payload bytes, and
  every semantic rule above. Output and diagnostics follow the same
  success/failure contract as `encode`.
- `loadMeshAsset` rejects an empty path, reads synchronously through the
  supplied `IAssetStore` and decodes; it is not a second async scheduler.
- `MeshAssetLimits` defaults are 64 MiB encoded, 1,048,576 vertices,
  3,145,728 indices, 65,536 submeshes and 4,096 slots, enforced with checked
  arithmetic before allocation or retention.

Test, registration and documentation:

- `tests/TestPhlosionMeshAsset.cpp`: six case groups behind
  `test_phlosion_mesh_asset_contract` (deterministic encode and round trip, the
  golden little-endian payload, authored validation, limits, provider loading,
  malformed/malicious documents).
- `tests/TestMain.cpp`: registers `phlosion_mesh_asset_contract`.
- `CMakeLists.txt`: adds the source to `engine_core` and the test to
  `PhlosionEngineTests`.
- `docs/MESH_ASSETS.md`: the CPU contract, validation rules, the exact manifest
  schema and chunk layout, limits, first-consumer notes, verification and
  deferred work.
- `docs/PUBLIC_ASSET_FIXTURE.md`: schema version 2, typed mesh dependency edges,
  the updated validation list, the `FixtureData` role and the M1-E evidence
  entry.
- `examples/public_asset_fixture/README.md`: names the typed `PHMS` resources.

Fixture migration (`examples/public_asset_fixture/`):

- `FixtureData.h/.cpp` no longer contain `PGEO`, `FixtureVertex`,
  `encodeGeometry`, `decodeGeometry` or a geometry file record. Each object now
  owns a `engine::assets::phlosion::MeshAsset`, a stable logical mesh id
  (`fixture/mesh/<role>`) and a versioned registry id
  (`fixture/mesh/<role>-a`/`-b`), and each mesh is a real `PHMS` document built
  by the production encoder.
- The scene archive keeps one texture file per material binding plus the
  versioned scene JSON (schema version 2), and declares one required dependency
  edge per mesh with the exact expected PHRC content hash (plus the existing
  `fixture/leaf/core` leaf). The fixture resolver maps the mesh registry ids to
  `AssetKind::Phrc` and unchanged virtual paths.
- `validateFixtureSnapshot` now proves the reference/role identity, requires the
  mesh in `snapshot.graph.assets`, requires `AssetKind::Phrc` and the expected
  virtual path, compares the declared hash with the loaded document hash,
  decodes the mesh with the production decoder, checks the declared mesh id and
  structural counts, requires one material binding per declared slot, and only
  then gates on the remaining texture/camera/motion/identity rules. The
  document-level `content_hash` is recomputed over mesh document bytes plus
  texture bytes in object order. Nothing activates on failure, and no provider
  read or fallback happens after activation.
- `FixturePresentation.cpp` consumes submesh ranges and slot labels: draws are
  grouped opaque-then-blended as before, each submesh resolves its slot label to
  a binding for texture/material parameters, the index range is drawn from the
  typed mesh, and the per-object `geometryCacheKey` keeps the accepted
  identity-plus-content-hash rule (a multi-submesh mesh would add a stable
  per-submesh suffix). Role rectangles are computed from the same mesh vertices.
- `FixtureSession.cpp` adds the mesh documents to the fixture store beside the
  archives. Texture files, material parameters and motion metadata stay in the
  fixture, as the PLAN allows.
- `tools/public_asset_fixture/FixtureGuardMain.cpp` only updated the synthetic
  handshake's `archive_file_count` (13 to 7) to match the real archive; no guard
  threshold, expected-content check or capture control changed.

Not touched: existing PHRC/`.phlo`/`.phscene` semantics, `Model`/glTF/model
cache code, renderers, shaders, editor host APIs, other repositories, and the
root-owned `PLAN.md`, `CHECKPOINT.md`, roadmap, documentation index,
`VERIFICATION.md` and asset-architecture status. No commit, stage, push, deploy
or production migration was performed.

## Verification

Configuration in use: `build-presentation` (Visual Studio 18 2026, Debug,
`PHLOSION_BUILD_EDITOR=ON`, `PHLOSION_BUILD_TESTS=ON`,
`PHLOSION_BUILD_PUBLIC_ASSET_FIXTURE=ON`, `PHLOSION_ENABLE_WARNINGS=ON`,
`PHLOSION_WARNINGS_AS_ERRORS=ON`), read from
`build-presentation/CMakeCache.txt`. CMake reconfigured automatically because
`CMakeLists.txt` changed.

| Command | Exit | Salient result | Log |
| --- | --- | --- | --- |
| `cmake --build build-presentation --config Debug --parallel 4` | 0 | Whole tree, including `PhlosionEditor`, the plugin probe, the runtime, the guard and `PhlosionEngineTests`, rebuilt warning-clean with warnings as errors | `artifacts/m1-e/build-full-01.log` |
| `cmake --build build-presentation --config Debug --parallel 4` (repeat) | 0 | No-op rebuild; the capture binaries are the ones under test | `artifacts/m1-e/build-final.log` |
| `ctest --test-dir build-presentation -C Debug --output-on-failure` | 0 | 6 of 6 CTest entries passed, including the 18-contract `PhlosionEngineContracts` and `PhlosionPublicAssetFixtureHarness` | `artifacts/m1-e/ctest-01.log`, `artifacts/m1-e/ctest-final.log` |
| `build-presentation/Debug/PhlosionEngineTests.exe` | 0 | 18 `[PASS]` contracts, 0 `[FAIL]` | `artifacts/m1-e/contracts-final.log` |
| Child `add_subdirectory` configure + `mesh_link_proof` build | 0 | `Phlosion::Core` alone provides the mesh contract; no renderer, platform or editor dependency and no graphics context | `artifacts/m1-e/link-proof-configure.log`, `artifacts/m1-e/link-proof-build.log` |
| `artifacts/m1-e/link-proof/build/Debug/mesh_link_proof.exe` | 0 | `[PASS] mesh link proof: bytes=844 vertices=3` | `artifacts/m1-e/link-proof-run.log` |
| `.\tools\public_asset_fixture\run_capture_matrix.ps1 -ArtifactsRoot artifacts/m1-e -RunId m1e-run01` | 0 | 24 of 24 native captures; `failure_count = 0` | `artifacts/m1-e/m1e-run01/guard.log`, `artifacts/m1-e/m1e-run01/{manifest,matrix}.json` |
| `PublicAssetFixtureGuard compare` on all 24 retained captures against `artifacts/m1-d/reorg-run02` | 0 | maximum channel difference 0 on every host/API/case | `artifacts/m1-e/m1e-vs-m1d/` |
| `git diff --check` | 0 | No whitespace or conflict errors (informational LF/CRLF notices only) | - |

Capture provenance (`artifacts/m1-e/m1e-run01/manifest.json`): source revision
`68aed76373c169494d9238d267cd0e50cd885ff2` with the M1-E working tree recorded
as dirty, VS2026 Debug, runtime capture frame 6, editor capture frame 8 with 14
editor frames, PowerShell 5.1.19041.7725, unchanged thresholds
(`mae = 0.01`, `rmse = 0.08`, `changed_ratio = 0.05`, channel tolerance 8), and
SHA-256 plus byte count for the runtime, editor, plugin and guard binaries. All
24 entries exited 0 with the requested native API, requested capture frame, a
`[ParityContract][<Backend>] PASS` line and no fallback text; the guard re-read
the raw logs and required exactly the unique
`{runtime, editor} x {opengl, vulkan, d3d12} x four cases` set.

Measured results in `artifacts/m1-e/m1e-run01/matrix.json`:

- worst intra-host cross-API normalized MAE 0.0000052 and worst RMSE 0.00024
  (M1-D recorded 0.0000052 / 0.00024 under the same limits);
- `a30_vs_failed_a30` pixel-identical on every host and API;
- `a0_vs_a30` changed 1.25% (runtime) and 1.40% (editor);
- `a30_vs_b30` changed 30.9% (runtime) and 34.6% (editor).

Appearance preservation: comparing each M1-E capture with the accepted M1-D
image for the same host, API and case gave `max_channel_diff = 0`,
`normalized_mae = 0`, `normalized_rmse = 0` and `changed_pixel_ratio = 0` for
all 24 pairs, i.e. the typed mesh migration is byte-identical in presentation.

The binary provenance in `manifest.json` still matches the on-disk runtime,
editor, plugin and guard hashes after the final no-op rebuild, so the GPU
evidence applies to the reviewed tree; only documentation changed afterwards.

## Astra review corrections (cycle 1)

Astra reviewed the actual diff, tests and evidence and accepted the architecture
in principle, then requested four concrete corrections. All four are fixed in
this cycle; the initial evidence above is preserved unchanged.

1. **Strict UTF-8 for mesh names.** `PhlosionMeshAsset.cpp` now validates the
   `meshId` and every material slot label as strict UTF-8 (no truncation, lone or
   stray continuation bytes, overlong encodings, surrogates or codepoints above
   U+10FFFF) and returns `false` with a diagnostic instead of letting
   `manifest.dump()` throw `nlohmann::json::type_error` outside the documented
   contract. Bytes are never replaced: `validateMeshAsset` fails, `encodeMeshAsset`
   leaves its output unchanged, and a document whose manifest carries invalid
   UTF-8 already failed through the caught parse error. New tests cover malformed
   mesh ids (lone `0xFF`, lone continuation, truncated, overlong, surrogate,
   above-range, five-byte), a malformed slot label, a 1024-byte multibyte id that
   is accepted, a 1025-byte one that is rejected, a valid multibyte round trip
   that stays byte-identical, and an invalid-UTF-8 manifest that fails cleanly.
2. **Fixture consumer hashing and binding cardinality.** The mesh hash in
   `validateFixtureSnapshot` moved out of the binding loop, so each object hashes
   its mesh document exactly once and then one texture per binding, matching
   `contentHashOfObjects` and the tamper helper. The consumer now requires each
   declared mesh slot to be bound exactly once and rejects duplicates (previously
   only membership in both directions was checked, so duplicates could pass with
   a repaired hash). A new consumer regression drives a valid two-submesh,
   two-slot `PHMS` candidate through validation and activation, asserts the
   consumer resolves each submesh to its own slot binding, and proves that
   duplicated and missing bindings are rejected while active A is preserved.
3. **Tangent forwarding.** `toWorldVertex` became the public fixture helper
   `fixtureWorldVertex` and copies `tangent[0..3]` into
   `WorldMeshVertex::tx/ty/tz/tw`; `WorldMeshVertex` already carries those
   attributes and the backend vertex layouts already bind them, so no renderer or
   shader API changed. A narrow CPU assertion checks a nondefault vertex
   (`tangent = {0.5, -0.25, 0.125, -1}`) through that adapter and additionally
   checks that the fixture's own presented vertices carry their authored tangent.
4. **Literal golden payload.** The mesh contract's golden check no longer
   re-implements the encoder's loops: `kGoldenVertex0` is the full first 64-byte
   vertex of `makeGoldenMesh` as literal little-endian bytes and
   `kGoldenIndices` is the complete 12-byte index payload, with the remaining
   components still covered by a real decode round trip.

The review also asked for a wrap-safety pass on submesh range arithmetic. Both
the engine partition check and the fixture draw-range check now use subtraction
form (`count > size - offset`) rather than addition that could wrap. Supported
hosts remain the documented 64-bit Windows/MSVC build; the change is defensive
and does not expand platform scope. The encoded-byte limit remains documented as
an input/logical-count budget, not a peak-memory promise.

Re-verification on the corrected tree:

| Command | Exit | Salient result | Log |
| --- | --- | --- | --- |
| `cmake --build build-presentation --config Debug --parallel 4` | 0 | Whole tree rebuilt warning-clean with warnings as errors | `artifacts/m1-e/build-full-02.log` |
| `ctest --test-dir build-presentation -C Debug --output-on-failure` | 0 | 6 of 6 CTest entries passed | `artifacts/m1-e/ctest-02.log` |
| `PhlosionEngineTests.exe` | 0 | All contracts pass, including the extended mesh contract and the two-slot fixture consumer regression | `artifacts/m1-e/contracts-04.log` |
| core-only `mesh_link_proof` rebuild and run | 0 | `[PASS] mesh link proof: bytes=844 vertices=3` | `artifacts/m1-e/link-proof-{build,run}-02.log` |
| `.\tools\public_asset_fixture\run_capture_matrix.ps1 -ArtifactsRoot artifacts/m1-e -RunId m1e-run02` | 0 | 24 of 24 captures, `failure_count = 0`, unchanged thresholds | `artifacts/m1-e/m1e-run02/guard.log`, `artifacts/m1-e/m1e-run02/` |
| guard `compare` of `m1e-run02` against `m1e-run01` and against `m1-d/reorg-run02` | 0 | `max_channel_diff = 0` on all 24 pairs in both comparisons | `artifacts/m1-e/m1e-run02-vs-run01/`, `artifacts/m1-e/m1e-run02-vs-m1d/` |
| `cmake --build build-presentation --config Debug --parallel 4` + CTest after the mutation checks below | 0 | Clean rebuild restored from the mutated state; 6 of 6 entries passed | `artifacts/m1-e/build-full-03.log`, `artifacts/m1-e/ctest-03.log` |
| `.\tools\public_asset_fixture\run_capture_matrix.ps1 -ArtifactsRoot artifacts/m1-e -RunId m1e-run03` (final) | 0 | 24 of 24 captures, `failure_count = 0`, unchanged thresholds, fresh binary hashes matching the manifest | `artifacts/m1-e/m1e-run03/guard.log`, `artifacts/m1-e/m1e-run03/` |
| guard `compare` of `m1e-run03` against `m1-d/reorg-run02` and against `m1e-run02` | 0 | `max_channel_diff = 0` on all 24 pairs in both comparisons | `artifacts/m1-e/m1e-run03-vs-m1d/`, `artifacts/m1-e/m1e-run03-vs-run02/` |
| `git diff --check` on the corrected tree | 0 | No whitespace or conflict errors | - |

`m1e-run02` and `m1e-run03` reproduce the run-01 results exactly (worst
cross-API MAE 0.0000052, worst RMSE 0.00024, A30 vs failed-A30 pixel-identical,
A0 vs A30 1.25-1.40%, A30 vs B30 30.9-34.6%). `m1e-run03` is the final capture
set: it was taken after the mutation checks below were reverted and the tree
rebuilt, so the SHA-256 values recorded in its `manifest.json` match the on-disk
runtime, editor, plugin and guard binaries. Forwarding the tangent changes the
data reaching the GPU but not any pixel, because the fixture materials do not
consume tangents; the honest statement is that the attribute now arrives and the
images are unchanged, not that the renderer output was unaffected by
construction.

Test-sensitivity checks (mutations were performed, observed to fail, and
reverted; the final tree contains no mutation):

| Mutation | Observed | Log |
| --- | --- | --- |
| Binding-cardinality rejection disabled (`boundCount > 1u` short-circuited) | `[FAIL] public_asset_fixture_rendering_contract: tampered B passed fixture validation` | `artifacts/m1-e/mutation-duplicate-binding.log` |
| Tangent components no longer copied in `fixtureWorldVertex` | `[FAIL] public_asset_fixture_rendering_contract: the fixture world-vertex adapter dropped a static attribute` | `artifacts/m1-e/mutation-tangent-adapter.log` |

## Issues found and fixed during the loop

1. `validateMeshAsset` legitimately does not enforce `maxEncodedBytes` (there is
   no encoded document to measure), so the first limits case wrongly expected
   authored validation to reject it; the case now asserts that `encode` fails
   and leaves its output unchanged. Log: `artifacts/m1-e/ctest-01.log`.
2. The manifest narrowing diagnostic named only the submesh record; it now names
   the exact `first_index`/`index_count`/`material_slot` field so the tamper
   tests and callers see which value overflowed.
3. Two mesh tamper cases initially targeted the wrong vertex component offsets
   (tangent `x` instead of `w`) and therefore did not change any validated rule;
   the offsets are corrected and the cases now fail for the intended reason.
4. The fixture's duplicate id/role checks ran after the new mesh-reference check,
   which masked the duplicate-role case; they moved beside the identity checks so
   the existing coverage still fires.
5. The first tamper-case patch left the old non-finite-vertex block behind and
   broke the translation unit; the block was removed.

## Risks and limits

- Static v1 is one geometry level with no skinning, LODs, compression or
  material payloads, and material slots are labels: a consumer must supply
  bindings. The fixture proves one slot per mesh; a multi-submesh, multi-slot
  asset is covered by the CPU contract, not by a rendered case.
- `MeshAssetLimits` bounds input bytes and decoded counts, not total peak
  memory: PHRC/manifest copies, caller buffers and vector capacity stay outside
  it.
- The fixture still embeds textures as `PTEX` records and keeps material
  parameters and motion in the scene document; only geometry became a typed
  engine resource.
- GPU evidence is one Windows/NVIDIA GTX 1070 host in VS2026 Debug. It does not
  qualify other GPUs, drivers, release configurations, skeletal animation,
  production material pipelines or performance budgets.
- `docs/VERIFICATION.md` (contract count, dated record) and the documentation
  index are root-owned and were not updated; the phase report and
  `docs/MESH_ASSETS.md` carry the M1-E evidence until Astra integrates it.

## Decisions for Astra

- `tools/public_asset_fixture/run_capture_matrix.ps1` still defaults
  `-ArtifactsRoot` to the historical `artifacts/m1-d`; M1-E passes the root
  explicitly. Changing that default is a runner behaviour change, so it was left
  to Astra rather than folded into this pass.
- The fixture scene schema moved to version 2 and the mesh registry ids are
  `fixture/mesh/<role>-<content>` with logical ids `fixture/mesh/<role>`; both
  are fixture-local conventions, not engine API.
- No architectural blocker was hit. The PLAN's contract was implementable as
  written; nothing needed reinterpretation or scope expansion.

## Next checkpoint

Astra reviews the patch and the evidence under `artifacts/m1-e/` (build, CTest,
link proof, matrix, cross-run comparison), then integrates documentation and the
`VERIFICATION.md`/roadmap entries. Deferred M1 work remains skins/skeletons/
animation, LODs and compression, material payloads and slot bindings, a cooker
and importer path, production `Model`/`.pacmdl` migration, `.phv` packaging and
renderer-side adoption.
