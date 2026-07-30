# Phlosion Editor Architecture

Status: Active
Last updated: 2026-07-30

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
  loading, common panels, editor transactions, viewport integration, and the
  local-agent tool contract.
- `PhlosionVFX` owns VFX-specific authoring panels and effect schemas.
- A game owns its `phlosion.project.json`, game-specific inspectors, startup
  scene choice, and project-owned scene or prefab overrides.
- Source assets, model weights, generated candidates, and private evidence
  remain outside public code repositories.

The editor initially builds in the Engine repository. It should not become a
separate repository until it consumes a stable public Engine SDK without
reaching into Engine internals.

## Project Contract

`phlosion.project.json` is a portable, tracked project descriptor. It names:

- the project and stable project id;
- relative cooked-content mounts;
- the startup scene asset id and path;
- the optional environment variable that locates a private asset depot.

Machine-specific absolute paths do not belong in the descriptor. Asset
payloads remain ignored and are restored through the project's existing asset
workflow.

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

The current active milestone is M0. The first captured host is intentionally an
OpenGL editor viewport; Vulkan and D3D12 editor presentation follow through an
Engine-owned UI submission abstraction rather than leaking backend-specific
Dear ImGui types into games.

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
