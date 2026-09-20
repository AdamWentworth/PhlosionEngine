# Phlosion Engine boundaries

Phlosion Engine is the reusable host. A project supplies its game runtime,
content rules, imported-source adapters, scene catalog, and editor extensions.
Opening a car-racing or shooter project must not initialize, expose, or require
Pokemon Autochess systems.

## What every game receives

- platform, window, input, logging, environment, and asset-store primitives;
- the `.phrc`, `.phlo`, and `.phscene` container/runtime APIs;
- model, animation, texture, scene, camera, and renderer APIs;
- OpenGL, D3D12, and Vulkan backends;
- the Phlosion Editor shell, project descriptor, project-plugin ABI, generic
  hierarchy/inspector/viewport contracts, and optional editor-package ABI;
- generic VFX host/preview integration points supplied by Phlosion VFX.

## What a project must supply

- gameplay rules, states, simulation, units, UI semantics, and save data;
- scene and game-preview catalogs;
- project editor commands, inspectors, asset previews, palettes, snapping
  policy, and authored-scene mutations;
- source-format decoders and import/cook policy;
- project materials, environment adapters, prefabs, and content.

Reusable feature tooling such as grid-tile selection, terrain palettes,
elevation, ramps, and platform authoring lives in the separate
`PhlosionPackages` monorepo. A project declares only the packages it needs;
the Editor loads no tile implementation for a racing or shooter project that
does not request it.

Pokemon Autochess therefore owns its autochess board and benches, Pokemon
data/model conventions, Route 1 scene adapter, LGPE canonical-scene decoder,
recovered CPU material oracles, terrain palette, battle/planning previews, and
all Pokemon-specific editor adapters. It can configure `phlosion.tile-tools`, but
does not own the generic package implementation. The current Blender workflow
does not activate that optional package.

`BoardRenderer`, `BattleFeed`, `HealthBarRenderer`, and health-bar data now live
in the game. The engine retains generic mesh, text, sprite and UI primitives.
The shared `healthbar` shader filenames remain for asset-depot compatibility;
their implementation is a generic solid-color rectangle used by boot loading.

`EngineServices` holds generic host services and presentation settings.
`EngineFramePerfStats` holds backend-neutral frame measurements. Autochess owns
`GameRuntimeServices`, including shop/round/combat profiling, board flags,
terminal modes, and Growl/Scratch diagnostics. Its runner and editor share that
game-owned state; a plain engine host supplies only the generic base services.

## Enforced boundary

`PhlosionEngineSemanticBoundary` scans engine source and shaders for project
vocabulary and the removed game diagnostics. It also asserts that the moved LGPE
decoder, card widget, board renderer, battle feed and health-bar types cannot
return to their former engine paths. Adding an exception is an
architectural decision, not routine test maintenance.

## Project material profiles

The recovered LGPE field-material evaluator, field-mode dispatch, material
tuning, CPU oracles, and D3D12 field-parameter packing belong to Pokemon
Autochess. They are not compiled into the standalone engine. The former
five-file shader exception has been removed from the semantic boundary check.

`WorldMaterialProfile` provides versioned world vertex/fragment insertion points
and validated scalar packing expressions. A project supplies OpenGL/D3D12 source
sections and six Vulkan variants: direct/indirect vertices, plus fragments with
standard/dual-source blending. The engine's shader templates and default
build do not include project material files. Standard color-space conversion
and RGB/HSV math remain generic renderer helpers.

An empty profile selects the engine defaults. Projects select a profile before
renderer construction; the editor reads the optional `world_material_profile`
descriptor entry before loading a project's scene. Opening another project
replaces the selected profile, and a failed open restores the previous profile.
Renderer-owned copies survive project-plugin unloads. See
[material profiles](MATERIAL_PROFILES.md) for the extension contract.

Recovered character shaders, eye/skin formulas, reflection-atlas decoding,
layered effects, vertex displacement and their per-mode packing now also belong
to the consuming game. Their eight shader contracts moved with the code. The
engine retains standard PBR, skinning, generic shader interfaces and review
presentation. Its semantic guard rejects the removed character identifiers,
without shader-file exemptions. The editor's project-defined lighting preset is
labelled Authored Stage.
