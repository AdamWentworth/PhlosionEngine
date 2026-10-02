# Runtime extraction checkpoint

Superseded by [VERIFICATION-CHECKPOINT.md](VERIFICATION-CHECKPOINT.md).

Full authorized task is STILL IN PROGRESS. Continue independent integration and
native verification; this is not completion. Root implements directly after the
Flash worker failed before edits with HTTP 402 and was interrupted. No active
children. No new commits/pushes. Preserve private assets and IDs.

## Current code

- Engine ModelsCore: neutral data, cooked codecs/KTX, billboards, material and
  visibility tracks, ModelPoseSampler + shared AnimationTrackSampler. Render
  links ModelsCore. Image codecs build once as Phlosion::ImageCodecs.
- Engine Models: WorldIndexedBatches, ModelGeometryCache (owned/cache overloads),
  ModelMaterials (ordinary texture setup/emission), ModelTransforms (CPU/GPU
  skinning), ModelAttachments, ModelInstance + optional ModelProfile.
  Instance owns geometry/material variants/16 pose slots and preserves prior
  resource on failed loads. Its in-memory identity gains a unique revision.
- Engine ModelRenderProvider implements ABI5 without editor dependencies.
  Callers now explicitly choose grounding/clearance/lighting preset; defaults
  are ungrounded and ordinary lighting. Path/stamp replacements are transactional.
- Engine ModelPreview + ModelPreviewRuntime own inspector presentation. Target
  Phlosion::ModelPreview links Models; lighting extension is ModelLighting.
- PokemonModelSupport owns source importer/shaders/materials, PoseCompatibility,
  MaterialCompatibility/Quality, SupportContact, source ModelProfile/SourceLighting.
  Independent PokemonModelCook, PokemonModelRuntime.dll and
  PokemonModelPreviewPlugin.dll targets. Explicit PHLOSION_MODEL_PROBE_ROOT points
  to a directory containing source_stage_diffuse.png/source_stage_specular.png.
  Editor fallback probes resolve only within its own project.
- Autochess delegates moved code; original redundant codecs/importer/shaders/
  geometry/world-batch/plugin CPP removed. PokemonPrefabPreview is a thin engine
  wrapper. Game action/ledge/procedural/placement/field policies remain game-owned.
  Its game preview target links Phlosion::ModelPreview. Source-based contract paths
  updated to owning repositories without dropping assertions.
- Cyberpunk CMake JUST changed to add adapter, compose character-only shaders,
  build both providers within its graph, configure adapter material profile.
  BattlePreview explicitly restores prior grounding (clearance .0025) and lighting
  preset1 for ABI5. CPU node requests use identical grounding.
  Launch/import/verification/content mount scripts are NOT switched yet.

## Evidence

- Prior CPU/core and standalone 10 adapter tests passed Debug and optimized.
  Eight composed Autochess shader sections matched pre-extraction composition.
- Full Engine tests passed after sampler consolidation (before later instance/
  provider/loop-argument edits). Latest strict engine semantic guard and adapter
  no-game-includes guard pass.
- New PhlosionModelInstanceTests passes Debug: fractional CPU/render agreement,
  palette modes, slot/instance isolation, one-shot/loop endpoints, failed load,
  unsupported profile, missing-node output preservation, unload/visibility boundary.
- Engine ordinary preview target builds Debug. Standalone adapter both provider
  DLLs build Debug with no game dependency (session24630 completed).
- Autochess Debug PAC_Tests + both provider DLLs + PokemonAutochessEditorProject
  build (session88366 completed). Latest four-test gate passes IR/cache/hotpath/VFX
  (session37070). Earlier full PAC run had 3 stale source-path failures after moves;
  all paths fixed, fire test still requires rerun. Filters are EXACT, not substring.
- No native captures since migration. New runtime optimized builds still required.

## Active build

Cyberpunk Debug build session98634 (poll next), log
`artifacts/model-separation-cyberpunk-debug-build.log`, targets game runtime/editor,
both model DLLs and guard. All other recent sessions completed.

## Next steps

1. Switch Launch-Editor away from Autochess. Stage model DLLs from Cyberpunk's own
   build/CONFIG. Runtime env must name PokemonModelRuntime.dll. Copy dependencies
   from its configured vcpkg bin/debug/bin or own build. CMake now composes shaders;
   remove duplicated launcher shader copying/compiling. Rebuild engine editor.
2. Explicit shared private publication/mount: preserve mount ID `autochess-pokemon`
   and `content/autochess` for serialized compatibility. Current junction points to
   Autochess/content/phlosion. Nothing private moved/copied yet. Proposed snapshot
   D:/ProjectData/Phlosion/PokemonModels/cooked of 570 model object directories and
   only referenced texture dependencies. Source cook_manifest.json has pokemon +
   staged_imports records with object/texture_dependencies; inspect staged shape.
   Corpus ~17GB, D: ~835GB free. Verify copies/manifest; never move/delete originals.
   Copy probes separately. Local publication only, no uploads.
3. Move reusable NativeModelPayloadStore.psm1 to adapter tools, keep game wrapper;
   Import-SV-Pokemon and research publishers use PokemonModelCook. Inspect importer
   ForgePath command compatibility. Verification scripts also still reference
   Autochess helper modules/layout/profile; migrate reusable verification helpers
   appropriately, preserving thresholds and exact evidence checks.
4. Run native comparisons: accepted Espeon hind legs/glint/shiny seams+green;
   Umbreon dim↔yellow/SV teal, representative native materials/source-stage lighting;
   runtime+editor all3 APIs, relevant 16:9/21:9. Existing scripts/artifacts are basis.
5. CPU/debug+optimized gates; isolated configure/build/launch without sibling-game
   lookup; runtime link audit; final ownership docs/tests and cleanup.

All scripts in Engine artifacts/model-separation were already run and MUST NOT
be rerun on transformed sources. New runtime/preview needs review and native
parity. Wireframe currently doesn't filter hidden submesh triangles; fix while
verifying. Batch copies now retain sharedSkinMatrices into instance slot storage
(added after initial instance test; rebuild). Generic engine plugin targets for
non-Pokemon consumers are not yet added; public provider/preview host APIs exist.
