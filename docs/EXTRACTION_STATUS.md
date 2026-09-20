# Engine Extraction Status

Status: Active
Last updated: 2026-09-19

Phlosion Engine now has an independent repository and build graph. Its source
history was retained from the `src/engine` subtree of Pokemon Autochess.

## Repository boundary

Phlosion Engine owns:

- core services, ECS, events, and runtime hosting;
- SDL platform and input integration;
- OpenGL, Vulkan, and Direct3D 12 rendering backends;
- model, texture, animation, and scene rendering infrastructure;
- PHRC, `.phlo`, and `.phscene` runtime container support;
- common UI and reusable VFX preview infrastructure;
- engine-owned shaders and engine contract tests.

Pokemon Autochess owns:

- game rules, simulation, sessions, and board behavior;
- board rendering, battle feed, health-bar semantics, and game UI;
- shop/round/combat profiling, board flags, and move-specific VFX diagnostics;
- Pokemon-specific runtime projection and VFX bridges;
- game-specific import/cooking orchestration;
- content manifests and asset IDs;
- all redistributable game content plus local mounts for private asset depots.

The private asset depot owns source dumps, captures, proprietary formats,
derived art payloads, and cooked local resources that are not licensed for
public redistribution.

## Completed material and presentation separation

The September cleanup moved the remaining Autochess renderers and diagnostics
out of the engine. Recovered field, character, eye and layered-fire material
implementations also moved to the game. There is no longer a five-file shader
allowlist or a built-in game-material evaluator in the standalone engine.

Projects load their own versioned [world material profiles](MATERIAL_PROFILES.md).
An empty profile selects generic engine defaults. Source decoders, canonical
scene adaptation, specialized editor semantics and game presentation remain
game/research responsibilities. [Engine boundaries](ENGINE_BOUNDARIES.md)
describes the enforced ownership contract.

## Current delivery limits

- The engine has an independent Windows build and asset-independent contract
  suite. [Verification](VERIFICATION.md) records what CI and local checks cover.
- The public engine/editor build requires no private packages or game assets.
  A consuming project may impose additional content or build requirements.
- An installed CMake package and a stable SDK/API freeze remain future work;
  source-checkout integration is the supported build boundary today.
- The original project's game art is not a reusable engine asset bundle. The
  README screenshot is a labelled example of a separate consumer.
- No new Cyberpunk feature milestone or general renderer redesign is scheduled
  by this presentation cleanup. No repository code licence has been selected.

Renderer changes still require native OpenGL, Vulkan and Direct3D 12 parity
checks, including affected editor surfaces. Separation is not a claim that every
model, project, operating system or GPU has been qualified.
