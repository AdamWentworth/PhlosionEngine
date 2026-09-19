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

## Honest compatibility boundaries

The remaining recovered LGPE field-material GPU evaluator is isolated to the
five files allowlisted by `PhlosionEngineSemanticBoundary`. Source decoders,
canonical scene adaptation, editor semantics, and game presentation belong to
the game or private research workspace. See [Engine boundaries](ENGINE_BOUNDARIES.md)
for the exact ownership contract.

The September cleanup moves the remaining Autochess renderers and diagnostics
out of the engine while retaining their behavior. It is a repository cleanup
checkpoint, not an API freeze or a commitment to add Cyberpunk features.

Replacing the remaining material compatibility profile with a project-loadable
extension and publishing an installed CMake package are separate future work.
Renderer changes still require native OpenGL, Vulkan, and D3D12 parity checks.
