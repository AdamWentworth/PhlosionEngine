# Model support

General 3D model support is part of PhlosionEngine. Applications do not need a
Pokemon package to load, animate, render, cook or inspect ordinary models.

| Target | Responsibility |
| --- | --- |
| `Phlosion::ModelsCore` | CPU model data, PHLO resource cooking/loading, KTX2 dependencies, skeletal/material/visibility sampling |
| `Phlosion::Models` | Model instances, owned geometry/materials/pose slots, skinning, billboards, indexed submission, runtime provider |
| `Phlosion::ModelPreview` | Ordinary asset inspector using the same model instance |

These are internal engine targets, not separately installed packages.
`Phlosion::Engine` includes `Phlosion::Models`; narrower consumers can link the
CPU, runtime or editor target directly.
`PhlosionModelRuntime` and `PhlosionModelPreviewPlugin` provide the generic
runtime and editor DLL entry points. Build them explicitly when a project uses
the plugin route. Applications may instead link the corresponding C++ targets.
The runtime provider does not link the editor or ImGui.

`ModelInstance` loads a cooked resource or accepts neutral `MeshData`. Its
default `ModelProfile` handles ordinary material modes and authored animation.
It rejects required source-specific materials without a matching profile.
An optional profile may supply source interpretation, pose compatibility,
material preparation and visibility; the engine has no species, clip-name or
source-game rules in its runtime. `ModelLighting` supplies optional environment
probes without embedding source-specific lighting policy in the engine.

Each instance owns its resources, material variants and 16 prepared pose slots.
Instances can be moved but cannot be copied; copying borrowed frame pointers
would alias another instance's storage.
A slot's palettes remain valid until that slot is prepared again, the resource
is replaced, or the instance is cleared. Do not reuse a slot while a deferred
submission still borrows it. Failed loads preserve the previously loaded
resource. CPU node queries use the same pose and transform as rendering.
One-shot clips clamp; looping clips wrap. Named missing clips/nodes fail
explicitly, and failed node queries leave their output unchanged.

`ModelRenderPlugin` ABI 6 separates placement and lighting choices from source
interpretation. Callers explicitly enable grounding, choose clearance and set a
lighting preset. Defaults retain authored placement and ordinary lighting.
Null/empty animation selects the bind pose. ABI 6 adds a finite two-component
projection offset in normalized device coordinates (+X right, +Y up), allowing
models inside UI panes without shifting the camera or changing the viewport.
The default zero offset preserves existing renders. Older plugins must be
rebuilt with their consumers. The provider caches by caller asset ID and reloads when its
path or PHLO modification time changes; destroying the provider releases its
resources. A game owns action selection, placement and procedural movement.

The current PHME/PHAN/PHMT resource codec retains existing cooked payloads.
The engine's existing static PHMS schema remains a distinct format. External
source importers translate their data to `MeshData` and call `cookModelObject`.
Source extraction, retargeting and recoloring do not belong in this module.

`PokemonModelSupport` is one optional consumer of these APIs. It owns recovered
Pokemon IR/material interpretation, compatibility rules and shaders. Both
Pokemon games depend on that adapter independently; neither provides the
other game's model runtime. Private catalogs are explicit content inputs,
separate from engine, adapter and game code repositories.

The `PhlosionModelsCore` and `PhlosionModelInstance` CTest contracts use public
synthetic fixtures, without private assets or a graphics context. They cover
cook/load integrity, animation tracks, CPU/render pose agreement, slot lifetime,
replacement/failure behavior and the runtime ABI. Native renderer qualification
remains a separate gate.
