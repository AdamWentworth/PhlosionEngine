# Local light occlusion

The next accepted milestone is approximately 75% of the intended conventional
lighting foundation, not a numerical Cyberpunk similarity measurement. Work is
ordered as local occlusion, scene-linear HDR/bloom, then wet material review.
Colored indirect light and scene reflections remain distinct later contracts.

## First renderer contract

Reuse the existing opaque, masked, skinned and wind-deformed depth submission.
Add an optional engine-owned local-shadow interface; callers opt in at prewarm.
A 4096-square D32 atlas reserves a 2048-square sun tile and eight 1024-square
local tiles. Existing consumers retain their original single-map allocation.
Local slots identify rectangular lights by index; disabled/invalid/unrendered
entries contribute full visibility. Clear all receiver state each frame/view.
Keep directional and local receiver/caster flags compatible with existing
material contracts. No station coordinates or blockers enter renderer shaders.

Each rectangle uses a perspective depth projection with a virtual apex behind
the luminous plane, clipped at that plane. This is one representative projection,
not exact area-source visibility or physically sized penumbrae. A bounded PCF
filter smooths the depth test; it does not simulate full area shadows. Multiply
each light's diffuse and specular contributions by its own visibility before
combining RGB. Shadow filtering stays inside each atlas tile.

Native implementations must share matrix conventions, bias and coverage. Keep
geometry/pose clocks unchanged across passes, preserve state on pass close, and
avoid per-frame resource allocation. Preserve all pre-existing dirty work using
the snapshot in artifacts/lighting-foundation/pre-local-shadows.

## Acceptance

- CPU contracts for projection, near-plane clipping, invalid inputs, slot limits,
  tile isolation, reset and disabled compatibility.
- Public analytic blocker scene with on/off, cutout, skin and receiver controls;
  independent ray/box oracle and same camera across APIs/hosts.
- Actual station and battle captures across OpenGL/Vulkan/D3D12 and runtime/editor.
- Source-off, dry/wet/day and moving-camera checks; shadow cost paired on/off.
- Inspect shadows visually and retain failures; do not weaken parity thresholds.
- Rebuild/stage all affected plugin hosts together. No commits or asset changes.
