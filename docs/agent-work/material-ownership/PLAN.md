# Material ownership cleanup

## Contract

Move general rendering out of PokemonModelSupport before the dedicated lighting
pass. PhlosionEngine owns ordinary PBR lighting/weather, normal/texture sampling,
review/color output and source-neutral material assembly. PokemonModelSupport
retains import/pose compatibility, material IDs and packing, source-stage behavior,
eyes, facial overlays, skin and layered effects. Games own field programs and
authored scene choices. Preserve appearance, material IDs, bindings and ABIs.

Baseline: engine `69a2fa2`, PokemonModelSupport `efe0dfd`, Cyberpunk `668c3a4`,
Autochess `b26669f`. All four trees were clean. Existing authorization permits
current-model implementation after the unavailable Flash worker; no worker owns
these paths. No new lighting feature, asset export, gameplay change or publishing.

## Implementation

1. Retain source and composed-program baselines in ignored artifacts. Extract
   ordinary sampling, PBR and final world output into engine shader fragments.
   Source-specific adjustments remain adapter fragments, with explicit neutral
   defaults in the engine. Preserve expanded shader tokens and source behavior.
2. Move material assembly/compilation to engine CMake. Both games call the engine
   helper with the optional adapter source directory and their own field source.
   Resolve only explicit engine/material fragment includes; reject missing,
   escaping or cyclic references. Track every input for regeneration.
3. Update ownership documentation and meaningful composition/boundary contracts.
   Keep source-specific dispatch in the adapter; it calls general engine shading.

## Acceptance

- General lighting/weather/color implementations exist in engine-owned files;
  engine fragments contain no Pokemon identities or recovered source formulas.
- Adapter-specific eye/coat behavior remains outside the engine. Existing source
  licensing exclusions follow moved material; do not relabel recovered formulas.
- Composed shader token streams match the baseline for both games (ignoring only
  comments/whitespace/include file names after expansion), or any discrepancy is
  investigated and independently qualified. No baseline update to hide differences.
- Engine composition handles default/override includes and fails invalid inputs.
  Adapter material and dependency boundaries pass. Both games build against the
  new assembly path with tracked dependencies and generated outputs left ignored.
- Native world/character coverage on OpenGL, Vulkan and D3D12, including editor:
  use Cyberpunk canopy field/battle captures and Autochess material matrices with
  existing content/parity limits. Relevant CPU contracts pass.

Use the existing renderer and game verifiers; retain commands, logs and outcomes
in a checkpoint. Commit/push are not part of this refactor request.
