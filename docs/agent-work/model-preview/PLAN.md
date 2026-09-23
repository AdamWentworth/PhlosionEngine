# Shared cooked-model Inspector

Status: in progress (2026-09-23)

The first engine slice now loads and validates the PHLO root and its four
typed child containers, including dependency hashes, safe paths, schema,
manifest counts, and the binary animation clip names/durations. The synthetic
contract and all four local Cyberpunk packages pass. Typed geometry, animation
keyframes, material decoding, and the rendered viewer remain in progress; the
editor still reports preview unavailable for these models.

## Decision

Phlosion Editor owns the reusable model Inspector: asset load state, camera,
animation transport, graphics-quality/material-debug options, lighting presets,
wireframe and skeleton overlays, and native renderer parity. Projects own model
placement, catalogs, gameplay, and source-specific material interpretation.
The Inspector must not depend on PokemonAutochess code or on a private asset
depot. Its model input is a versioned, validated cooked PHLO model contract.

The host already owns the controls and render surface in `EditorShell` and
`PhlosionEditorMain`. PokemonAutochess's `PokemonPrefabPreview` owns the concrete
model path today. It depends on the game-owned PHLO v3 loader, `MeshData`,
character material modes, pose evaluation, and projected-world presentation.
The engine's public `PhlosionMeshAsset` is a different static v1 schema and
cannot decode the Cyberpunk PHLO packages. Copying the Autochess viewer into
the engine would create an invalid game dependency.

## Interfaces and failure behavior

- The engine gains a source-neutral cooked-model reader for PHLO model prefabs
  and their mesh, skeleton, animation, material, and texture dependencies. It
  preserves authored values and validates container/dependency integrity.
- The engine viewer consumes that reader and the existing editor preview
  options. It can be used by a project plugin without adding game headers.
- Material families that require a game's source-specific shader rules use an
  explicit project adapter. Unsupported material semantics must be reported;
  the viewer must not silently substitute a misleading appearance.
- Selection remains cancelable/reselectable. Missing or invalid resources show
  a terminal error rather than an endless loading state. No private content is
  copied into Git or a test fixture.

## Phases

1. **Cooked model contract.** Extract a reusable PHLO model decoder and data
   representation into Phlosion Engine. Validate synthetic mesh/skin/animation/
   material fixtures and read all four Cyberpunk model packages without game
   libraries. Preserve the existing Autochess decoder and behavior until the
   new contract is accepted. This phase has no visual parity claim.
2. **Engine model viewer.** Implement render, animation, camera, material-debug,
   lighting, wireframe, and skeleton behavior over the shared contract. Connect
   the temporary fixture plugin to the engine viewer so Cyberpunk's regular and
   shiny Espeon/Umbreon open in the Inspector. Qualify expected content at
   matching frames on OpenGL, Vulkan, and D3D12.
3. **Autochess adoption.** Route generic preview behavior through the engine
   viewer, retain source-specific material/visibility hooks in Autochess, and
   compare established Pokémon preview cases before retiring duplicated code.

## Acceptance

- Cyberpunk's four cooked models load promptly, list 77 clips each, and expose
  a working animation/lighting Inspector on all three native renderers.
- Regular and shiny material identity, eyes, visibility, selected animation,
  and lighting match expected content at matched frames; unsupported material
  modes remain visible as an explicit limitation until adapted.
- The Outskirt battle and Autochess editor paths retain their existing visuals
  and controls. Relevant CPU contracts and native visual matrices pass.
