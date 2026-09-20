# M1-G: existing pose sampling and native skeletal deformation

Date: 2026-09-20. Astra designs/reviews; one astra_flash_builder implements.
Baseline: clean engine main 9c0d812ce2520f0fbfae6dd5bcccc0e42e118339.
M1-F is committed; the user authorized proceeding to the next pass.

## Objective and architecture

Verify existing animation playback and actual weighted GPU deformation with a
tiny public rig, through native runtime and real editor preview on OpenGL,
Vulkan and Direct3D 12. Existing engine functionality is the starting point;
this is not a replacement animation system or another custom asset format.

ModelAnimation.cpp contains a CPU pose evaluator but Model construction requires
OpenGL. Expose that existing evaluator through a small production CPU interface
and make Model delegate to it. This gives tests and non-OpenGL consumers the same
implementation without fake GL, private-member hacks or copied algorithms.
Preserve current sampling behavior exactly. GPU backends already accept weighted
WorldMeshVertex data plus skin matrices through WorldTextureData/WorldMeshInstance.

Add src/engine/render/ModelPoseSampler.h/.cpp in Phlosion::Render with:

```cpp
namespace engine::render::animation {
struct PoseSource {
    const std::vector<model_types::NodeTRS>& nodesDefault;
    const std::vector<std::vector<int>>& nodeChildren;
    const std::vector<int>& sceneRoots;
    const std::vector<model_types::SkinData>& skins;
    const std::vector<model_types::AnimationClip>& animations;
    const std::vector<std::string>& nodeNames;
};
void samplePose(const PoseSource& source, float timeSec, int animIndex,
                std::vector<model_types::NodeTRS>& outLocal,
                std::vector<glm::mat4>& outGlobal,
                bool suppressHorizontalRootMotion = true);
}
```

PoseSource borrows its arrays for the call only. Inputs must be validated,
acyclic imported data, and must not alias outputs; this is not a hostile-input
validator. Retain Model's public API, defaults, diagnostics and draw behavior.
Model::buildPoseMatrices becomes a thin call to this implementation. Keep its
other helpers if drawing still uses them; avoid unrelated cleanup. No changes to
animation types, cache schema, backend interfaces, shaders, joint limits or
root-motion policy. No new blending, events, attachments, cubic interpolation,
morphs, importer/cook or Pokemon-specific policies in this pass. Any defect
requiring behavior changes returns to Astra with evidence before expanding scope.

## CPU and compatibility contract

Add tests/TestModelPoseSampler.cpp to the current CPU test executable. It must
invoke the real shared implementation with tiny authored data and independently
specified expected matrices/positions/quaternions, not a duplicate evaluator.
Cover defaults/empty input/invalid clip, LINEAR and STEP T/R/S, normalized
quaternion interpolation, a one-key channel, negative and looping time, exact
positive endpoint policy, clips with a channel shorter than total duration,
hierarchy transforms, output replacement, horizontal root-motion suppression
enabled/disabled (named carrier and skeleton-root carrier, keep animated Y),
unaffected noncarrier translation, and segment-scale compensation. Characterize
the existing loop/root-motion rules as engine policies, not universal glTF rules.
Do not freeze CUBICSPLINE tangent loss or feed cyclic/corrupt graphs to recursion.

Keep extraction reviewable against the original function. Retain a bounded
differential proof in ignored artifacts if useful, comparing the baseline CPU
algorithm and extracted implementation on the supported cases. No baseline copy
ships in production or permanent tests. Existing 21 CPU contracts remain intact.

## Public native fixture

Runnable sample belongs in examples/animation_fixture, verification utilities in
tools/animation_fixture, automated checks in tests (including any harness tests).
Do not put production engine logic into examples. Keep the sample independent of
verification libraries; reuse current process/image primitives where practical.
Use the existing public_asset_fixture hosts/plugin and capture tools as patterns;
do not refactor their accepted schema or weaken their guards to accommodate this.

Create a small public rig through the existing animation structures: at least
two joints, nonidentity inverse binds and mesh transform, fixed base, moving tip,
and visibly blended weights between them. A simple colored strip/hinged shape is
enough. It need not repeat glTF import, which M1-F already covers. Submit original
bind vertices with joints/weights and GPU skin matrices through the existing
native backend API. Do not CPU-deform submitted vertices or substitute rigid
motion for skinning. Caller-owned palette uses inverse(meshGlobal) * jointGlobal
* inverseBind, with lifetime covering the draw. CPU expected-point checks must
demonstrate correct palette space and non-rigid weighted deformation.

Shared presentation runs in a standalone native window and an actual editor
project plugin using the existing ABI. No renderer/editor-host implementation
changes. Each host first presents bind pose, then the requested pose using the
same geometry and palette storage so an unchanged/stale palette is observable.
The fixture has four deterministic cases: bind, LINEAR intermediate, LINEAR near
endpoint, and a distinct STEP state. Define exact clip/time/logical frame values
in one case catalog; record them separately from the screenshot host frame.
Choose simple authored transforms and colors making each expected deformation
analytically checkable. Keep the camera/resolution/scene identical across APIs.

Capture the full 4 cases x 3 native APIs x runtime/editor matrix (24 images).
Requested and observed native API must match; no fallback. Include expected
anchored-base, moved-tip and blended-middle content checks, not only image parity.
Expected regions/landmarks derive from authored known outcomes, not the sampled
pose under test (which could make a broken sampler validate itself). Guards must
reject disabled skinning, a stale bind palette, and incorrect joint/weight/space
results. Include CPU synthetic/adversarial checks that demonstrate those failures.

Reuse approved image-comparison limits: normalized MAE <= 0.01, RMSE <= 0.08,
changed ratio <= 0.05 at channel tolerance 8. Do not increase thresholds to hide
failures. Native matrix validation requires every unique host/API/case, matching
logical time, observed capture frame/backend, process exit success, expected
content, executable/plugin hashes and source baseline/dirty state. Missing,
duplicate or tampered evidence fails. Retain raw screenshots, metrics, manifests
and a contact sheet. View the actual native screenshots as well as metrics.

Fixtures must remain opt-in for consuming child projects and CPU CTest must
need no graphics context. Use a new PHLOSION_BUILD_ANIMATION_FIXTURE option with
the same standalone/child defaults as the existing public fixture option.
Verify hosts disabled with tests enabled and child-project default-off behavior.
Keep temporary/output ownership safe: atomically acquire unique test directories;
never predelete a shared path; validate absolute cleanup targets remain in the
owned scope. Reuse the existing bounded native process launcher with hidden windows.

## Ownership and verification

Worker owns ModelPoseSampler.h/.cpp, the pose-evaluator extraction in
ModelAnimation.cpp, CMake registration/options, tests/TestMain.cpp, new CPU tests,
examples/animation_fixture, tools/animation_fixture, tests/animation_fixture if
needed, docs/ANIMATION_VERIFICATION.md and this phase's REPORT.md. A tiny shared
image/process reuse wiring change is allowed only if existing behavior and tests
stay intact; otherwise stop and explain the required boundary change.
Root owns PLAN/CHECKPOINT, roadmap, docs/README and docs/VERIFICATION integration.
You are not alone: preserve others' work, no staging/commits/pushes, recursive
agents, other-repository edits, dependency/settings changes or private assets.

Use native Windows tools and the existing build-presentation Debug tree with
editor/tests enabled and warnings as errors. Run full build and all CTest entries,
direct CPU contracts, new complete native matrix and existing 24-case archive
matrix as regression coverage once final source/binaries are stable. Retain logs
under ignored artifacts/m1-g with real exit codes and sufficient failure detail.
No claim that these public cases qualify private characters, full production
materials, instanced/indirect skinning paths not actually exercised, every GPU,
or performance budgets. Clearly distinguish shared sampler/backend coverage from
the legacy OpenGL-only Model::drawAnimated path.

Allow a coherent implementation/test/debug/native QA loop up to 90 minutes;
checkpoint only for a genuine blocker, material architecture/production defect
or host limit. Do not send play-by-play progress. Return ready_for_review with
actual paths, commands/results, precise coverage/limits and evidence locations.
Astra reviews the actual full result; passing build alone is insufficient.
