# M2-B: fixed-step kinematic character movement

Date: 2026-09-22. Baseline: clean `main` at
`b570f4e2d5ee2decf13a1d8ae50526347a4ea681` (accepted M2-A collision).
User requests resuming the readiness roadmap where the collision pass stopped.
This implements the next bounded M2 pass; it does not claim the M2 exit gate.

Outcome: accepted by root Astra on 2026-09-22. See [ACCEPTANCE](ACCEPTANCE.md)
for source/evidence review and the remaining M2 boundaries. The user authorized
committing this accepted work on 2026-09-22; see repository history.

## Architecture and scope

Use the already pinned Jolt 5.4.0 `CharacterVirtual` solver in the existing
opt-in `Phlosion::Collision` library. Keep Phlosion/GLM public types and private
Jolt ownership. Use its collision, stair, floor-following and penetration
recovery algorithms rather than writing a separate sweep/slide solver.
The world owns character instances, avoiding dangling world references and
providing explicit teardown before the physics system. Characters have no
inner rigid body and do not collide with one another or appear in world queries.

References checked against the installed 5.4.0 headers:
- https://github.com/jrouwe/JoltPhysics/blob/v5.4.0/Jolt/Physics/Character/CharacterVirtual.h
- https://github.com/jrouwe/JoltPhysics/blob/v5.4.0/Jolt/Physics/Character/CharacterBase.h

Scope: upright capsules, analog world-XZ walk/run, fixed ticks, gravity and
terminal fall speed, support reporting, wall/corner sliding, walkable slopes,
steep-slope rejection/sliding, configurable steps/floor following, shallow
penetration recovery and explicit safe repositioning. CPU-only, public geometry.
No jump/crouch/root motion, moving bodies/platforms, character-character response,
trigger event state, camera/input devices, host/scene integration, rendering,
game content, dependency upgrades, serialization or editor/plugin ABI changes.

## Public contract

Add `CharacterController.h` for these public value types (exact spelling may
follow nearby conventions) and extend `CollisionWorld` with the methods below.
Keep narrow implementation helpers in `src/engine/collision/`; splitting its
private implementation is allowed if needed, but avoid unrelated refactoring.

- Opaque comparable nonzero `CharacterId`, never reused across worlds in the
  linked module, separate type from ColliderId. Zero/stale/foreign IDs reject
  safely. Add `maxCharacters=64` to world options; supported range 1..4096.
- `CharacterSettings`: radius .35, cylinderHalfHeight .55, walkSpeed 2,
  runSpeed 4.5, gravity 9.81, terminalFallSpeed 50, maxSlopeDegrees 45,
  stepHeight .3, floorSnapDistance .15, fixedDeltaSeconds 1/60,
  layerMask all bits, ignored ColliderId none. Sensors never block/support.
  Validate finite settings and publish explicit safe ranges. Require run speed
  >= walk speed, nonnegative speeds/gravity/step/snap, positive radius/terminal
  speed; cylinder half-height zero is supported. Slope in (0, 90), dt within
  [1/240, 1/15]. Bound geometry/speeds to a practical documented envelope;
  reject overflow/unsupported configurations before entering Jolt.
  Approved implementation refinement: slope settings use [1,89] degrees;
  Jolt disables slope limiting for cosine >= .9999, so accepting angles close
  to zero would misrepresent the setting. Tests must cover the stated bounds.
- `CharacterInput`: `glm::vec2 move` maps to world X/Z and `bool run`.
  Finite analog input preserves magnitude up to one; normalize values above
  unit length without overflow, including extreme finite float inputs.
  Zero is idle. This controls requested horizontal velocity immediately,
  including in air; acceleration and camera-relative intent belong to callers.
- `CharacterState`: center (same geometric center convention as Capsule),
  actual/solver velocity with a documented meaning, ground state
  (Grounded/Steep/Unsupported/InAir), optional copied ColliderInfo for support,
  ground point/normal (defined finite defaults when absent). Grounded alone
  means a walkable supporting surface. State cannot expose stale body handles.
- `bool createCharacter(settings, center, CharacterId& out, string* error)`;
  `bool removeCharacter(id)`; `size_t characterCount() const`;
  `bool characterState(id, CharacterState& out, string* error) const`;
  `bool stepCharacter(id, input, CharacterState& out, string* error)`;
  `bool teleportCharacter(id, center, string* error)`.
  Each step advances exactly one configured tick. No elapsed-time accumulator,
  hidden wall clock, dropped catch-up time or variable dt parameter. Host-owned
  scheduling/interpolation is later work. No PhysicsSystem simulation/jobs.
- Creation may start in shallow penetration so recovery can be exercised;
  document support initialization. Teleport is the explicit stuck-player
  escape: reject genuinely penetrating destinations using precise geometry
  and a stated small tolerance, preserve state on rejection, and on success
  reset velocity and refresh support. Do not silently choose a spawn location.
  Automatic solver recovery is bounded best effort for shallow penetration;
  impossible/trapped geometry must remain finite and explicitly recoverable.
- Floor following only for supported grounded motion; do not snap airborne
  characters to distant floors. A disabled step/snap setting must be honored.
  Reject climbing steep faces or steps above the configured policy (subject
  to documented contact tolerance/capsule geometry). Preserve ceiling clearance.
- All owner-thread/lifetime rules match CollisionWorld. World destruction and
  clear remove characters before colliders; document that clear now invalidates
  both kinds of ID. Removing support must not dereference stale Jolt contacts;
  invalidate or refresh character support/contact state before reuse. Multiple
  independent characters/worlds and independent owner threads remain supported.
- Invalid input/IDs/capacity return false with diagnostic, preserve outputs and
  prior character/world state; success clears diagnostic. Wrong-thread calls
  throw logic_error before mutation. Constructor/allocation exceptions retain
  the existing documented conventions. Clamp/validate arithmetic endpoints
  before solver entry; never publish nonfinite positions/velocities.

## Acceptance and verification

Separate `PhlosionCharacterTests` / `PhlosionCharacterContracts`, linked only
to Collision, inheriting /W4 /WX and a 120-second timeout. Real Jolt/static
geometry, no mocks. Independent analytic/geometric assertions, no golden values
copied from a first run and no thresholds widened simply to get a pass.

Cover these behavior groups:
1. Free movement: walk/run distances, analog/diagonal normalization, idle,
   gravity, terminal fall speed, landing and stable grounding; two render-frame
   batching arrangements of the same fixed ticks have matching final states.
2. Wall slide, corner blocking, thin-wall high-speed sweep, ceiling collision,
   no tunnelling. Authored boxes and meshes including transformed geometry.
3. Walkable ramp ascent/descent, slope limit/steep sliding or blocking,
   step below/above height, disabled stepping, low ceiling and descending
   floor following; ledge departure must fall rather than hover/snap too far.
4. Shallow initial-overlap recovery, trapped state remains finite, successful
   safe teleport, blocked teleport preservation, post-teleport velocity/support.
5. Layers incl bit31/zero mask, ignored collider, nonblocking sensors;
   ground metadata; removed support and removed/reused collider slots safe.
6. Zero/stale/foreign IDs, independent characters/worlds, remove/clear,
   capacity recovery and repeated create/destroy, owner-thread rejection and
   an independent world on another thread. Keep existing query tests passing.
7. NaN/infinity, malformed settings, exact configured bounds, zero-height
   sphere, extreme finite input, numeric-envelope rejection and unchanged
   sentinel outputs/state on invalid operations. Repeated authored sequences.

Retain full raw logs under ignored `artifacts/m2-b/`. Configure the existing
VS2026 build via `cmake -S . -B build-presentation`, preserving enabled targets;
build all Debug targets with /WX, then run all CTest entries. Confirm standalone
character executable linkage has no SDL/graphics imports. Fix failures within
scope and keep negative as well as final evidence. GPU testing is not applicable
to this CPU-only addition; native walkable/runtime/editor coverage remains pending.

## Ownership and workflow

One implementation worker owns `src/engine/collision/**`,
`tests/TestCharacter*.cpp`, `tests/TestCollisionWorld.cpp` if required for clear
semantics, narrow `CMakeLists.txt` integration, `docs/CHARACTER_CONTROLLER.md`,
`docs/COLLISION_WORLD.md`, and this directory's `REPORT.md` plus ignored evidence.
Root owns PLAN/CHECKPOINT/ACCEPTANCE, roadmap/index/verification/boundary/README.
You are not alone in the codebase: preserve other edits and accommodate them.
No recursive agents, commits/staging/push, provider changes, new dependencies,
game edits, rendering edits or weakened existing contracts.

Root is Astra; installed builder pins DeepSeek V4.1 Flash/high. Static doctor
is ready with unchanged configuration hash from prior accepted runs. Prior
verified routing is reused; current inference is verified only by host/provider
metadata, never a self-report. Prior checkpoints record user-authorized Astra
fallback if Flash is unavailable; disclose any necessary fallback.

Worker should finish its implementation/test/debug loop and return one report
ready for root review, with changed files, commands/exits, behavior evidence,
failures fixed, limits and exact resume action if blocked. Up to 120 minutes for
this bounded bundle; checkpoint material contract conflicts instead of expanding
scope. Root reviews the actual diff and evidence, consolidates corrections, then
records acceptance and updates the roadmap. Later M2 work remains trigger events,
camera/actions and native host integration/qualification.
