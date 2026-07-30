# Engine Extraction Status

Status: Active
Last updated: 2026-07-30

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
- Pokemon-specific runtime projection and VFX bridges;
- game-specific import/cooking orchestration;
- content manifests and asset IDs;
- all redistributable game content plus local mounts for private asset depots.

The private asset depot owns source dumps, captures, proprietary formats,
derived art payloads, and cooked local resources that are not licensed for
public redistribution.

## Honest compatibility boundaries

The first extraction deliberately preserves working behavior. Several
LGPE-named material contracts and canonical-scene structures still live in the
renderer because Pokemon Autochess previously implemented them there. They are
compatibility code, not the intended generic API surface.

The next cleanup stages are:

1. replace title-specific renderer names with data-driven material families;
2. move import/cook adapters out of runtime rendering;
3. lift common cooked mesh, skeleton, animation, and material resource types
   into the engine;
4. publish an installed CMake package after the add-subdirectory integration
   is proven;
5. add CI configurations that build the engine without Pokemon Autochess.

These boundaries must be improved with parity tests in place. The extraction
does not justify silently changing promoted renderer behavior.
