# M1-F: verify the existing character-import path

Date: 2026-09-20. Astra design and acceptance; one native astra_flash_builder.
Baseline: engine main `9d094c91b9b801459069dedfd8eaaac213c821b2`.

## Objective and decision

Close a verification gap in existing functionality, following the user's
clarification that meshes, animation and materials already work. Add public,
asset-independent CPU tests of the actual glTF scene/skeleton/animation import
and material-resolution functions. Do not build another runtime format or
replace the animation system. M1-E's typed mesh format is committed and optional
pipeline work; M1-F does not depend on adopting it in the production Model path.

Repository evidence:
- Model.h and ModelAnimation.cpp already provide clips, pose sampling, skinning
  and node lookups. Model construction/drawing requires OpenGL initialization.
- gltf/FastGLTFLoader.h::loader::tryLoad parses an actual path, using the existing
  options and supported-extension mask.
- gltf/ModelFastGltfSceneData.h/.cpp::buildSceneData is the existing CPU importer
  for node defaults/names/children, mesh and skin assignments, selected scene
  roots, joint order/inverse binds, clips/samplers/channel targets and paths.
- gltf/ModelFastGltfMaterial.h/.cpp::resolveMaterialRenderInfo is a CPU function
  even though the same object file also contains GL upload helpers. Test the
  resolver without constructing Model or calling uploadTexture2D.
- PhlosionEngineTests already links Phlosion::Render and needs no graphics
  context. CMake propagates fastgltf and glm. There are 18 C++ contracts today.

Use these existing seams rather than copying production algorithms into tests,
adding test-only runtime APIs or refactoring Model just to instantiate it.
Keeping production source unchanged avoids modifying animation/render behavior
before its existing support and limits have been recorded.

## Test contract

Add TestGltfSceneImport.cpp and TestGltfMaterialImport.cpp, registered in the
existing TestMain/CMake test target. Use bool test_gltf_scene_import_contract
and bool test_gltf_material_import_contract with std::string& diagnostics.
Small shared fixture helpers may live under tests/gltf_import if useful; avoid
an elaborate fixture framework. Fixtures must be authored here, tiny and fully
public. Generate JSON plus explicit little-endian .bin data in a uniquely owned
temporary directory (RAII cleanup of only that directory), or retain small
reviewable fixtures under tests/gltf_import with reliable CMake source paths.
Do not add opaque downloaded assets, private game content or external tooling.

Scene contract goes through loader::tryLoad on a real glTF file and then the
actual buildSceneData, with fastgltf's existing buffer adapter. Assert authored
expected values, not outputs of a duplicate importer. Required cases:
- Named hierarchy with translated/rotated/nonuniformly scaled nodes and known
  child ordering, mesh/skin indices and a non-first selected scene; default
  scene absence uses the first scene. No-scene and empty-data behavior may be
  characterized if meaningful, without pretending it is malformed-input safety.
- Joint index ordering and nonidentity inverse bind matrices, including matrix
  column order; omitted inverse binds get the current identity behavior.
- At least two distinct clips; LINEAR and STEP translation/rotation/scale
  channels, known key times/values (including quaternion xyzw order), target
  node/sampler associations, duration from the last key across samplers, names.
- Passing prefilled output containers and importing again does not accumulate
  stale nodes/skins/clips; null outNodeNames is supported.
- Missing file, malformed JSON and a missing required external buffer fail at
  the actual loader boundary. Do not feed unchecked corrupt accessors into
  fastgltf iterators or claim broad malformed-content validation.

Material contract invokes resolveMaterialRenderInfo on parsed fixture materials
where practical. Cover defaults/invalid material index, authored PBR factors,
normal scale, occlusion strength, emissive factor with emissive-strength applied
once, OPAQUE/MASK/BLEND, alpha cutoff and double-sided state. Use literal small
CPUTexture pixel arrays to demonstrate the current blend-normalization policy:
opaque alpha becomes OPAQUE, binary alpha becomes MASK, intermediate alpha
remains BLEND. Record this as existing engine policy, not a universal glTF
guarantee; do not silently alter it or introduce project-specific material IDs.
No shaders, texture upload, Model construction or graphics context in tests.

## Document coverage honestly

Add docs/GLTF_IMPORT_VERIFICATION.md distinguishing what already exists, what
these tests actually execute, and remaining evidence/feature gaps. In particular:
- CPU import tests do not sample poses, evaluate skinning matrices, draw pixels,
  verify full vertex/material/texture import, or qualify a private character.
- Existing buildSceneData strips CUBICSPLINE tangent entries, retaining values;
  ModelAnimation's non-STEP sampling uses linear/spherical interpolation. Record
  this inspected fidelity limitation explicitly. Do not enshrine tangent loss as
  desired behavior or claim full cubic support. Fixing it is a later bounded
  runtime change with native visual qualification.
- Morph weight channels are not represented by ChannelPath; advanced blending,
  events, root-motion consumption and managed attachments remain separate work.
- Material factor checks do not prove material appearance or backend parity.
- Existing three-API evidence from M1-E remains that fixture's evidence; do not
  relabel its rigid marker as skeletal animation qualification.

If a supported-behavior test exposes a production defect, retain a precise
reproduction/report and return the architectural choice to Astra before editing
runtime code. Do not weaken a test or silently broaden to a renderer change.

## Ownership, verification and completion

Worker owns new test files/helpers, CMake test registration, TestMain, the new
guide and docs/agent-work/cyberpunk-m1-gltf/REPORT.md. No src/, shaders, examples,
tools, dependencies, game/package or infrastructure edits. Root owns PLAN,
CHECKPOINT, roadmap, verification/index and integration. You are not alone in
the workspace: preserve others' edits, no recursive agents, staging, commits,
pushes, settings changes or external services.

Run the complete existing build-presentation Debug build (warnings as errors),
all six CTest entries and direct engine contracts. Retain logs under ignored
artifacts/m1-f. Demonstrate the new tests need no graphics context and run from
the CTest build directory. No new GPU run is required for test/docs-only work;
there is no new visual qualification claim. Verify actual runtime/source files
are unchanged against baseline. Check whitespace and local documentation links.
Use existing scripts/dependencies. Ordinary debugging stays with the worker.

Return ready_for_review with changed paths, concrete coverage, commands/results,
artifact paths and discovered limitations. Allow a coherent implementation and
test loop up to an hour; checkpoint only for a real blocker or host limit.
Astra reviews actual tracked/untracked diff and evidence, then assigns acceptance.

## Review refinement

Astra requested a third, narrow fixture-ownership contract after finding shared
temporary paths in the first implementation. It verifies unique acquisition,
cleanup isolation and rejected path components. The first correction also made
the skinned document structurally coherent and added the existing parser's
validation check. A second correction is justified by a concrete filesystem
risk: the new regression's own sentinel setup still overwrote shared paths.
Its setup must acquire its own unique namespace before writing, clean up only
owned paths and avoid assumptions about unrelated temporary files. This remains
test-support work; production scope and rendering claims are unchanged.
