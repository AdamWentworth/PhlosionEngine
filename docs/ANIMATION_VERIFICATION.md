# Animation pose sampling and native skeletal verification

Status: Implemented CPU extraction, CPU contracts, CUBICSPLINE sampling and
public native fixture (M1-G extraction, M1-H cubic fidelity)
Last updated: 2026-09-20

## Goal and boundary

Animation playback and weighted GPU deformation already existed; `Model` simply
owned the CPU pose evaluator inside a constructor that requires an OpenGL
context. This pass exposes that evaluator through a small production CPU
interface, makes `Model` delegate to it, and verifies real weighted skeletal
deformation with a tiny public rig through the standalone runtime host and the
actual editor project plugin on OpenGL, Vulkan and Direct3D 12.

M1-H adds the glTF CUBICSPLINE path on top of that extraction: the importer
keeps the authored tangents instead of discarding them, the shared sampler
evaluates them with componentwise cubic Hermite interpolation, and the model
cache bumped to v10 so tangent-stripped caches re-import. The LINEAR and STEP
sampling policy, `Model`'s public API, defaults, diagnostics and draw behavior
are otherwise unchanged, and no backend, editor-host, shader or material code
was touched. Channel blending, animation events, attachments, morph targets,
importer/cook policy and Pokemon-specific rules remain out of scope.

## Shared CPU pose sampler

[`ModelPoseSampler.h`](../src/engine/render/ModelPoseSampler.h) declares
`engine::render::animation::samplePose` with a borrowed `PoseSource`
(`nodesDefault`, `nodeChildren`, `sceneRoots`, `skins`, `animations`,
`nodeNames`) and writes the sampled local pose plus the composed global
matrices. `trsToMat4` is declared in `ModelPoseSampler.h` and defined in
`ModelPoseSampler.cpp`; `Model::trsToMat4` delegates to that definition so the
extraction has exactly one implementation.

[`ModelAnimation.cpp`](../src/engine/render/ModelAnimation.cpp) keeps its
OpenGL-only responsibilities (skin uniform upload, cached draws, sampled
drawing, bound-shader geometry drawing) and `Model::buildPoseMatrices` is a
thin call into the shared sampler. The extraction itself was proven mechanical
in M1-G, whose committed state is afaeb1e: at that time the normalized
statement streams of `Model::buildPoseMatrices` (baseline 9c0d812) and the
extracted `samplePose` were byte-identical after removing comments, blank
lines, the `source.` alias block and namespace qualification. That script and
its output are retained in `artifacts/m1-g/diff/`; no baseline copy ships in
production or tests. The proof is historical: M1-H deliberately changes
`samplePose` afterwards to add CUBICSPLINE, so it covers the extraction only,
not the current sampler body.

M1-H adds CUBICSPLINE to that shared sampler. `AnimationSampler` keeps its
existing members: `outputs` stores one (x, y, z, w) value per key for LINEAR and
STEP and the raw per-key glTF `[in-tangent, value, out-tangent]` triplets for
CUBICSPLINE, which the type comment records. The importer no longer drops the
tangents, the sampler clamps the wrapped sample time to the channel's own keys
and evaluates componentwise cubic Hermite with tangents scaled by the real
interval in seconds, and rotations normalize the result without a sign flip.
`LINEAR` and `STEP` keep the legacy wrapping, tail and fallback behavior
byte-for-byte.

[`ModelCacheAnimation.h`](../src/engine/render/ModelCacheAnimation.h) /
`ModelCacheAnimation.cpp` is the narrow CPU seam for the sampler part of the
model cache: the packed-header identity constants plus `readAnimationSampler`
and `writeAnimationSampler` over standard streams. `ModelCache` uses those
functions for real sampler I/O and for the identity check before any cached
state is published, so the CPU contract exercises the shipped codec rather than
a copy. A failed read returns false and leaves the caller's sampler untouched.
The cache version moved from 9 to 10 so caches written while tangents were
stripped re-import instead of replaying lossy animation.

## CPU contracts

[`TestModelPoseSampler.cpp`](../tests/TestModelPoseSampler.cpp) drives the real
shared implementation through its public interface with tiny authored data and
hand-computed expectations. It covers empty input, default poses, invalid clip
indices, LINEAR and STEP translation/rotation/scale, normalized quaternion
interpolation, one-key channels, negative and looping time, the exact positive
endpoint policy, channels shorter than the clip duration, hierarchy composition,
scene-root selection, output replacement, horizontal root-motion suppression for
named carriers, skeleton-root carriers and matrix-authored carriers, unaffected
non-carrier translation, LINEAR nonuniform scale interpolation (literal
per-axis midpoint plus a transformed point), segment-scale compensation and
empty-sampler fallbacks. The M1-H additions cover cubic translation, scale and
rotation with literal closed-form expectations, two segments with non-unit
intervals and asymmetric tangents, exact interior keys, clamped channel
endpoints, a LINEAR reference that would differ, quaternion normalization from
scaled values, a sign-sensitive curve that must not flip, and malformed cubic
data (too few keys, wrong output count, non-finite components, non-increasing
times, non-finite key times and tangents, a degenerate quaternion) leaving the
current local component untouched. One contract uses finite FLT_MAX tangents to
prove that clamped endpoints and exact first/interior/last keys return the
authored values while a strictly interpolated sample with a non-finite result
skips the channel.

[`TestGltfSceneImport.cpp`](../tests/TestGltfSceneImport.cpp) adds an authored
cubic glTF fixture (JSON plus explicit little-endian bytes) that is loaded
through `loader::tryLoad` and `buildSceneData` and then sampled: the importer
must keep the raw tangent triplets and the xyzw rotation order, and `samplePose`
must reproduce independently hand-computed Hermite results.

[`TestModelCacheAnimation.cpp`](../tests/TestModelCacheAnimation.cpp) exercises
the production cache codec seam
([`ModelCacheAnimation.h`](../src/engine/render/ModelCacheAnimation.h)) that
`ModelCache` itself uses: raw tangent roundtrip, sampling after the roundtrip,
LINEAR/STEP one-output-per-key layout, a truncated stream that must fail without
publishing a partial sampler, and the packed-header identity check (v10
accepted, v9 and wrong magic rejected).

Engine policies recorded by these contracts (not universal glTF rules):

- A positive exact multiple of a clip duration resolves to just below the end,
  so a LINEAR channel returns its last key instead of wrapping to the first.
  Negative whole durations land on the first key, and negative fractional times
  wrap forward.
- On the legacy LINEAR/STEP path, STEP holds the earlier key inside its key
  range and the last key beyond it.
- Horizontal root-motion suppression freezes animated X/Z on the carrier nodes
  (`origin`, `waist`, `hips`, plus each skin's skeleton root) while keeping
  animated Y, every rotation and every scale.
- A legacy LINEAR channel shorter than the clip interpolates from its last
  key back to its first across the remaining tail span.
- A legacy LINEAR/STEP channel whose sampler has no keys contributes zero
  translation, zero scale and identity rotation; channels with unknown sampler
  or node indices are skipped entirely.
- CUBICSPLINE clamps the wrapped sample time into the channel's own key range
  and evaluates the raw per-key [in-tangent, value, out-tangent] triplets with
  componentwise cubic Hermite interpolation, scaling the tangents by the real
  interval in seconds. Rotation normalizes the result without flipping the
  authored quaternion sign. Clamped endpoints and exact keys return the authored
  value directly (rotations are then normalized by the caller), so finite but
  extreme tangents cannot overflow a sample that does not need interpolation;
  only a non-finite interpolated result, or malformed cubic data, skips the
  channel and leaves the destination's current local component untouched.

Absent morph-weight channels remain a documented limitation of the importer.

## Public native fixture

[`examples/animation_fixture`](../examples/animation_fixture) authors a two-joint
hinged strip: a fixed base joint at `(-0.5, 0, 0)`, a moving tip joint at
`(2.5, 0, 0)`, both with nonidentity inverse binds, five weight stations whose
tip weight ramps from 0 to 1, a nonidentity mesh transform and an authored
camera. Both hosts submit the original bind vertices with joint indices and
weights, set `gpuSkinning` on the world draw and pass a caller-owned palette of
`inverse(meshTransform) * jointGlobal * inverseBind` per joint. No vertex is
deformed on the CPU. A green background quad is drawn first so the viewport is
detectable inside editor chrome.

Each host presents the bind pose first and then the requested case pose through
the same geometry cache key and one caller-owned palette allocation. The hosts
only rewrite that buffer's values in place between the two states, so the
submitted pointer never changes and a stale or swapped palette is observable in
both the handshake and the capture. `FixturePaletteStorage` owns that buffer and
`FixturePaletteTransition` records the bind/requested draw addresses and whether
the contents changed; both live in the example, not in the engine.

| Case | Clip | Sample time | Logical frame | Authored tip angle |
| --- | --- | --- | --- | --- |
| `bind` | `linear` | 0.0 s | 0 | 0° |
| `linear-intermediate` | `linear` | 1.0 s | 30 | -35° |
| `linear-near-endpoint` | `linear` | 2.0 s | 60 | -70° |
| `step-state` | `step` | 1.5 s | 90 | -110° |
| `cubic-midpoint` | `cubic` | 1.0 s | 120 | -120° |
| `cubic-early` | `cubic` | 0.5 s | 150 | -76.4264214° |

The case catalog in
[`AnimationFixtureCase.h`](../examples/animation_fixture/AnimationFixtureCase.h)
owns clip, sample time, logical frame, the analytic angle and the angle a LINEAR
interpolation of the same keys would produce. The verification tools derive
expected landmarks from those authored values with their own
linear-blend-skinning math, never from the sampled pose under test.

The `cubic` clip is the authored CUBICSPLINE rotation: keys at 0 s and 2 s from
identity to -120° about +Z with nonzero tangents (3x the chord / interval out of
the first key, -1x into the second). Componentwise Hermite interpolation puts
the curve exactly on the authored -120° key at the midpoint and at
-76.4264214° at a quarter of the interval, while a LINEAR fallback would sit at
-60° and -30°, so both cubic cases are visibly distinct from a tangent-stripped
or linearized sampler and the guard's synthetic defect is detectable.

## Capture matrix and guard

`tools/animation_fixture/run_capture_matrix.ps1` captures six cases x three
native APIs x two hosts (36 images), records the engine revision/baseline, dirty
state, binary hashes, requested capture frames and parsed log provenance, and
then runs `AnimationFixtureGuard matrix --require-complete`. The runner reuses
the shared native process launcher from the public asset fixture. An omitted
`-ExpectedBaseline` records the resolved HEAD; an explicitly supplied revision is
resolved to a commit and must match HEAD.

`AnimationFixtureGuard`:

- loads PNGs, detects the green viewport (identity for standalone captures),
  crops editor chrome away and compares API pairs with the approved limits
  (normalized MAE <= 0.01, RMSE <= 0.08, changed ratio <= 0.05 at channel
  tolerance 8) that cannot be loosened;
- requires exactly the unique `{runtime, editor} x {opengl, vulkan, d3d12} x
  six cases` Cartesian set, a zero exit code, exactly one matching
  `[Screenshot][<Backend>] WROTE` record at the requested frame, a passing
  parity contract for that API and no OpenGL compatibility fallback;
- re-hashes the runtime, editor, plugin and guard binaries and requires the
  recorded revision to equal the recorded source baseline; the `binaries` object
  and each of its four identities are mandatory, so a manifest that omits them
  fails instead of skipping the check;
- requires every entry's requested capture frame to equal the manifest's
  requested capture frame for that host, and the handshake host to match the
  matrix tuple;
- validates each handshake against the case catalog (clip, sample time, logical
  frame, clip index, analytic angle, explicit requested-backend identity) and
  against independently authored math: the 2x4x4 palette must equal
  `inverse(meshTransform) * jointGlobal * inverseBind`, the mesh transform must
  match, every submitted vertex position (including Z), joint index and weight
  must match the authored rig, and every submitted numeric field must be finite;
- requires the recorded bind-draw, requested-draw and storage palette addresses
  to be the same allocation, requires the requested palette to differ from the
  recorded bind palette on posed cases and to stay identical on the bind case,
  and requires the host's `palette_contents_changed` claim to agree;
- checks expected image content per case: green background coverage, rig
  coverage band, anchored-base landmark, moved-tip landmark, blended-middle
  landmark, red base and yellow tip colour dominance inside the rig, and
  background at the positions a rigid base or a single-joint (rigid middle)
  mapping would occupy;
- checks authored case differences (bind vs each posed case, intermediate vs
  near-endpoint, near-endpoint vs step, the cubic cases against bind and against
  each other) and writes `matrix.json` plus a contact sheet.

Measured image values (`measured_content`) are reported separately from the
authored landmark distances (`expected_geometry`), so a published number never
mixes an expected separation with a pixel measurement.

The guard's `synth` command builds deterministic captures for the hermetic
tests. It models disabled skinning, a stale bind palette, a palette built in
mesh space, a rigid tip mapping, a blank frame and a cubic clip evaluated as a
LINEAR curve between its keys (`linearized-cubic`); the CPU contract requires
the validator to reject every distinguishable defect, and the PowerShell harness
drives the same paths through the real CLI. The cubic defect uses the catalog's
authored LINEAR fallback angle, so it is rejected on `cubic-midpoint` and
`cubic-early` and is deliberately indistinguishable (and accepted) on the
LINEAR and STEP cases. With skinning disabled the renderer
still applies the nonidentity mesh transform to the raw bind vertices, so that
defect is rejected even on the `bind` case; at zero degrees a stale bind palette
and the rigid tip transform are the identity and are indistinguishable there.
The harness also rejects a missing or partial `binaries` object, an entry frame
that disagrees with the manifest capture frame, a handshake host or clip index
mismatch, a missing requested backend, a submitted vertex Z mismatch, a swapped
palette allocation and a requested palette that stayed at bind values.

[`TestAnimationFixture.cpp`](../tests/TestAnimationFixture.cpp) covers the rig
structure, palette space (`mesh * palette * bindVertex` equals the analytic
blend for every authored vertex), non-rigid blend separation, handshake
validation and tamper rejection, guard verdicts per defect, image primitives and
published SHA-256 vectors. The contracts need no graphics device.

## Evidence record

On 2026-09-20, the existing `build-presentation` Debug tree (VS2026,
editor/tests enabled, engine warnings as errors) rebuilt warning-clean and CTest
passed 7 of 7 entries, including the 24 C++ contracts and both fixture
harnesses. The native matrix (`artifacts/m1-h/m1h-run02`, captured after the
review correction that returns authored values for clamped endpoints and exact
keys) captured all 36 animation cases with `failure_count = 0`; the worst
cross-API pair measured normalized MAE 0.0000074 and RMSE 0.00039 under the
unchanged approved limits, and the authored case differences measured 4.8-9.7%
changed pixels with the two cubic cases distinct from bind (7.4-9.6%) and from
each other (5.0-5.6%). The original four cases re-captured on this tree are
pixel-identical to the retained M1-G captures (24 of 24 comparisons), the whole
36-image matrix is pixel-identical to the pre-correction M1-H run (36 of 36),
and every comparison reports worst channel difference 0.

The existing public archive fixture was re-run as regression coverage
(`artifacts/m1-h/archive-regression/m1h-archive-final`): all 24 cases passed with
its historical numbers (editor A0 vs A30 1.40%, editor A30 vs B30 34.55%,
runtime 1.25%/30.90%, failed replacement pixel-identical). That fixture renders
rigid archive content and never samples a pose, so it evidences only that the
shared renderer paths keep their appearance; sampler behavior rests on the CPU
contracts, the authored palette expectations and the animation matrix. Astra
recaptured this archive matrix after an independent check found all four older
recorded binary hashes differed from the final build. Unlike the animation
guard, the archive guard validates digest format and binary size without
recomputing the digest; its earlier PASS was insufficient to reuse that evidence.
All four hashes in each final manifest were independently checked against the
actual binaries (`artifacts/m1-h/final-provenance.json`).

The M1-H extraction of the cache sampler codec keeps the packed layout
unchanged: `ModelCacheAnimation` is the only writer/reader of sampler bytes and
the model cache version moved from 9 to 10 so tangent-stripped caches re-import.

Raw logs, handshakes, metrics, manifests, matrices, contact sheets and the
retained comparison summaries are under ignored `artifacts/m1-h/`; the M1-G
extraction differential proof and child-project evidence remain under
`artifacts/m1-g/`.

## Evidence limits

These public cases qualify the shared sampler, the caller-owned palette space
and the world-mesh-textured skinning path in both hosts on one Windows/NVIDIA
GTX 1070 Debug workstation. They do not qualify a private character, production
materials, morph targets, instanced or indirect skinned paths that were not
exercised, other GPUs/drivers, or performance budgets. The fixture's cubic
coverage is a single-segment, two-key Z-rotation curve captured natively on all
three APIs; multi-segment cubic translation, two-key cubic scale, the
extreme-tangent exact-key behavior and every malformed-data rule are CPU-tested
only (sampler, importer and cache-codec contracts), with no native capture.

The legacy OpenGL-only `Model::drawAnimated` path is not exercised by this
fixture. Its draw code is unchanged and it now samples through the shared
corrected evaluator, so its playback of CUBICSPLINE clips changes with the rest
of the engine by design; the native legacy draw path itself remains unqualified.
The matrix establishes three-API-consistent presentation of a two-joint rig, not
general skeletal-animation coverage. Synthetic captures and CLI-level mutations
are hermetic checks of the guard; only the 36 native images exercise the real
backends.

## Related documentation

- [Verification](VERIFICATION.md) for CI scope and dated evidence.
- [glTF character import verification](GLTF_IMPORT_VERIFICATION.md) for the
  importer contracts this pass builds on.
- [Public asset fixture](PUBLIC_ASSET_FIXTURE.md) for the archive-driven fixture
  and the capture-provenance conventions reused here.
