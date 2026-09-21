# M1-K: reusable glTF scene path and integrated character scene

Date: 2026-09-20. Astra owns design/review; one astra_flash_builder implements.
Baseline: main cc600e1f9722b7da7c198102a26763a469e8ee4c. Existing uncommitted
roadmap and M1-J checkpoint edits belong to root and must be preserved.

## Objective and boundary

Deliver a reusable CPU-owned scene asset and world-renderer adapter, demonstrated
by one public animated character in a small environment. Geometry, hierarchy,
skin, animation and five-map materials must come from a real authored glTF file,
not parallel hand-built runtime arrays. Exercise replacement and unload in both
hosts. This implements part of the engine-facing M1 exit checklist; it does not
finish private roster qualification, asynchronous archive integration or GPU
residency management.

Model construction uploads OpenGL resources. Use the existing CPU loader,
buildSceneData, ModelPoseSampler, texture decoders and shared indexed world
renderer without constructing Model. Generic CPU import, pose/palette assembly
and draw binding belong in src/engine/render/gltf; sample content/camera/hosts
belong in examples; verification belongs in tools/tests. Project import/cook
policy stays consumer-owned. No new cooked format, parser, animation evaluator,
renderer abstraction, dependency or production game migration.

## Public contracts

Add GltfSceneAsset.h/.cpp and GltfSceneRenderer.h/.cpp (split implementation
helpers locally if needed) in engine::render::gltf. Exact naming may follow
neighbors, but preserve these semantics:

- loadGltfSceneAsset(path, shared_ptr<const GltfSceneAsset>& out, error, options)
  is CPU-only and synchronous. Build/validate privately; success replaces out,
  failure leaves it unchanged and provides a diagnostic. No live fastgltf views
  or file dependencies remain in the published asset. It owns decoded geometry,
  indices, node/skin/clip arrays, materials and pixels, with source identity and
  honest decoded-payload byte statistics. Existing ModelAnimationTypes and
  WorldMeshVertex are reused; no duplicate skeletal representation.
- sampleGltfScenePose(asset, time, clip, outPose, error) reuses samplePose and
  forms each skinned mesh-node palette as inverse(meshGlobal) * jointGlobal *
  inverseBind. Static mesh nodes retain their node transform. A pose records its
  asset identity so mixing generations is rejected. Failed sampling preserves
  the caller's previous pose. Bind pose is explicit (clip -1).
- drawGltfSceneAsset(renderer, asset, pose, drawOptions, stats, error) submits
  owned bind vertices and palette pointers through existing cached textured world
  drawing. Options provide instance transform, camera/view-projection and surface
  dimensions. Material bindings/factors are assembled generically, not only in
  the sample. Report draw/triangle/palette counts. Borrowed data is valid for the
  call; the caller owns asset/pose until submission returns. Owner-thread use,
  no concurrent mutation. Do not retain a renderer pointer in the CPU asset.
- Cache identities distinguish actual geometry/material/pixel content and asset
  generations, including a successful reload of the same source path. A failed
  replacement keeps the previous shared asset and draw identity intact. Retained
  snapshots remain readable after replacement/unload. No GPU eviction guarantee:
  the current renderer caches outlive these CPU snapshots.

Supported initial subset: indexed or generated-index TRIANGLES, one default
scene (first scene if default absent), multiple mesh nodes/primitives/materials,
POSITION/NORMAL/TANGENT/TEXCOORD_0/COLOR_0, JOINTS_0/WEIGHTS_0, and the existing
LINEAR/STEP/CUBICSPLINE T/R/S sampler. Default missing normal/tangent generation
may reuse existing helpers. Missing UV is accepted only when no authored map
needs it. Four weights per vertex, at most 64 joints per skin, finite values,
valid indices, acyclic single-parent hierarchy, valid inverse binds and counts.
Validate before unsafe accessor iteration and publication; reject unsupported
topology, morph targets, extra joint sets, required unsupported extensions and
material features with a useful diagnostic instead of silently approximating.

Material subset is opaque, alpha factor 1, shared UV0, identity per-role texture
transforms and common repeat wrap/default filtering. Support RGB base factors,
emissive factors and the existing normal/MR/AO factors. Reject non-default UVs,
transforms, blend/mask/double-sided and unsupported material extensions for now.
Images use existing PNG/JPEG routes and documented decoder fallback behavior.
Do not present this bounded importer as arbitrary glTF support. Load options
bound nodes, primitives, vertices, indices and retained decoded texture bytes;
checked arithmetic is required. These are output/admission limits, not a hard
peak-memory or untrusted-input sandbox promise; parser/decoder temporary memory
and filesystem reads remain outside the accounting.

## Material decision and backward compatibility

The existing decoder bakes base factors into encoded bytes. Add an explicit
base-color decode policy with the existing legacy policy as the default; preserve
the existing function call shape via a defaulted argument or overload. The new
PreserveSource policy returns source pixels unchanged and white for absent base
maps. Existing callers, Model cache contents and all prior decoder assertions
remain unchanged. Keep the separate base factor in GltfSceneAsset and apply it
once through WorldTextureData.vertexColorMul, which the current shared shaders
multiply with sampled linear base color and vertex color.

For a genuinely absent emissive map, the new scene material binds implicit white
and keeps emissiveFactor separate. A present invalid/corrupt map retains the
existing decoder fallback; do not conflate absent maps with decoding failures.
This closes factor-only emission on the new supported scene path without silently
changing legacy Model behavior. Opaque alpha remains 1 in the bounded subset.

Primary reference: [glTF materials](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials).
Existing shared PBR remains engine policy; do not claim general glTF shading
conformance or change shaders to manufacture a pass. If generic adapter binding
cannot meet these contracts with existing backends, return the precise blocker
and a proposed bounded decision to Astra.

## Integrated sample and native evidence

Add examples/scene_fixture with one small recognizable block character, a weighted
animated limb, and floor/wall/prop geometry, all authored into glTF. Include a
nonidentity mesh transform, at least two material slots, real PNG maps, a
nontrivial base RGB factor and an emissive-factor-only surface. Use an authored
public asset builder or checked-in tiny assets, no downloads/private content or
tests headers. Reuse existing stb writing and safe owned staging conventions.
The sample owns only content, lifecycle/camera and presentation choices; the new
engine adapter owns generic import, palette and draw binding.

Six native cases x three APIs x two hosts = 36 captures:
1. a-bind: initial A bind pose.
2. a-pose: A at a deterministic visibly bent pose (same in-process bind-to-pose).
3. failed-replacement: draw A, fail a malformed replacement, preserve A-pose.
4. replacement-b: draw A, replace the same source path with changed B geometry
   and texture/material content, then draw B-pose through the same host.
5. emission-off: identical A-pose geometry/materials except emission factor zero.
6. unloaded: draw A then release the active CPU scene and draw only background.

Both standalone runtime and a real editor scene plugin use the exact engine
adapter. Match runtime viewport dimensions to the validated editor viewport.
Keep fixed case times and requested capture frames; record actual draw/transition
state. An optional small in-scene albedo inspection surface may use debug view 2
to independently verify linear factor multiplication, always reset debug state.
The main scene must show the integrated lit character and environment clearly.

Expected content must check character/environment presence, weighted deformation
against authored analytic positions (not imported outputs), failed replacement
preservation, changed B geometry/material content, emission-on/off differences
on that surface, and absence of scene content after unload. Base-factor/albedo
expectations derive from authored source bytes and transfer math, not captures.
CPU draw-recording tests independently inspect actual five-map pointers, factors,
node matrices and palettes submitted by the generic adapter. Native lit-region
checks establish a bounded emission effect, not a complete PBR equation oracle.

Reuse existing verification libraries for PNG I/O, image comparison, SHA-256 and
canonical path containment; reuse FixtureProcess.psm1. Keep the new scene guard
and runner focused on this scene, not another wholesale copy of the material
guard. A small shared utility extraction is allowed only if it meaningfully
avoids duplication and preserves existing guard behavior/tests. No weakening.

Require exactly 36 unique cases, all requested APIs/hosts, real editor activation,
matching frames and same-size cross-host/cross-API comparisons under MAE .01,
RMSE .08, changed ratio .05, tolerance 8. Rehash actual final binaries, verify
log/PNG/handshake/editor-metrics consistency; keep run paths component-contained.
Malformed/missing/oversized evidence must fail cleanly. The adversarial harness
must reject missing/wrong case/host/API/evidence, stale binaries and missing or
wrong scene content; restore each mutation and re-pass pristine afterward.

## CPU, build and regression acceptance

Add meaningful tests for real-file import, per-node primitive bindings, material
source preservation and factors, bind and animated palettes, invalid indices /
attribute counts / hierarchy / skin / unsupported subset, bounded arithmetic,
failure-preserved output, retained ownership after source removal/replacement,
and the renderer adapter's actual submissions. Use tests' existing glTF helpers
where useful, not duplicated parser/math logic. The new loader must run without
creating a graphics context. Keep asset parsing/decoding off the per-frame loop.
Sample/test debug-dump flags must be hermetic and temporary files owned/released.

Record wall-clock load/pose timings and accounted CPU payload bytes for these
inputs, with definitions and no target-budget or GPU-residency claim. Keep the
existing archive/session APIs unchanged; asynchronous scene import and GPU cache
release remain explicit follow-up work.

Build all enabled Debug targets in build-presentation with warnings as errors;
run all existing 26 CPU contracts plus new coverage, and full CTest including the
new adversarial harness. Run final 36 scene captures and existing archive24,
animation36 and material18 regressions. Reuse old captures only if every recorded
binary SHA-256/size still matches final files; otherwise regenerate. Inspect
contact sheets and retain raw evidence under ignored artifacts/m1-k.

## Ownership, budget and handoff

Worker owns new engine glTF scene asset/renderer files; the narrow base-decode
policy edit in ModelFastGltfTextures.h/.cpp; new examples/scene_fixture and
tools/scene_fixture, tests/TestGltfSceneAsset*.cpp, tests/TestSceneFixture*.cpp,
tests/scene_fixture; CMake/TestMain registration; docs/GLTF_SCENE_ASSETS.md and
docs/SCENE_FIXTURE.md, necessary bounded accuracy edits to texture/material guides;
this phase REPORT.md. Existing common test/guard helpers may be reused or narrowly
extracted as above. Do not migrate Model, change renderer/shader/ABI/cache/schema,
edit other repositories, fetch assets, add dependencies, or stage/commit/push.

Root owns PLAN/CHECKPOINT, shared README/VERIFICATION/readiness/architecture docs
and the pre-existing M1-J checkpoint edits. You are not alone: preserve others'
changes. No recursive agents or router/config changes. Allow up to 150 minutes
for this coherent implementation/test/native loop; checkpoint on a real contract
or environment blocker. Return ready_for_review with exact scope, commands/exits,
coverage, remaining limits and final evidence. Astra reviews actual code and
artifacts; one consolidated correction is the default. Leave work uncommitted.
