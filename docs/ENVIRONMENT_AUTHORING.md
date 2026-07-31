# Environment Authoring

Status: Active design and implementation contract
Last updated: 2026-07-31

## Goal

Phlosion Editor must support the same basic scene-building loop expected from
a modern game editor:

1. open a game scene while simulation is stopped;
2. organize its objects in named hierarchy folders;
3. select and transform objects directly in the Scene viewport;
4. create, duplicate, delete, and reparent reusable prefab instances;
5. construct terrain features such as ramps, ledges, and raised platforms;
6. save a small editable project document;
7. cook that document deterministically into runtime `.phscene` and `.phlo`
   resources.

An imported environment remains a source-faithful base. Project-authored
changes compose over that base and do not rewrite the imported asset.

## Document Model

The editor document is not the cooked `.phscene`. It is a project-owned scene
description whose nodes have stable IDs, display names, parent IDs, sibling
order, enabled state, and a set of typed components. Hierarchy folders are
ordinary organizational nodes with no runtime transform or rendering behavior.

The initial component vocabulary is:

- `Transform`: source-local translation, rotation, and scale;
- `PrefabInstance`: reference to a reusable `.phlo` and optional parameter
  overrides;
- `ImportedSourceBinding`: immutable source object identity, source transform,
  provenance hash, and recook guard;
- `TerrainPatch`: authored top surface, material set, UV policy, collision, and
  navigation participation;
- `RaisedPlatform`: editable footprint and elevation with separate top,
  transition stripe, fringe, and cliff-side material roles;
- `LedgeSpline`: path, height, thickness, corner/cap policy, fringe rules,
  collision, and one-way traversal metadata;
- `Ramp`: endpoints or attachment nodes, width, slope, surface and edge
  materials, collision, and navigation links;
- `VegetationScatter`: prefab palette, region, density, exclusions, seed, and
  source behavior bindings;
- `Collision` and `Navigation`: explicit authored overrides or generated
  results;
- `Lighting` and `Atmosphere`: scene-level settings and source-behavior
  bindings.

Components describe intent. Backend render meshes, physics geometry, navigation
links, fringe joins, and material batches are generated cook outputs rather
than hand-maintained duplicate data.

## Imported Environments

An imported scene exposes the finest stable identities supported by its
importer:

- independent prefab placements are directly editable;
- repeated source records should be split into stable individual instances
  when the source evidence supports that split;
- a source mesh that cannot yet be decomposed is exposed honestly as an
  editable source mesh group;
- every override records the expected source transform or provenance hash and
  fails loudly after an incompatible recook.

For Pokemon Autochess Route 1, the adapter exposes canonical source mesh
groups, encounter-grass records, decoded vegetation placements, and 47
independent tree instances. The tree split is derived from connected trunk
topology and matching contiguous source material blocks; it does not guess
centres or replace the source geometry with a generic tree. Source meshes that
do not yet have an equally strong decomposition proof remain clearly labelled
as groups.

## Editing Transactions

Viewport manipulation has two phases:

- **Preview** updates only the affected transform and render instances on every
  pointer movement. It must not rebuild projected shadows, material catalogs,
  statistics, collision, or navigation.
- **Commit** runs once when the pointer is released. It validates the document,
  rebuilds affected derived data, records one undo command, and atomically
  saves the project document. Escape restores the pre-drag state.

Inspector fields, gizmos, duplication, reparenting, and local-agent commands
must all use the same typed transaction API. A tool must never mutate a private
runtime representation that cannot be undone or serialized.

The first command implementation is now active in Pokemon Autochess Route 1:

- Duplicate creates a project-owned prefab instance bound to an immutable
  imported prototype identity;
- Delete suppresses an imported source object or removes an authored instance;
- rename and hierarchy-folder changes are persistent metadata edits;
- transform, duplicate, delete, rename, and reparent commits share one bounded
  undo/redo history and atomic-save path;
- hierarchy labels use natural numeric ordering, so numbered objects are shown
  as `1, 2, ... 10, 11`.

Route 1 now persists these records in the Engine-owned
`phlosion_authored_scene` schema version 1. Its tracked
`scenes/route1.scene.json` composes over the immutable cooked Route 1
environment. `phlosion.project.json` declares that document with
`authored_scene_path`; the editor passes the resolved path through the generic
project-plugin scene context.

The implemented version-1 component vocabulary is deliberately narrow:

- folder nodes have no components;
- object nodes have `transform` plus exactly one of
  `imported_source_binding` or `prefab_instance`;
- imported bindings include immutable target kind, logical name, record index,
  and expected source transform;
- prefab bindings include prototype node ID, `.phlo` asset ID, and creation
  transform.

The Engine parser rejects duplicate IDs, invalid transforms, missing or
non-folder parents, hierarchy cycles, missing prefab prototypes, and malformed
component combinations. Source-specific adapters add stronger rules such as
Route 1's exact stable-ID derivation and `Environment/` hierarchy boundary.

## Parametric Terrain Workflow

Raised platforms, ledges, and ramps should be authored as connected parametric
components rather than disconnected raw meshes:

1. draw or edit a platform footprint;
2. set its elevation and material roles;
3. attach ledge splines to selected perimeter segments;
4. attach a ramp between compatible elevations;
5. regenerate top surfaces, transition stripes, overhanging fringe, cliff
   sides, collision, and navigation as one transaction;
6. validate joins, self-intersections, slope limits, and orphaned traversal
   links before saving.

Source-specific adapters may supply Game Freak-compatible profile templates,
material families, fringe placement, and source behavior. The generic Engine
components do not hard-code Pokemon rules.

## Cook Boundary

The authored document and referenced `.phlo` resources are tracked game
inputs. Forge validates and hash-tracks the authored document today while the
game/editor adapter composes it over the immutable `.phscene` at load time.
The later cook step will resolve that same document into a deterministic
runtime `.phscene`, derived collision/navigation data, and optional packaged
`.phv` payloads. That flattening is an optimization and deployment boundary;
it must not create a second authoring schema.

Private source dumps remain outside public repositories. Their imported/cooked
outputs follow the owning game's asset policy; provenance hashes and conversion
recipes remain tracked.

## Delivery Sequence

1. Smooth transform preview and organized hierarchy folders.
2. Expose every stable imported prefab placement and honest source mesh group.
3. Split the remaining evidence-backed repeated prop or foliage batches into
   individual stable instances. Route 1's 47 trees satisfy this step.
4. **Complete:** add create-from-selected/duplicate, delete, rename, and
   reparent commands with undo/redo.
5. **Complete:** persist a generic project-owned scene document rather than a
   Route 1-only delta schema.
6. Add `RaisedPlatform`, `LedgeSpline`, and `Ramp` creation tools.
7. Generate collision/navigation and cook the authored composition.
8. Qualify editing, undo, recook stability, and renderer parity with Pokemon
   Autochess before applying the workflow to a second game.
