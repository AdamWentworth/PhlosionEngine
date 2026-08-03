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
all Pokemon-specific editor adapters. It configures `phlosion.tile-tools`, but
does not own the generic package implementation.

## Enforced boundary

`PhlosionEngineSemanticBoundary` scans engine source and shaders for project
vocabulary. It also asserts that the moved LGPE decoder and legacy card widget
cannot return to their former engine paths. Adding an exception is an
architectural decision, not routine test maintenance.

## Explicit compatibility debt

The shared world pipelines currently contain the recovered LGPE field-material
GPU evaluator in exactly five allowlisted backend shader files. It is dormant
unless a project submits those specialized material-mode IDs, but it is still
compiled into the reusable renderer. The correct long-term removal is a
backend-neutral, project-loadable shader/material-profile package selected
before renderer pipeline creation. Until that API exists, the boundary test
keeps this compatibility island fixed and prevents it from spreading.

For a racing game or shooter today, the compatibility evaluator is the only
Pokemon-derived implementation left in the engine binary. No Pokemon data,
scene loader, editor tool, gameplay system, prefab, or environment is included.
