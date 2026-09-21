# M1-H: glTF cubic animation fidelity

Date: 2026-09-20. Astra plans/reviews; one astra_flash_builder implements.
Baseline: clean main afaeb1ec470434fe3da10f7c5f3c8238b2fc5068.
The user authorized committing M1-G and proceeding; M1-G is committed.

## Objective and evidence

Close the confirmed CUBICSPLINE gap in the existing importer and shared pose
sampler. ModelFastGltfSceneData currently discards both tangents at every key,
and ModelPoseSampler treats every non-STEP mode as linear/slerp. Model cache v9
can retain those lossy results. This is a behavior fix to existing animation,
not another animation system or asset format.

Normative reference: [glTF 2.0 cubic interpolation and animation sampling](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#interpolation-cubic).
Use the existing engine clip-time wrapping and root-motion policies. For cubic
channels, clamp wrapped sample time to the channel's first/last key, as glTF
requires. Preserve legacy LINEAR/STEP tail interpolation and every other tested
LINEAR/STEP behavior. Document that deliberate boundary in the guide.

## Representation and sampling contract

Keep AnimationSampler's current members. For CUBICSPLINE only, outputs retains
the raw per-key [in-tangent, value, out-tangent] triplets from glTF. LINEAR/STEP
keep one value per key. Document that contract at the type definition; do not
introduce another set of tangent arrays or custom asset schema.

Use componentwise cubic Hermite with normalized interval position and tangents
scaled by the actual interval in seconds. Translation and scale use XYZ;
rotation uses all XYZW components then normalizes the result. Do not normalize
tangents, slerp, or independently flip quaternion signs. Exact keys and clamped
endpoints use the authored value (normalize rotation values).

For malformed cubic data, skip the affected channel and preserve its current
local component: require at least two keys, exactly three outputs per key,
finite strictly increasing input times, finite relevant output components, and
a finite nonzero sampled quaternion for rotation. Reject non-finite computed
results as well. Avoid overflow in count checks. This bounded defense must not
turn the sampler into a general hostile scene-graph validator or alter legacy
LINEAR/STEP fallbacks. Keep Model's API and all rendering backends unchanged.

## Cache compatibility and production test seam

Bump model cache version 9 to 10 so old tangent-stripped caches trigger reimport.
The existing sampler binary layout already stores arbitrary output vectors;
keep its field order/widths unchanged. Do not rewrite the full model cache.

Extract only sampler read/write plus shared magic/version identity validation
into a small internal CPU helper, ModelCacheAnimation.h/.cpp (namespace detail
or equivalent). ModelCache must use that exact helper for real sampler I/O and
header identity checks before consuming cached state. The helper takes standard
streams and AnimationSampler; it has no GL/Model constructor dependency. Shared
constants initialize the existing local packed header. Read failure must return
false without publishing a partial sampler. Preserve existing non-animation
cache layout and behavior. This seam permits meaningful CPU verification of the
production codec; a test-only copy or a constant-only version test is inadequate.

Cover raw tangent roundtrip and sampling after roundtrip, LINEAR/STEP layout,
truncated data, v9 rejection, v10 acceptance and wrong magic rejection through
the production helper. Keep the original header identity check ahead of all
cached state mutation. General cache hardening and portable cache formats are
out of scope.

## CPU and native acceptance

Extend real import tests with tiny authored glTF JSON/little-endian buffers
through tryLoad and buildSceneData into samplePose: translation, rotation and
scale preserve tangents and produce independently calculated results. Use
non-unit intervals, asymmetric tangents, multiple segments, exact key samples,
clamped channel endpoints, quaternion normalization/sign-sensitive curves,
degenerate quaternion and malformed cubic data. Expected numbers must not be
computed with a copy of the evaluator under test. Keep all 23 existing CPU
contracts and the seven CTest entries passing; add focused cases as appropriate.

Extend the existing animation fixture with two cubic rotation cases (six total),
using nonzero tangents and independently specified analytic tip angles whose
visible results differ from linear fallback. Retain the original four cases
unchanged. Reuse the existing hosts, two-joint weighted rig, stable palette
allocation, provenance checks and all established negative guards.

Extend the complete native matrix to 6 cases x 3 APIs x 2 hosts = 36 images.
Verify runtime and actual editor on OpenGL/Vulkan/D3D12 with requested/observed
backend matching, no fallback, expected-content checks, unchanged parity limits,
matching logical frames, hashes and capture provenance. Add a synthetic
linearized-cubic defect and prove the guard rejects it on both cubic cases,
including the CLI harness. Ensure handshake expected poses stay independent of
samplePose. No CPU deformation of submitted vertices.

Compare the original 24 native cases against retained M1-G m1g-run04 captures
and report differences under the unchanged approved thresholds. Run the existing
24-case public archive matrix as regression coverage. View actual new native
screenshots and contact sheet, not only JSON. Do not claim the public fixture
qualifies private characters, every GPU, legacy Model::drawAnimated, morphs,
blend graphs, animation events, attachments or performance budgets.

## Ownership and execution

Worker owns ModelAnimationTypes.h, ModelPoseSampler.cpp (header only if needed
for contract comments), gltf/ModelFastGltfSceneData.cpp, ModelCache.cpp, narrow
ModelCacheAnimation.h/.cpp, CMake registration, relevant existing CPU tests and
TestMain wiring, examples/animation_fixture, tools/animation_fixture,
tests/animation_fixture, docs/ANIMATION_VERIFICATION.md,
docs/GLTF_IMPORT_VERIFICATION.md, and this phase's REPORT.md.
Root owns PLAN/CHECKPOINT, roadmap, docs/README and docs/VERIFICATION.

You are not alone in the workspace. Preserve others' edits; do not stage,
commit, push, spawn children, edit another repository or change dependencies,
router configuration, backend/shader code or unrelated modules. New reusable
logic stays under src, runnable examples under examples, guards under tools,
automated checks under tests. Stop for an architecture blocker outside this scope.

Use native PowerShell and the existing build-presentation Debug configuration
(editor/tests enabled, warnings as errors). Full build, full CTest, direct CPU
contracts, 36-case animation matrix, prior-capture comparison, archive regression
and meaningful codec checks are required on final source/binaries. Retain actual
commands, exit codes and raw artifacts under ignored artifacts/m1-h. Do not
loosen checks or hide failures. Reuse existing safe process/temp-directory tools.

Allow up to 90 minutes for implementation, testing, debugging and native QA.
No progress polling or play-by-play is needed. Report ready_for_review with
changed paths, checks/results, coverage limits and artifact locations. Astra
reviews the actual tracked/untracked diff and evidence, then returns at most one
consolidated correction by default. New M1-H work remains uncommitted for review.
