# Canopy wet edge and editor battle entry — 2026-11-06

The user confirmed the camera-following circle and movement skips were resolved,
but the canopy rectangle remained and entering battle closed the editor. This
follow-up preserves the earlier GI, shadow-bias and scene-cache changes.

## Confirmed failures

- The previous dry/orbit check was insufficient for the rectangle. The same
  canopy camera with accumulated rain shows a straight wet/dry boundary;
  the dry control does not. The active wet-material path consumed the rain
  occlusion map without the older authored shelter-edge blend.
- The full editor opening (arrival enabled) first failed D3D12's strict shader
  lookup: screen-feedback program bb696f7af37efefd was absent from the prepared
  bundle. Direct battle loads and skipped arrivals did not exercise it.
- Vulkan then exposed the engine's global once-per-frame wind admission rule.
  A transition renders the outgoing field snapshot and incoming battle in one
  backend frame. They already own independent wind fields, but the global guard
  incorrectly rejected the second scene.

## Changes

- Wind simulation admission is once per resource scope per frame on both APIs.
  Repeating one scope remains rejected; outgoing and incoming histories stay
  separate. Native tests verify both same-frame fields and retained history.
- Open-canopy ground wetness blends within 2.5 m of the authored roof edge, using
  a small fixed world-space variation. Ground well inside stays sheltered;
  exposed terrain away from the edge keeps its rain-map response. Closed-room
  rejection, physical splash shelter, surface-specific impacts and puddle rings
  are unchanged. This adds no texture reads or moving CPU work.
- The prepared package includes full editor transition programs. TEST_PLAN now
  explicitly requires arrival through the command menu and return, with strict
  shader lookup, rather than accepting direct-battle-only qualification.

## Candidate validation

gpu-worker Windows, EVGA GPU1, optimized RelWithDebInfo, validation enabled.

- Native wind and wet-surface suites pass on D3D12 and Vulkan. The new roof-edge
  readback has 47 gradient pixels and maximum adjacent linear-albedo change
  0.0155801 on D3D12 (limit 0.04).
- Both real editor openings reach battle tick 1484 (arrival duration 1445), all
  four Pokemon settled and visible, with the command menu present.
- Wet-night Low/Medium/High/Ultra runs complete on both APIs without validation
  errors. Dry baseline and identical-camera wet controls isolate the rectangle.
  The median four-pixel change across the former sharp front edge drops from
  8.83 to 3.67 display-code values; this is local edge evidence, not a global
  image-quality or performance score.
- A worker-only D3D12 Medium cached-PSO anomaly initially made exposed ground
  dark and glossy. Identical shader bytecode rendered correctly with a separate
  cache and with PSO-cache reads disabled. The old worker PSO directory is
  quarantined in task evidence; rebuilt and warmed caches render correctly.
  No global cache disabling or quality reduction is shipped. The exact reason
  that old driver cache produced different shading is not established.
- CPU wind/wet contracts pass. Final locked-build/publication evidence is kept
  with the delivered build, including strict prepared-shader transition checks.

Evidence root: WORKER_EVIDENCE_ROOT/phlosion-scene-followup-20261106 on gpu-worker;
controller copy: PROJECTS_ROOT/Games/PokemonCyberpunk/.phlosion/agent-work/scene-followup-20261106.
These are correctness/visual checks, not clean GPU timing measurements.
