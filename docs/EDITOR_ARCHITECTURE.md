# Phlosion Editor Architecture

Status: Active
Last updated: 2026-07-31

## Decision

Phlosion uses Dear ImGui docking as the first native editor shell. Phlosion
owns the editor document model, commands, transactions, undo/redo, project and
asset schemas, inspectors, viewport behavior, visual style, and specialized
authoring widgets.

Dear ImGui is a replaceable presentation dependency. Engine and game runtime
targets do not expose Dear ImGui types in their public contracts. The editor
UI is enabled with `PHLOSION_BUILD_EDITOR`.

Building a complete retained-mode GUI and docking toolkit is not a current
engine milestone. A future custom shell remains feasible because editor
semantics live below the UI layer.

## Ownership

- `PhlosionEngine` owns `Phlosion::EditorCore`, `Phlosion::Editor`, project
  loading, the `PhlosionEditor` executable and project browser, common panels,
  editor transactions, viewport integration, and the local-agent tool
  contract.
- `PhlosionVFX` owns VFX-specific authoring panels and effect schemas.
- A game owns its `phlosion.project.json`, game-specific inspectors, startup
  scene choice, project-owned scene or prefab overrides, and an optional
  generated editor-project plugin.
- Source assets, model weights, generated candidates, and private evidence
  remain outside public code repositories.

The editor initially builds in the Engine repository. It should not become a
separate repository until it consumes a stable public Engine SDK without
reaching into Engine internals.

## Project Contract

`phlosion.project.json` is a portable, tracked project descriptor. It names:

- the project and stable project id;
- relative cooked-content mounts;
- the startup game scene id;
- a catalog of reusable environment backdrops, including their cooked,
  runtime-generated, or placeholder representation;
- a catalog of game scenes that reference those backdrops and optionally name
  a runtime script and implementation status;
- an optional portable editor-plugin library name and configuration-relative
  generated output directory;
- optional named play configurations with a project-relative executable,
  working directory, arguments, and environment overrides;
- the optional environment variable that locates a private asset depot.

Machine-specific absolute paths do not belong in the descriptor. Asset
payloads remain ignored and are restored through the project's existing asset
workflow.

`PhlosionEditor` always starts from the Engine build or installation. Opening a
project loads its optional editor-project plugin in-process through the
versioned `IEditorProjectRuntime` contract. That plugin is a transitional and
extensible adapter: it owns game-specific scene loading and future
game-specific inspectors, while the Engine owns the process, window, camera,
render loop, and shell. As generic `.phscene` loading absorbs the remaining
Route 1-specific code, Pokemon Autochess's basic viewing path should require
less plugin code.

Recent projects, panel layout, and outer-window placement are machine-local
editor state under the operating system's application-data directory. They do
not belong to either the Engine or game repository. Remembering the outer
window rectangle is intentional support for multi-monitor workstations.

## Scenes, Hierarchy, and Game Preview

These editor concepts are deliberately separate:

- **Scenes** is the catalog of game location/state containers. A scene
  references a reusable environment backdrop and may identify a runtime
  script. Opening one changes the active Scene view, hierarchy, Inspector
  context, runtime scene adapter, and associated Game previews. Unfinished
  scenes stay in the catalog with explicit status rather than disappearing.
- **Scene Hierarchy** is the object/component tree inside the open game scene.
  Its first dependency is the environment backdrop. The current read-only
  adapter exposes source-backed groups for cooked environments and explicit
  runtime-preview status for backdrops that are still runtime-generated;
  stable object identities and component editing belong to M1.
- **Inspector** displays the properties of the selected hierarchy object,
  scene, or asset. It is read-only until the command/transaction layer can
  make edits safely.
- **Assets** enumerates cooked resources from the project's content mounts.
  `.phscene` worlds and `.phlo` prefabs are the top-level entries. Meshes,
  materials, animations, skeletons, and textures owned by a prefab are
  dependencies of that prefab, not unrelated peer assets.
- **Scene view** renders the active scene's inspectable environment backdrop
  with editor camera and simulation controls. Scenes may share this asset; for
  example, Route 1 and Route 1.5 can remain distinct game scenes while both
  reference the same Route 1 environment.
- **Game view** renders the project's real runtime state over its loaded
  assets.

Opening a project enters Edit mode at simulation time zero. The scene remains
rendered and camera navigation remains available, but time-dependent material,
wind, lighting, and vegetation animation do not advance. Play starts the scene
simulation, Pause freezes it at the current time, Step advances one fixed
60 Hz frame, and Stop returns to time zero.

Game scenes are not falsely required to be `.phscene` files. `.phscene` is one
cooked representation of an environment dependency, not the identity of the
game scene that uses it. A project plugin exposes named previews such as a
frontend screen, route script, or world snapshot. A preview may name the game
scene that owns it; the Game Preview panel presents those under the active
scene and keeps application-level previews separate. The editor
initializes one project runtime when the project opens, renders it into an
Engine-owned offscreen surface, and presents it in the central Game view.
Choosing another preview mutates or restores that warm runtime; it does not
launch another executable or repeat asset prewarming.

Boot is application lifecycle and loading presentation, not a route scene. A
project may expose a replayable Boot preview without reinitializing the
runtime. Frontend screens are UI/runtime states. Each route remains a game
scene, but multiple route scenes may share an environment backdrop. Classic
versus Adventure and Planning versus Battle are session configuration and
phase layered over that scene, not duplicate scenes or environments.

The initial project open may still perform expensive CPU/GPU asset prewarming.
"Warm switching" means subsequent preview changes reuse those resources; it
does not imply that a newly opened process can skip its first initialization.

## Cooked Prefab Inspection

Selecting a previewable `.phlo` replaces generic metadata in the Inspector with
an embedded, read-only 3D viewer. The project plugin decodes the selected
cooked prefab directly; the editor does not silently substitute its source
model.

The viewer provides:

- right-mouse orbit, middle-mouse pan, wheel zoom, and reset view;
- animation selection, pause/play, restart, playback speed, and timeline
  scrubbing;
- mesh, material, texture, wireframe, and skeleton inspection modes;
- cooked vertex, triangle, material, texture, bone, and clip counts.

The preview uses an Engine-owned offscreen surface and the editor host's active
renderer. Scene, Game, and prefab surfaces remain isolated render targets while
sharing one device, command stream, shader/material implementation, and
backend selection. The project plugin supplies content and animation state; it
does not create a hidden renderer of its own. This is an inspection tool, not a
second model editor: source authoring remains in Blender and later asset
changes go through Forge.

## First Vertical Slice

The first accepted editor host must:

1. open a tracked project descriptor;
2. mount the project's cooked Route 1 `.phscene` with no source-cache fallback;
3. render through the same Engine world renderer used by the game;
4. provide a docked hierarchy, inspector, assets view, and console;
5. preserve camera navigation in the uncovered scene viewport;
6. expose the mounted scene's actual runtime statistics;
7. produce an automated screenshot and pass Engine and game tests.

This slice is intentionally read-only. The next slice adds stable selections,
command transactions, undo/redo, and project-owned board-layout editing.

## Local AI Boundary

Local AI operates out of process and calls the same typed editor commands as a
human. A model may query state and propose a transaction. It may not silently
rewrite source files or private assets.

Every accepted AI transaction records its inputs, model identity, tool calls,
asset hashes, generated outputs, validation results, and undo operation.
`llama-server` is the initial language-model provider. Generative workflows
may later use local ComfyUI and Blender services behind provider-neutral
adapters.

## Roadmap

### M0: Native viewer

Open a tracked project, mount a cooked `.phscene`, render it through the shared
world renderer, navigate the camera, and expose read-only hierarchy, inspector,
assets, and console panels. Pokemon Autochess Route 1 is the qualification
scene.

### M1: Safe scene editing

Introduce stable selections, editor documents, typed commands, transactions,
undo/redo, transform gizmos, and saveable project-owned overrides. Qualify by
editing only the Pokemon Autochess board-layout delta while canonical Route 1
remains byte-identical.

The first M1 interaction slice is active for project adapters that expose
source-backed layout records. In `EDIT` mode the Scene viewport projects
pickable object markers and source-local Move, Rotate, and Scale gizmos.
Dragging applies an in-memory preview every frame, releasing autosaves through
the project adapter, and Escape restores the pre-drag layout. The Inspector
uses the same preview/commit path. Canonical cooked scenes remain read-only.
The Route 1 adapter now presents stable source records in semantic,
collapsible hierarchy folders and uses a lightweight preview path so drag
frames do not rebuild projected shadows, material catalogs, or runtime
statistics. Route 1's 47 source-baked trees are now decomposed into stable
individual placements through topology- and vertex-block evidence. Command
history, undo/redo, decomposition of the remaining qualified repeated source
batches, and creation of new scene components remain later M1 work.

The generic authored-environment component model and the path from imported
source groups to prefab instances, ramps, ledges, and raised platforms are
defined in `ENVIRONMENT_AUTHORING.md`.

### M2: Forge and asset registry

Expose import state, dependencies, deterministic cook results, thumbnails,
provenance, validation, hot reload, and visual baselines through reusable
Engine services. Source-specific import recipes remain game or plugin owned.

### M3: Vault packaging

Build `.phv` vaults and prove that loose and vault-mounted resources produce
identical asset hashes and fixed editor/game captures.

### M4: Local agent

Run a local language model out of process. The agent queries editor state and
submits typed transaction proposals through the same command bus used by human
editing. All changes remain inspectable, rejectable, and undoable.

### M5: Local asset studio

Add provider-neutral adapters for local ComfyUI, Blender, audio, and later 3D
generation workflows. Generated candidates retain model, workflow, prompt,
seed, input, license, and output provenance before Forge accepts them.

### M6: Generic-engine proof

Open a small original non-Pokemon project and complete the same import, edit,
cook, package, and play loop without adding a game-specific runtime format or
forking the editor.

M0 is complete with an Engine-owned project browser, recent-project workflow,
dynamic game-project adapter, an embedded persistent Game runtime surface, and
shared Direct3D 12, Vulkan, and OpenGL editor presentation. Windows `Auto` uses
Direct3D 12; Vulkan is the explicit modern cross-platform option, and OpenGL
is the broad compatibility option. All three use the same Engine-owned editor
surface abstraction, so backend-specific Dear ImGui types do not leak into
games. M1 is the current active milestone.

The Direct3D 12 editor path owns a separate shader-visible descriptor heap for
Dear ImGui and embedded editor-surface textures. It rebinds that heap after
world rendering and before every ImGui submission because world rendering may
bind the renderer-owned material heap earlier in the same command list.

The Vulkan editor path exposes only a native device/queue/render-pass context
to the Engine shell. Scene, Game, and prefab previews use double-buffered
renderer-owned color and depth targets, the same linear scene-color composite
as the game, and per-frame sampled descriptors registered with Dear ImGui.
Swapchain recreation rebuilds those targets without changing the editor
surface handles held by the shell.

The M0 SDL2 bridge currently owns keyboard, mouse, text, focus, and DPI input.
Clipboard, cursor-shape, accessibility, controller navigation, and detached
platform windows remain explicit shell work rather than implicit support.

## Build Tree Convention

Source repositories remain siblings. CMake `FetchContent` clones are disposable
build artifacts, never submodules or source ownership boundaries. Remote-pin
qualification builds should use a binary directory outside the source tree,
for example:

```powershell
cmake -S D:\Projects\PokemonAutochess `
  -B D:\Build\PokemonAutochess\fetch-deps `
  -DPHLOSION_ENGINE_SOURCE_DIR:PATH= `
  -DPHLOSION_VFX_SOURCE_DIR:PATH=
```
