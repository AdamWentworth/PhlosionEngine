# Animation pose sampling and native skeletal verification

Status: Implemented CPU extraction, CPU contracts and public native fixture (M1-G)
Last updated: 2026-09-20

## Goal and boundary

Animation playback and weighted GPU deformation already existed; `Model` simply
owned the CPU pose evaluator inside a constructor that requires an OpenGL
context. This pass exposes that evaluator through a small production CPU
interface, makes `Model` delegate to it, and verifies real weighted skeletal
deformation with a tiny public rig through the standalone runtime host and the
actual editor project plugin on OpenGL, Vulkan and Direct3D 12.

Nothing about animation semantics changed. The sampling policy, `Model`'s public
API, defaults, diagnostics and draw behavior are unchanged, and no backend,
editor-host, shader, cache-schema or material code was touched. Cubic
interpolation, channel blending, animation events, attachments, morph targets,
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
drawing, bound-shader geometry drawing) and `Model::buildPoseMatrices` is now a
thin call into the shared sampler. The extraction is proven mechanical: the
normalized statement streams of the committed baseline
`Model::buildPoseMatrices` (9c0d812) and the extracted `samplePose` are
byte-identical after removing comments, blank lines, the `source.` alias block
and namespace qualification. That script and its output are retained in
`artifacts/m1-g/diff/`; no baseline copy ships in production or tests.

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
empty-sampler fallbacks.

Engine policies recorded by these contracts (not universal glTF rules):

- A positive exact multiple of a clip duration resolves to just below the end,
  so a LINEAR channel returns its last key instead of wrapping to the first.
  Negative whole durations land on the first key, and negative fractional times
  wrap forward.
- STEP holds the earlier key inside its key range and the last key beyond it.
- Horizontal root-motion suppression freezes animated X/Z on the carrier nodes
  (`origin`, `waist`, `hips`, plus each skin's skeleton root) while keeping
  animated Y, every rotation and every scale.
- A channel shorter than the clip interpolates from its last key back to its
  first across the remaining tail span.
- A channel whose sampler has no keys contributes zero translation, zero scale
  and identity rotation; channels with unknown sampler or node indices are
  skipped entirely.

CUBICSPLINE tangent loss and absent morph-weight channels remain documented
limitations of the importer, not newly accepted behavior.

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

The case catalog in
[`AnimationFixtureCase.h`](../examples/animation_fixture/AnimationFixtureCase.h)
owns clip, sample time, logical frame and the analytic angle. The verification
tools derive expected landmarks from those authored values with their own
linear-blend-skinning math, never from the sampled pose under test.

## Capture matrix and guard

`tools/animation_fixture/run_capture_matrix.ps1` captures four cases x three
native APIs x two hosts (24 images), records the engine revision/baseline, dirty
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
  four cases` Cartesian set, a zero exit code, exactly one matching
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
  near-endpoint and near-endpoint vs step) and writes `matrix.json` plus a
  contact sheet.

Measured image values (`measured_content`) are reported separately from the
authored landmark distances (`expected_geometry`), so a published number never
mixes an expected separation with a pixel measurement.

The guard's `synth` command builds deterministic captures for the hermetic
tests. It models disabled skinning, a stale bind palette, a palette built in
mesh space, a rigid tip mapping and a blank frame; the CPU contract requires the
validator to reject every distinguishable defect, and the PowerShell harness
drives the same paths through the real CLI. With skinning disabled the renderer
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
passed 7 of 7 entries, including the 23 C++ contracts and both fixture
harnesses. The final native matrix (`artifacts/m1-g/m1g-run04`) captured all 24
animation cases with
`failure_count = 0`; the worst cross-API pair measured normalized MAE
0.0000038 and RMSE 0.00029 under the unchanged approved limits, and the authored
case differences measured 4.77-9.73% changed pixels. Re-capturing the same tree
after the shared-palette fix reproduced the previous 24 captures
pixel-identically (worst channel difference 0), and the handshakes record one
palette address for the bind and requested draws with changed contents on the
posed cases.

The existing public archive fixture was re-run as regression coverage
(`artifacts/m1-g/archive-regression/m1g-archive-02`): all 24 cases passed with
its historical numbers (A0 vs A30 1.40%, A30 vs B30 34.55%, failed replacement
pixel-identical), and its runtime/editor/plugin/guard binaries were unchanged by
this pass. That fixture renders rigid archive content and never samples a pose,
so it evidences only that the shared renderer paths keep their appearance;
sampler behavior rests on the CPU contracts, the authored palette expectations
and the animation matrix.

A child-project configure confirmed `PHLOSION_BUILD_ANIMATION_FIXTURE=OFF` by
default with the fixture hosts absent, that the example and verification
libraries still build when engine tests are enabled, and that a consumer linking
only `Phlosion::Engine` reaches the shared sampler without a graphics context.

Raw logs, handshakes, metrics, manifests, matrices, contact sheets, the
extraction differential proof and the child-project evidence are under ignored
`artifacts/m1-g/`.

## Evidence limits

These public cases qualify the shared sampler, the caller-owned palette space
and the world-mesh-textured skinning path in both hosts on one Windows/NVIDIA
GTX 1070 Debug workstation. They do not qualify a private character, production
materials, morph targets, cubic spline tangents, instanced or indirect skinned
paths that were not exercised, other GPUs/drivers, or performance budgets.

The legacy OpenGL-only `Model::drawAnimated` path is not exercised by this
fixture; its behavior is preserved by the shared sampler plus unchanged draw
code, and M1-F's glTF import contracts still cover import structure. The matrix
establishes three-API-consistent presentation of a two-joint rig, not general
skeletal-animation coverage. Synthetic captures and CLI-level mutations are
hermetic checks of the guard; only the 24 native images exercise the real
backends.

## Related documentation

- [Verification](VERIFICATION.md) for CI scope and dated evidence.
- [glTF character import verification](GLTF_IMPORT_VERIFICATION.md) for the
  importer contracts this pass builds on.
- [Public asset fixture](PUBLIC_ASSET_FIXTURE.md) for the archive-driven fixture
  and the capture-provenance conventions reused here.
