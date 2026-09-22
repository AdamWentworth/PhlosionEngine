# Fixed-step kinematic characters

`Phlosion::Collision` provides upright capsule characters through
`CollisionWorld`. Include `engine/collision/CollisionWorld.h`; independent
public value types are in `engine/collision/CharacterController.h`. The
implementation uses pinned Jolt 5.4.0 `CharacterVirtual` collision, sliding,
stair, floor-following and penetration-recovery algorithms. It does not advance
`PhysicsSystem`, create jobs, or require SDL, graphics, input devices or game
content.

```cpp
engine::collision::CollisionWorld world;
// Add static world geometry first.
engine::collision::CharacterId player;
std::string error;
if (world.createCharacter({}, {0, 0.9f, 0}, player, &error)) {
    engine::collision::CharacterState state;
    // Call once per scheduled fixed tick, with half-strength world +X intent.
    if (world.stepCharacter(player, {{0.5f, 0}, false}, state, &error)) {
        // state.center is the geometric capsule center.
    }
}
```

## Ownership and scheduling

The world owns characters and destroys them before its colliders and physics
system. All operations, including reads and destruction, follow the
[collision world's owner-thread rules](COLLISION_WORLD.md). Distinct worlds may
run independently on distinct owner threads. `CharacterId` is an opaque,
comparable nonzero identity, a different C++ type from `ColliderId`. Identities
are never reused across worlds in the linked module. Zero, stale and foreign
character IDs reject safely. `removeCharacter` releases capacity; `clear`
invalidates both character and collider IDs.

Each `stepCharacter` advances exactly one configured tick. There is no elapsed
time argument, wall clock, accumulator or hidden catch-up policy. The host owns
tick scheduling and render interpolation. `CharacterInput::move` maps X/Y to
world X/Z. Finite magnitudes up to one preserve analog strength; larger values
normalize using double-precision arithmetic, including extreme finite floats.
`run` selects run speed. Requested horizontal velocity changes immediately,
including in air. Zero intent is idle. Camera-relative input, acceleration,
jumping and crouching are outside this API.

Gravity uses semi-implicit Euler: subtract `gravity * fixedDeltaSeconds` from
the private downward velocity, clamp to `-terminalFallSpeed`, then move for one
tick. Walkable support resets that accumulator. Collision and slope constraints
resolve requested movement. Returned `velocity` is the **actual center
displacement divided by this tick's dt**, including stairs, floor snap and
penetration recovery. It is zero after creation and successful teleport; reads
and collider removal do not fabricate a new tick. It can exceed configured speed
during positional corrections. The separate gravity accumulator prevents those
corrections from launching a character into the air. Position precision limits
measured velocity precision, especially near coordinate bounds.

## Settings and numeric envelope

All settings and positions must be finite. The supported envelope is narrower
than general collision queries:

| Setting | Default | Supported range |
| --- | --- | --- |
| `radius` | 0.35 | [0.05, 5] |
| `cylinderHalfHeight` | 0.55 | [0, 10]; zero is a sphere |
| `walkSpeed` | 2 | [0, 100] |
| `runSpeed` | 4.5 | [walkSpeed, 100] |
| `gravity` | 9.81 | [0, 100] |
| `terminalFallSpeed` | 50 | [0.01, 100] |
| `maxSlopeDegrees` | 45 | [1, 89] |
| `stepHeight` | 0.3 | [0, 2] |
| `floorSnapDistance` | 0.15 | [0, 2] |
| `fixedDeltaSeconds` | 1/60 | [1/240, 1/15] |
| `layerMask` | all bits | Any 32-bit mask, including zero and bit 31 |
| `ignore` | none | One ignored collider; stale/foreign/zero has no effect |
| World `maxCharacters` | 64 | 1..4096 |

Center coordinates are within [-10,000, 10,000] per component, in the same
world units as collision geometry. Total capsule height is
`2 * (radius + cylinderHalfHeight)`. Jolt disables slope limiting when the
slope cosine is at least 0.9999 (roughly below 0.81 degrees); [1,89] avoids that
special case and near-vertical limits.

Before solver entry, each tick validates its requested endpoint and, when
grounded, potential stair-up and floor-snap endpoints. This conservative check
can reject a tick even if nearby geometry would block it sooner. Results are
checked before publication; recovery outside the envelope restores the preceding
solver state and returns false. Bounds prevent unsupported arithmetic, not
guarantee submillimeter precision everywhere. Prefer human-scale geometry near
the origin when small contact differences matter.

Invalid settings, IDs, intent, destinations or exhausted capacity return false
with an optional diagnostic and preserve outputs and existing state. Success
clears the diagnostic. `removeCharacter` returns false for an unknown ID.
Wrong-thread calls throw `std::logic_error` before mutation. Invalid world
options throw `std::invalid_argument`. Identity exhaustion and allocation or
resource exceptions retain the collision world's documented C++ conventions;
out-of-memory recovery is not promised.

## Contacts and movement policy

Centers match public `Capsule` geometry. Internally the shape offset cancels
Jolt's automatic upward padding offset. The controller keeps 0.005-unit contact
padding and uses 0.0001 collision tolerance; resting centers can be slightly
above exact geometric touching. A supporting-volume plane includes the lower
spherical cap, so a wall touching the middle of the capsule cannot count as a
floor. Meshes collide on both sides with enhanced internal-edge removal.

`CharacterGroundState` reports:

- `Grounded`: supported by a walkable surface within the slope limit.
- `Steep`: supported by a surface too steep to walk; gravity can slide downward.
- `Unsupported`: touching geometry that cannot support the character.
- `InAir`: no supporting/contact surface to report.

`support` is optional copied `ColliderInfo`, also available for steep or
unsupported contact. `groundPoint` and `groundNormal` describe that contact;
when absent they are zero and +Y respectively. No Jolt handle or pointer escapes.
Removing a collider refreshes all character contacts after removing the body
from the broadphase and before destroying it, preventing stale support or
recycled body slots from affecting later ticks.

Movement continuously sweeps the capsule and slides against walls and corners.
Only initially walkable grounded characters may attempt stairs or floor
following. Zero step/snap settings disable their respective algorithms. Capsules
can still roll geometrically over sufficiently small edge contacts, so step
height is a solver policy rather than a promise to reject every vertical edge
below the capsule radius. Stair casts upward, forward and downward preserve
ceiling clearance; steps above the allowed reach block. Steep faces reject
uphill movement. Descending motion can follow nearby ground; an airborne
character cannot snap to a remote floor, and ledge departure falls under gravity.

Sensors never block or support. Layer/ignore filters apply to movement and
teleport. Characters have no inner rigid body or character-vs-character collision
interface: they can share space, and collision-world queries do not see them.
Static sensor queries remain available. Explicit
[character trigger samples](CHARACTER_TRIGGERS.md) provide discrete Enter/Stay/Exit
membership independently of movement; ticks and teleports do not sample or
reset that history automatically.

## Creation, recovery and teleport

Creation refreshes contacts immediately without advancing time or changing the
requested center. A shallow overlapping spawn is allowed so the next ticks can
apply Jolt's bounded recovery. It uses default limits of five collision
iterations, fifteen constraint iterations and 256 collected hits. Recovery is
best effort: deeply embedded or trapped geometry has no promised automatic
escape. Such cases remain finite and can use explicit teleport. The API never
silently chooses a spawn point.

`teleportCharacter` tests the precise geometric capsule at the destination
against blocking shapes/triangles, not an AABB occupancy shortcut. Any contact
penetration greater than 0.0001 units rejects the entire operation, preserving
center, velocity and support. Touching and shallower contacts within that small
numerical tolerance are accepted. Meshes are surfaces rather than closed-volume
occupancy solids, consistent with collision queries.

Success replaces the center, resets reported and gravity velocities to zero,
and refreshes support immediately. No tick elapses. Teleport is the caller's
explicit stuck-player escape and must name a safe destination.

## Verification and scope

`PhlosionCharacterTests`, linked only to `Phlosion::Collision`, registers as
`PhlosionCharacterContracts` with a 120-second timeout and engine /W4 /WX
settings. Ten CPU groups use real Jolt geometry and independently calculated
distance, support, plane, clearance and numeric-bound assertions. They cover
analog movement and fixed-tick batching; gravity/terminal speed/landing;
wall/corner/thin and transformed-mesh sweeps; ramps/steep slopes/ceilings;
stairs/floor following/ledges; recovery/teleport; filters/metadata/removed
support; identity/capacity/threads; malformed/extreme inputs and transactional
boundary rejection; and repeated authored sequences. Existing collision-query
contracts remain separate and unchanged.

Evidence is under ignored `artifacts/m2-b/`, with the implementation report in
`docs/agent-work/cyberpunk-m2-character/REPORT.md`. This CPU foundation does not
qualify a native walkable scene, runtime/editor integration or renderer parity.
Trigger sampling is documented separately above. Camera/actions, host
scheduling/interpolation, moving bodies and platforms, and character-character
response remain later work.
