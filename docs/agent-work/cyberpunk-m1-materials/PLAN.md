# M1-J: five-map native material verification and empty-image fallbacks

Date: 2026-09-20. Astra plans/reviews; one native astra_flash_builder implements.
Baseline: clean main a037f8833cb3a509b980b825fc5833565eac4784 (M1-I committed).

## Objective and architecture

Connect M1-I's existing five CPU decoders to the existing shared world renderer
in a public runnable material sample, qualify the result on three native APIs
in runtime and editor, and correct one bounded production defect: empty encoded
image data must use the same role-specific fallback as undecodable image data.
This is the next part of M1, not full game/material readiness.

Inspection found WorldTextureData already carries all five image roles, separate
color-space flags, wrap modes and scalar material factors. The existing default
world pipelines already consume them. Use that interface and indexed world-mesh
drawing with an empty material profile; do not add a renderer/material abstraction
or migrate the legacy Model draw path. The sample's glTF-to-WorldTextureData
binding is consumer/example code. Generic fallback correction belongs in src;
runnable hosts/data/presentation belong in examples; guards/runners in tools;
contracts/adversarial checks in tests. Reuse existing host/process/image helpers
where useful without weakening or repurposing the archive/animation fixtures.

Primary reference: [glTF materials](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials).
The renderer's existing shading policies are not a claim of exact glTF PBR
conformance. Report material-mode, normal strength, roughness and debug-view
policies encountered, rather than changing shaders to match a new reference.

## Production contract

Only ModelFastGltfTextures.cpp's empty/unavailable encoded-byte fallback changes:
use its existing role-specific fallback selection, giving opaque white base,
opaque black emissive, flat normal (128,128,255,255), and opaque white MR/AO,
with the existing fallback sampler metadata. Successful decodes, absent/invalid
references, corrupt-image behavior, base-factor byte multiplication and emissive
factor handling stay as they are. No shader, backend, Model/cache or ABI changes.
Legacy Model decodes only base/emissive, whose results do not change, so this
does not require cache invalidation. Verify that assumption against final diff.

Promote the former empty-byte artifact probe to a permanent CPU regression in
TestGltfTextureImport.cpp asserting the corrected desired results for all roles.
Assert role bindings so it cannot accidentally pass via absent references. Keep
the genuine PNG/JPEG, corrupt-payload and retained-output coverage. Demonstrate
the new regression fails under the old empty-byte branch before accepting it.

## Native sample contract

Add examples/material_fixture with shared content/presentation, a standalone
MaterialFixtureRuntime and a real MaterialFixtureEditorPlugin. Use existing
editor plugin ABI and host conventions; no engine/editor host modifications.
Add a top-level-ON, consumed-project-OFF build option as prior fixture options do.
Keep CPU verification available when tests are ON and fixture hosts are OFF.

Load a small authored public glTF through loader::tryLoad and the actual five
decode helpers; retain their owned pixels and use them directly in WorldTextureData.
Use fixed tiny PNG payloads with independent authored pixel expectations; do not
include test headers in the runnable sample. Portable sample input may live under
examples/material_fixture/assets, with no private data or extra dependency.
Use content-dependent role cache keys, keep pointer lifetimes valid across draws,
and pass base/emissive as sRGB and normal/MR/AO as linear. Share one UV set and
identity transforms, with baseColorFactor=1. Factors must reach their intended
renderer fields once. Do not claim arbitrary sampler filtering or per-role UVs.

Three fixed cases, each on both hosts and all three APIs (18 native captures):

1. authored-maps: five distinguishable decoded image inputs, including spatial
   variation and deliberately different MR green/blue channels, non-flat normal,
   varying AO and colored emission. Use nontrivial scalar factors where the
   existing debug/shading path supports meaningful independent assertions.
2. empty-image-bytes: all role slots deliberately select zero-length image data;
   decoded normal/MR/AO must show corrected placeholders. Document that this
   is a bounded malformed-input fallback case, not a valid glTF asset.
3. absent-maps: valid material without maps, neutral reference factors; verify
   consistency with the intended empty-image fallback results. Emission factor
   zero in both fallback cases avoids treating the known factor-only defect as
   a required appearance.

Each capture must show identifiable diagnostic patches for base, normal,
roughness, metallic, AO and emission, using the existing per-draw material debug
views, plus a composed lit material region. Reset transient debug state after
draws. A single layout per case keeps the matrix bounded; view choice must be
recorded. Expected-content guards must independently check role identity,
channel routing, color-space treatment and corrected fallback patches. Use
interior regions with justified tolerances, not edge-sensitive exact pixels.
Lit content must be present and materially different for authored vs neutral
inputs; diagnostic checks alone do not qualify full PBR fidelity.

No production shader/backend correction is approved. If genuine native behavior
contradicts the material contract, return the precise affected path, captures
and proposed bounded fix to Astra rather than masking it or expanding scope.

## Verification and evidence

Build all enabled Debug targets in build-presentation with warnings as errors.
Run all existing 25 CPU contracts plus the new fixture/guard coverage, and full
CTest including a CPU-only adversarial CLI/matrix harness for this fixture.
Reuse FixtureProcess.psm1. Launch hidden/bounded native processes, preserve exit
codes and logs; uniquely own any temporary directories, no broad cleanup.

The capture runner/guard must require the exact unique 18-case Cartesian set,
requested native backend (no fallback), deterministic logical/capture frames,
actual editor plugin activation and shared sample identity. Preserve source
revision/dirty state, image/handshake paths, executable/plugin/guard SHA-256 and
sizes. Actually compare hashes to the final binaries: the older archive guard's
format/size-only hash check is insufficient. Bound paths to the run directory.

Use established maximum parity thresholds (normalized MAE .01, RMSE .08,
changed ratio .05, channel tolerance 8), no relaxed thresholds. Require both
cross-API and runtime/editor content/parity evidence for each case. Guard tests
must reject missing/duplicate cases, wrong backend/host, stale hashes, absent
content, swapped MR channels, missing/neutralized normal/AO/emission maps and
the old black normal/MR/AO fallback. Restore each mutation independently.
No learning golden expectations from the captured outputs.

Run the existing archive (24 captures) and animation (36 captures) native
regressions on final binaries since this phase changes rendered inputs and adds
a fixture. Do not attribute those older fixtures to material-map coverage.
Retain final logs/matrices/contact sheets/provenance in ignored artifacts/m1-j.
Worker visually inspects contact sheets; Astra independently reviews them and
the actual patch. If final binaries change, refresh or explicitly revalidate
provenance; do not reuse a PASS tied to different binaries.

## Ownership and handoff

Worker owns the narrow fallback edit in src/engine/render/gltf/ModelFastGltfTextures.cpp;
tests/TestGltfTextureImport.cpp; new examples/material_fixture, tools/material_fixture,
tests/material_fixture and tests/TestMaterialFixture*.cpp; CMake fixture/test
registration and tests/TestMain.cpp registration; docs/MATERIAL_VERIFICATION.md
and necessary accuracy edits to docs/GLTF_TEXTURE_VERIFICATION.md; this phase's
REPORT.md. Existing process/image utility changes require a concrete reuse need,
backward compatibility and their existing tests. No unrelated refactor.

Root owns PLAN/CHECKPOINT, docs/README.md, docs/VERIFICATION.md, the readiness
roadmap and prior checkpoint commit status. You are not alone: preserve others'
edits. No staging/commits/pushes, recursive agents, other-repo changes, private
assets, dependencies, router settings, renderer changes or weakened guards.

Known base-factor color semantics, missing-emissive factor-only behavior,
production game adapters, legacy Model five-map integration, independent UVs,
compressed formats and private-character qualification remain later material
work. This pass establishes native evidence and closes only the empty-byte gap.

Allow up to 90 minutes for the coherent discovery/build/test/capture loop. Return
ready_for_review with exact commands/exits, changed paths, evidence locations,
policy observations and limits. Checkpoint if blocked by a contract conflict or
environment; otherwise complete verification before reporting. Astra permits
one consolidated review correction by default. Leave M1-J uncommitted.

## Accepted review refinements

The editor capture establishes viewport dimensions; the runtime accepts those
dimensions without any editor-host change. All nine host pairs use the same
whole-image thresholds as the cross-API pairs. Interior pixels must agree within
three byte counts and authored/fallback lit regions must differ independently
of diagnostic tiles. The composed shading equations and GPU scalar application
remain outside this qualification.

One consolidated correction closed ownership, evidence and content-check gaps.
A narrow second correction was justified by root-reproduced false PASS results
for overflowing metadata: hashes are compared exactly and dimensions checked
in 64-bit form before narrowing. The final native run is `m1-j-final3`; its
predecessors remain historical evidence. See [CHECKPOINT.md](CHECKPOINT.md) for
acceptance and final artifact identities.
