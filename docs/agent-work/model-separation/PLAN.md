# Engine model support and optional Pokemon adapter

Status: Implemented and verified, 2026-10-02. See [result and evidence](RESULT.md).

The user approved engine-owned general 3D model support. This supersedes the
earlier optional `phlosion.models` proposal in PhlosionPackages. General model
loading, cooked resources, instances, skeletal/visibility/material animation,
attachments, rendering, cooking and ordinary preview belong to PhlosionEngine.
Source-specific Pokemon interpretation remains an optional shared adapter. Both
games must consume these independently, without building the other game.

## Baseline and constraints

- Engine: `8804d92e8026572bded84ebbac20976fbb9b9c96`, initially clean.
- Autochess: `04b5b276bb768c73820692e26fafc4d40f452862`, initially clean.
- Cyberpunk: `a008eb86edd98d00636f8061e7fcee51e89c2dc3`, initially clean.
- PhlosionPackages: `d27d065`, only the previous untracked design document.
- Preserve current cooked payloads, IDs, material modes, palette/animation
  results, all renderer thresholds and unrelated work. No automatic commits,
  pushes, private-asset publication or global tool/provider configuration edits.
- Native PowerShell, existing CMake/vcpkg installations, sequential GPU captures
  and Debug/optimized CTest runs. Never move/delete a private depot wholesale.

## Ownership and dependency contract

Engine owns neutral CPU model data, current production cooked payload codecs,
pose/math services, instance preparation, material/visibility tracks, billboards,
render submission and generic runtime/editor provider hosts. Internal targets
separate CPU/cook/runtime/editor dependencies; this is not another external
package. Reuse current engine model/animation/backend APIs and consolidate shared
behavior instead of creating an independent animation implementation.

The optional code package is `D:/Projects/Phlosion/PokemonModelSupport` (new local
repository, no remote or publication required). It owns source-material mode
definitions, recovered shaders/packing, source IR translation, source-specific
pose/material compatibility and their tests. It may depend on Engine; Engine
must not depend on it. It must include no `game/` header and require neither
game repository. Generic contracts must not acquire source/species name tests.

Each game owns resolved action selection, simulation, board/battle policies,
placement/grounding choices, procedural pose policy, cameras, scene catalogs and
content selections. Research retains extraction/retarget/recolor/provenance.
Canonical payloads are mounted by explicit depot configuration/manifest rather
than traversing a sibling game checkout. Keep existing mount IDs when required
for serialized compatibility.

Runtime must not call editor preview classes. The editor uses the same engine
runtime. Generic model render plugin ABI and project material-profile hooks stay
engine-owned. Model preview activation can use the existing asset-preview plugin
route; the tile editor package ABI is not a model provider registry.

## Implementation phases

1. Extract neutral model data and cooked read/write/texture/billboard support to
   engine CPU/cook targets. Keep thin source-compatible game adapters so all
   existing consumers use the one engine implementation. Keep material constants
   and source/cache/ground-support heuristics outside engine. Preserve PHME/PHAN/
   PHMT payload compatibility; engine static PHMS is a distinct existing schema.
   Add real neutral round-trip and failure tests and run existing affected game
   contracts in Debug and RelWithDebInfo.
2. Extract the optional Pokemon adapter and offline source translator. Separate
   generic model cooking from Autochess Forge's catalog/Route 1 commands. Compose
   project profiles from shared character source and project-owned field source.
   No copy of a complete Autochess world profile as the final shared dependency.
3. Extract runtime model presentation from game and editor types, reusing engine
   pose math with explicit policy and source adapter hooks where necessary.
   Wire Autochess and generic runtime/preview hosts to the same implementation.
   Preserve per-instance state, multiple same-frame palettes, resource scopes,
   one-shot endpoints, fractional sampling and error/lifetime semantics.
4. Switch Cyberpunk and research cook entry points to independent engine/adapter
   targets and explicit depot mounts. Remove sibling-game launch/build/import
   dependencies and stale boundary documentation. Validate isolation and all
   affected CPU/GPU behavior before removing superseded bridge implementations.

Stages 2-4 receive refined executable briefs after preceding contracts are
accepted. A phase is not task completion; continue until both games are qualified.

## Acceptance

- Neither game builds/loads code from the other checkout; demonstrate isolated
  configure/build/launch with the sibling path unavailable to dependency lookup.
- Generic CPU/cook support needs no graphics context or editor/game types.
  Runtime does not link editor/ImGui; neutral model tests require no private assets.
- Engine semantic guards remain strict. Shared Pokemon adapter has no game includes.
- Old cooked resources still load. Decode/recook preserves motion, material tracks,
  billboards, visibility and texture identity; malformed/missing required data
  fails explicitly and failed reloads preserve valid state.
- CPU queries and render poses agree at endpoints/fractional times; no hidden
  clip-name/species guessing in the engine. Unsupported required profiles fail.
- Three native APIs, both relevant hosts, accepted Espeon hind-leg/glint/shiny fur,
  Umbreon dim/bright regular/shiny pulse, unaffected original clips, representative
  recovered materials and field composition are verified at matching times.
- Existing appropriate CPU contracts pass in Debug and RelWithDebInfo; resource
  replacement/unload and instance isolation are covered where changed.

## Execution

The installed orchestration skill pins `astra_flash_builder` to DeepSeek V4.1
Flash. Static doctor reports Astra/xhigh root and the configured worker role;
runtime routing must be established by the actual child result. Root owns this
plan/checkpoint and acceptance; one worker at a time owns assigned code paths.
