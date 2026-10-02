# Final integration checkpoint

Completed. This intermediate checkpoint is superseded by [RESULT.md](RESULT.md).
All listed remaining native, isolation and targeted CPU gates passed. The final
model-instance API also prohibits copying; Debug/optimized ownership tests pass.
No active child agents or required implementation work remains. Changes are
uncommitted and unpushed; no private assets were added to Git.

The historical in-progress notes below are retained as execution evidence.

Supersedes RUNTIME-CHECKPOINT.md. User authorized full engine-owned model support,
optional PokemonModelSupport, independent games and verification. Finish the
remaining native checks before concluding. No commits or pushes requested for
this refactor. Flash child failed HTTP402 before edits and was interrupted; root
implemented directly. No active children. Do not retry/change provider settings.

## Current implementation

Engine owns ModelsCore, Models (included by Phlosion::Engine), ModelPreview,
ModelRenderProvider ABI5 and generic PhlosionModelRuntime/PhlosionModelPreviewPlugin
DLL targets. Generic core/instance/provider tests need no private assets or GPU.
Source adapter repo D:/Projects/Phlosion/PokemonModelSupport owns IR, source
materials/shaders/profile/pose compatibility/support anchors/probes, cooker and
both Pokemon provider DLLs. Runtime does not link editor/ImGui. Game policy stays
in each game. Autochess's inspector is a thin engine wrapper. Old moved CPPs and
shaders removed, compatibility headers retained.

Cyberpunk CMake/launcher/import/verification use shared adapter and engine;
PokemonModelRuntime.dll is separate from PokemonModelPreviewPlugin.dll. Both
compile in its own build; PokemonModelCook now outputs in build/CONFIG too.
ABI5 callers explicitly request old grounding (.0025 clearance) and lighting1.
Null/empty animation supports bind pose. Camera inputs validated. Instance slots
own stable palettes, failed replacement retains state, successful query clears
error. Wireframe now respects visibility. CPU + plugin lifecycle tests cover
replacement, unsupported profile, one-shots and fractional sampling.

Shared private publication: D:/ProjectData/Phlosion/PokemonModels/cooked.
570 models, 6882 model files +2 probes copied and SHA256-verified; original private
files untouched. Publication manifest model_catalog.json. Cyberpunk content/autochess
junction now targets this publication. Legacy IDs preserved. Explicit argument or
PHLOSION_POKEMON_CATALOG_ROOT chooses mount; PHLOSION_MODEL_PROBE_ROOT chooses probes.
PowerShell publisher uses List.ToArray() (fixed manifest-writing error).

Generic RenderParityImageDiff/ContentGuard moved to Engine/tools/render; Autochess
wrappers retain callers. NativeModelPayloadStore moved to adapter/tools; game
wrapper retained. Cyberpunk owns its capture layout/unchanged numeric thresholds.
Import-SV uses own cooker + adapter module; PlanOnly passed. SourceProjectDepot
still explicitly defaults to a retained private Autochess-named *data snapshot*,
not game code. Research publish_umbreon supports --cooker (keeps --forge alias),
default adapter cooker; research native battle harness uses runtime DLL.

Docs updated: Engine/docs/MODELS.md + README; adapter README/LICENSE/LICENSING;
both game boundaries, Cyberpunk model/editor/setup docs; Autochess material/file
map docs preserve source licensing exclusions. Old package proposal superseded.

## Latest source change / lifetime refinement

Adapter ModelProfile now calls pure buildIndexedBatchTemplates. ModelInstance owns
templates; it no longer adds entries to the legacy game material cache. The legacy
cached entry point delegates the exact same builder, preserving game references.
Source pose caches are thread-local, metadata maps bounded at 256, identity checked;
support-contact metadata bounded too. New PokemonModelRuntimeTests covers 300 live
generations, replacement, retained instance isolation, missing profiles and clamp.
Standalone 11 adapter tests pass Debug/RelWithDebInfo; latest expanded runtime
stress passes both. Build/test logs adapter/artifacts/lifetime-* and runtime-stress-*.
No more code edits anticipated unless verification reveals a real issue.

## Completed evidence

- Engine ModelsCore/ModelInstance/EngineContracts/strict semantic boundary PASS
  Debug and RelWithDebInfo. Generic provider DLLs built both configs. No compiler
  warnings in relevant engine build logs.
- Autochess full PAC_Tests PASS Debug and RelWithDebInfo. Debug native stderr must
  use ErrorActionPreference Continue (Stop aborts at expected error messages).
  One stale source-inspection assertion in TestGltfAsset was updated to engine
  ownership without removing its direct-PHLO/no-cache requirement. Full suites
  preceded latest lifetime refinement; targeted fresh opt tests pending below.
- Autochess 19 boundary/material CTest gates PASS. Native payload module wrapper
  and generic image guard module wrapper tests PASS.
- Cyberpunk all11 CTests PASS Debug/optimized. ABI5 ctypes sampling PASS both configs.
- New cooker recooks shiny Espeon into artifacts/model-separation/recook; mesh,
  skeleton, animation, material resource bytes IDENTICAL to accepted canonical.
- Cyberpunk inspector all47cases x3 APIs captured, content/API checks PASS across
  two runs: inspector-optimized log early30 and inspector-remaining log final17.
  Dedicated fur/ring validators PASS. All24 accepted shiny Espeon model crops and
  all30 accepted Umbreon dim/teal model crops are PIXEL IDENTICAL. Comparison JSONs
  in Cyberpunk/artifacts/model-separation. One old special-a guard sampled fully dim
  rings and failed contrast; moved ROI to visible face/eyes keeping same threshold15
  (OpenGL25.2/Vulkan19.85). No thresholds relaxed; other guards retained.
- Debug shiny battle idle: all12API/host/aspect captures PASS. Debug script stopped
  at next phase because a concurrent Debug rebuild made its staged plugin stale.
  Optimized full shiny/ring native runs are active instead (no more same-config
  builds while they run).
- Independent source exports at
  D:/ProjectData/Phlosion/Qualification/model-separation-20261002:
  PokemonCyberpunk/source and PokemonAutochess/source, no sibling game at expected
  lookup path. Both freshly configured and built optimized game runtime/editor and
  model DLLs. Generated build graph scan: zero other-game references.
  dependency-audit.json and per-project configure.log/build.log. Source exports
  predate lifetime refinement; optional refresh/rebuild of shared adapter will pick
  up latest code. No destructive checkout renames.
- Isolated Cyberpunk extra rules/VFX/guard built. base/Phlosion junction points to
  shared libraries; isolated source/build junction points to its own build. Its
  assets + content/phlosion/{objects,dependencies} link to its own canonical private
  files. Isolated native launch still pending.
- Engine whitespace diff gate PASS (use normal Git CRLF settings or
  core.whitespace=cr-at-eol; do NOT set core.autocrlf=false, it yields fake CR errors).

## Active sessions and next exact actions

1. Cyberpunk session6913 ACTIVE sequential GPU pipeline. It rebuilt/staged optimized
   providers after lifetime fix, finished final17inspector + fur/ring validators,
   now running Verify-Shiny-Espeon then Verify-Umbreon-Rings. Poll it. Logs:
   artifacts/model-separation-shiny-optimized.log and ...-umbreon-optimized.log.
   Outputs artifacts/model-separation/{shiny-battle-optimized,umbreon-battle-optimized}.
   Expect36shiny +48ring captures on all3APIs/bothhosts/bothaspects. GPU jobs sequential.
2. Autochess full Debug session22264 has completed all tests PASS (poll to close).
   Earlier build session97865, Cyberpunk47397, export40170 also completed; may close.
   Latest Autochess opt lifetime build11438 completed. New standalone stress81455
   completed PASS. No active compiles except if explicitly started later.
3. Run targeted Autochess fresh optimized tests after lifetime refinement. Last
   attempt passed model_asset_smoke then guessed a wrong filter and exited2. Correct
   EXACT names: phlosion_native_model_ir_contract, native_charmander_family_fire_contract,
   shared_projected_unit_renderer_cached_batch_material_identity_contract,
   shared_projected_unit_renderer_segment_scale_compensation_contract,
   shared_projected_unit_renderer_facial_overlay_base_pose_contract.
   Inspect TestMain for any other names; --filter matches EXACTLY.
4. AFTER Cyberpunk GPU pipeline, Autochess native representative material checks:
   tools/check_character_materials.ps1 -Configuration RelWithDebInfo
     -EditorBuildDirectory build-presentation -OutputDirectory artifacts/model-separation/character-editor
   New EditorBuildDirectory param reuses qualified shared editor. Matrix has17cases
   (51captures) covering allsource roles/probes; full matrix reasonable.
   tools/check_native_character_materials.ps1 -Configuration RelWithDebInfo
     -OutputDirectory artifacts/model-separation/character-runtime
   runtime cohorts use tools/render_parity_matrix.ps1 and same unchanged thresholds.
   Both modules now delegate generic helpers correctly. Build runtime/editor already
   fresh optimized. Do not run GPU jobs concurrently. Can use isolated optimized
   Autochess executable via render_parity_matrix -BuildDir <isolatedbuild> to prove
   isolated launch (need ensure own data root/shader setup); source code is independent.
5. Isolated Cyberpunk Launch-Editor -SkipBuild -Probe from exported source should
   work now with library/build/private mount junctions described above. Stage own
   DLLs. Then Verify-Outskirt-Encounter at least a battle phase to exercise models;
   no sibling Autochess path exists. Avoid overwriting original accepted artifacts.
6. Final code/doc review, state/whitespace checks all repos incl untracked new files,
   update this checkpoint + final evidence report/PLAN. Must honestly report any
   outstanding coverage. No new commits/pushes.

All one-shot migration Python scripts under Engine/artifacts/model-separation
were already run; do not rerun them on transformed sources. No user-owned private
model payload was modified. No active child agents; interrupt any finished child
if a notification resurfaces.
