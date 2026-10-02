# Model separation checkpoint

This checkpoint is superseded by [RUNTIME-CHECKPOINT.md](RUNTIME-CHECKPOINT.md).

Implementation is in progress. The user authorized the full engine-owned model
separation, optional Pokemon adapter, independent games and required verification.
Do not stop after the completed CPU phase.

## Routing

Doctor observed Astra/xhigh and the pinned Flash role. Child
`/root/model_core_extraction` failed before edits with HTTP 402 insufficient
balance and was interrupted. Root is implementing directly; user was informed.
No provider/model setting was changed.

## Completed CPU foundation

Engine `src/engine/models` owns ModelData, ModelBillboards, PhlosionModelObject
and PhlosionTextureDependencyStore. The original two game codec CPP files were
removed; game headers delegate to the engine implementation and model data aliases
retain source compatibility. Eight game forward declarations now use aliases.
Engine `Phlosion::ModelsCore` links CPU core and KTX; engine vcpkg now declares KTX.
Failed model reload now preserves prior output (a local candidate already existed).

New engine TestModelsCore tests neutral geometry, animation, two texture roles,
material/visibility/attachment tracks, graph agreement, corrupt dependencies,
failed-reload preservation and bad attachment references. Debug and optimized
tests pass. Autochess Debug targets PAC_Tests, PokemonModelPreviewPlugin and
PhlosionForge built; its native model IR and model-cache contracts passed Debug
and RelWithDebInfo. Engine semantic boundary passed after initial extraction.

Logs are in each repo's `artifacts/model-separation`. Migration helper scripts in
engine artifacts are one-shot and must not be rerun on already-transformed files.

## Current adapter extraction

New local Git repository `D:/Projects/Phlosion/PokemonModelSupport` has no commits
or remote. It owns source importer NativeModelIr, recovered MaterialModes, copied
character shaders, ComposeMaterialProfile.cmake, filtered character profile,
offline PokemonModelCook, importer test support and eight shader contracts.
Engine image decoder implementations now build once as Phlosion::ImageCodecs,
shared with render and CPU import; stb include discovery was added.

Standalone adapter Debug cook tool and tests build without either game. All 10
standalone Debug CTests pass (IR roundtrip, boundary and 8 shader contracts).
Autochess CMake now consumes adapter Materials/Import/TestSupport targets and its
material composer. Old native importer/test CPP and old shader files are still
present pending successful game integration, but excluded from compilation.
Remove these duplicates and update ownership guards after the build passes.
The old shader CMake tests forward to the shared contracts with the adapter root.

Autochess Debug integration build is running as exec session 21746; log is
`artifacts/model-separation/adapter-debug-build.log`. Latest observed output was
still compiling tests. Poll it, diagnose failures, run affected tests. Adapter's
first shader contract failure was a stale PAC_ROOT profile path; it was corrected
to its own config/character_materials.json, and all 10 tests then passed.

## Remaining substantial work

- Finish phase 2 integration, optimized verification, remove obsolete duplicate
  source/test/shaders and adjust exact ownership assertions.
- Extract engine runtime from Autochess model rendering and editor classes.
  The game still owns the large projected backend mesh pipeline and pose helpers.
  Do not rename source-specific material logic into engine to bypass semantic
  boundaries. Generic batch submission and geometry caches can move directly;
  recovered material packing, source effect overlays/visibility and source pose
  qualifiers need explicit adapter hooks. Game placement, procedural pose and
  board policy remain game inputs.
- Split the provider runtime from PokemonPrefabPreview; engine owns general
  model rendering and ordinary preview, domain adapter supplies source policies.
- Update Cyberpunk CMake/launch/provider/profile/import and catalog mount to engine
  plus adapter plus direct private-depot publication. It currently still builds
  and stages the Autochess DLL and uses its checkout to find content/probes.
- Update research publisher cook paths; generic Forge primitives live in engine,
  source cook entry is PokemonModelCook, Autochess Forge retains its game commands.
- Demonstrate sibling-game checkout independence, all affected CPU and native
  three-API runtime/editor visual qualification, hashes/identity preservation,
  cleanup bridges and final documentation. No new commits/push were authorized.

The previous PhlosionPackages proposal is marked superseded and links to the
current engine PLAN. Only documentation changed in PhlosionPackages.
