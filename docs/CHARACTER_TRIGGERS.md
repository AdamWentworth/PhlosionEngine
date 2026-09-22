# Character trigger samples

`CollisionWorld::sampleCharacterTriggers` reports discrete sensor membership
for a world-owned capsule character. Public value types live in
`engine/collision/CharacterTriggers.h`, also included by `CollisionWorld.h`.
Link only `Phlosion::Collision`; no event bus, callback, graphics context or
simulation update is required.

```cpp
engine::collision::CharacterTriggerSample sample;
std::string error;
// Typically called after a successful complete fixed character tick.
if (world.sampleCharacterTriggers(player, sample, {}, &error)) {
    for (const auto& event : sample.events) {
        // Caller-owned gameplay can interpret Enter, Stay or Exit.
        // event.collider contains copied id, userData, layer and sensor fields.
    }
}
```

## Sampling and ordering

Sampling tests the character's current upright geometric capsule against
sensor colliders through the existing precise `overlapCapsule` query. It does
not add character padding or predictive contact distance. Zero cylinder
half-height tests a sphere. Meshes are double-sided triangle surfaces, not
closed-volume occupancy solids. Touching and near-touching follow the collision
query's 0.0001 numerical tolerance.

Each character retains the copied membership from its previous successful
sample. The first successful sample compares against an empty set. Exactly one
event is produced for each ID in the union of previous and current membership:

| Previous sample | Current sample | Event |
| --- | --- | --- |
| Absent | Present | `Enter` |
| Present | Present | `Stay` |
| Present | Absent | `Exit` |
| Absent | Absent | None |

`CharacterTriggerSample::overlaps` contains current membership only. Both
`overlaps` and `events` sort by opaque `ColliderId`, with no duplicates or
separate triangle/subshape events. Event kind does not change ordering. If both
memberships are empty, both vectors are empty; leaving the last sensor still
produces `Exit`. Sampling twice at the same position produces Enter then Stay.
There is no implied elapsed time, frame counter or duration attached to Stay.

All event and overlap metadata is copied. Exit uses the previous snapshot,
including if the sensor has been destroyed. Output vectors belong to the
caller: retaining, editing, clearing or discarding them cannot alter future
samples. No raw Jolt body ID or pointer escapes.

## Filters and lifecycle

`TriggerFilter` has an independent 32-bit `layerMask` (all bits by default) and
one ignored `ColliderId` (none by default). All 32 layers, including bit 31,
are supported. A zero mask is a valid empty current membership. Stale, foreign
and zero ignored IDs have no effect. Movement's layer mask and ignored collider
are unrelated: triggers always use the filter supplied to this sample. Solid
colliders and other characters never participate.

Changing the filter reconciles the old membership with the new result, so
filtering out a formerly overlapping sensor produces Exit. No filter setting
changes character movement or static geometry.

Creation inside sensors does not queue events; the first sample emits Enter.
Movement and teleport neither sample nor reset membership. The next sample
reports the net endpoint result. Teleport out then sample produces Exit;
teleport back then sample produces Enter. Teleporting out and back without
sampling in between produces Stay if membership is unchanged. Failed movement
or teleport preserves history.

Removing a sensor preserves its prior copied membership until the next sample
emits Exit. Reusing the underlying collider slot creates a new opaque identity,
so a replacement produces Exit(old) and Enter(new), never Stay(old). Removing
a character or clearing its world discards its history. No synthetic exit
callback follows destruction. Callers that need a final exit batch can sample
with a zero mask before removing the character.

Sampling changes only that character's retained membership. It does not move
the character, apply gravity, change velocity/support, or mutate world geometry.
Histories are independent across characters and worlds.

## Errors, ownership and memory

The existing [owner-thread and runtime rules](COLLISION_WORLD.md) apply.
Unknown, removed, foreign or zero character IDs return false, set the optional
diagnostic, and preserve output and history. An overlap-query failure also
preserves both. Wrong-thread calls throw `std::logic_error` before mutation;
independent worlds may be used on independent owner threads. Success clears
the diagnostic.

The candidate output and next retained membership are fully allocated before
either is published. Their final moves/swaps do not allocate, preventing a
partial history update if an earlier allocation fails. General allocation and
resource-failure limitations remain those documented for CollisionWorld; this
API does not promise recovery from out-of-memory conditions.

Retained membership contains at most `maxColliders` copied entries per live
character, including stale entries awaiting reconciliation. Current membership
also has that bound; events can contain up to previous plus current membership
(at most twice the collider limit). There is no pending-event queue. These are
count limits, not process-memory budgets: dense worlds, many characters,
temporary query storage and caller-retained output copies all consume memory.

## Discrete endpoints, not swept crossings

A character can move entirely through a thin sensor between samples without
either endpoint overlapping it. That produces no event. Sampling does not
reconstruct the solver's slide/stair path or approximate it with a straight
segment. Teleport across a sensor likewise reports only final membership.

Usually sample after every complete fixed tick and author sensor volumes with
the character's speed and tick spacing in mind. This reduces missed transient
membership but is not a continuous-crossing guarantee. Continuous path events
require a separate path contract and are outside this API.

## Verification and remaining scope

`PhlosionTriggerTests` links only Collision and registers as
`PhlosionTriggerContracts`, with /W4 /WX and a 120-second timeout. Seven groups
check complete expected metadata, ordering and membership against real Jolt
geometry: Enter/Stay/Exit; precise corners, rotated boxes and transformed mesh
sides; independent filters and output ownership; teleport/failure/purity;
removal, capacity and dense old/new membership; invalid IDs and threads; and
discrete crossing plus repeated authored sequences. Existing character movement
and collision query contracts remain separate and unchanged.

Raw local evidence is under ignored `artifacts/m2-c/`; the implementation
report is `docs/agent-work/cyberpunk-m2-triggers/REPORT.md`. This CPU addition
does not qualify native walkable/runtime/editor integration or character visual
parity. Camera/actions, gameplay dispatch, host scheduling, dynamic bodies and
continuous trigger crossings remain outside this pass.
