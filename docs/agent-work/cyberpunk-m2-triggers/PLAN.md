# M2-C: character trigger events

Date: 2026-09-22. Baseline: clean main
`21672db1fef0a538db234b9c75386541d9af8aca` (accepted M2-B, now committed).
User requested committing M2-B and proceeding to the next pass. This is the
checkpoint's next CPU trigger-state dependency; camera/actions and native host
qualification follow. Commit authorization was for existing M2-B, not an
automatic commit or push of this new pass.

Outcome: accepted by root Astra on 2026-09-22. See [ACCEPTANCE](ACCEPTANCE.md)
for patch/evidence review and remaining boundaries. The user subsequently
authorized committing M2-C on 2026-09-22; see repository history.

## Design and contract

Extend the opt-in Collision world with explicit, owner-thread character trigger
sampling. Reuse precise capsule overlaps from M2-A and owned character state
from M2-B. No new solver, callbacks, threads, clocks or global event bus.
Each character retains only its previous successful sample's sensor membership;
callers choose when to sample, typically once after each complete fixed tick.
This avoids a hidden event queue or coupling movement failure to event delivery.

Public types in `src/engine/collision/CharacterTriggers.h` (or equally narrow
existing header) and a method on CollisionWorld:

```cpp
struct TriggerFilter {
    std::uint32_t layerMask = 0xffffffffu;
    ColliderId ignore;
};
enum class TriggerEventKind { Enter, Stay, Exit };
struct TriggerEvent {
    TriggerEventKind kind;
    ColliderInfo collider;
};
struct CharacterTriggerSample {
    std::vector<ColliderInfo> overlaps;
    std::vector<TriggerEvent> events;
};
bool sampleCharacterTriggers(CharacterId id, CharacterTriggerSample& out,
                             const TriggerFilter& filter = {},
                             std::string* error = nullptr);
```

Exact names may follow repository conventions while retaining this contract:

- Query the actual upright geometric capsule at its current center, using the
  precise existing overlap query and sensor-only policy. No predictive contact
  expansion or controller padding. Triangle meshes remain double-sided surfaces,
  not closed-volume solid tests. Touching uses existing collision tolerances.
- Trigger filters are independent of movement's layer mask/ignored collider.
  Support all 32 layer bits, zero mask, one ignored collider; stale/foreign/zero
  ignored IDs have no effect. Never emit nonsensor colliders or characters.
- First successful sample emits Enter for each overlapping sensor. Later calls
  emit exactly one event per ID in the union of previous/current membership:
  Enter if newly present, Stay if still present, Exit if no longer present.
  `overlaps` is current membership only. Both vectors sort by opaque ColliderId;
  no duplicate subshape events or dependence on narrow-phase contact order.
  With no previous or current membership, success publishes empty vectors and
  clears error; leaving the last sensor instead publishes its Exit event.
- Events and overlaps copy full metadata (id/userData/layer/sensor). Exit uses
  the prior snapshot, including when the sensor has been removed. Output memory
  is caller-owned and independent of retained history. No raw Jolt IDs/pointers.
- A filter change reconciles membership on the next call (including Exits).
  Two calls without movement produce Enter then Stay, not a second Enter.
  Sampling has no implied time step or frame counter; it does not move a
  character, apply gravity, alter velocity/support, or mutate static geometry.
- Movement/teleport do not automatically sample or reset membership. The next
  call observes the net current result, so teleport out/in yields Exit/Enter.
  Failed movement/teleport preserves prior history. Creation inside a sensor
  does not queue events until sampled. Removing sensors leaves previous copied
  memberships until the next sample emits Exit; ID/slot reuse cannot become Stay.
- Character removal and world clear discard that character's history; there
  are no synthetic exit callbacks after the character ceases to exist. Callers
  needing an explicit exit batch can sample with a zero mask before destruction.
  Stale/foreign/zero character IDs fail without affecting any history/output.
  Multiple characters retain independent histories; separate worlds/threads
  follow existing owner-thread rules. Destruction order remains unchanged.
- Invalid IDs/query failure return false with a diagnostic and preserve out and
  all retained state. Wrong-thread calls throw logic_error before mutation.
  Success clears the diagnostic. Build both candidate output and next retained
  membership before publication to avoid partial history updates on allocation
  failure; retain existing documented allocation/resource exception limitations.
- History is bounded by the configured collider count per live character; no
  unbounded pending-event queue. A sample can contain up to previous+current
  membership in its events. Explain count limits versus process memory budgets.

Sampling is deliberately discrete. It reports only endpoint membership; moving
entirely through a thin sensor between samples is not a swept crossing event.
Document this clearly with a test and guidance to sample at fixed ticks / author
appropriate sensor sizes. Do not pretend to reconstruct the character solver's
slide/stair path from a straight endpoint segment. Continuous crossing events
would need a separately designed path contract; they are outside this pass.

## Non-goals and ownership

No gameplay rules, NPC interaction, input/camera, event dispatch framework,
serialized scene format, editor/plugin ABI, rendering, dynamic bodies,
character-character response, dependency upgrades or game repository changes.
Keep `Phlosion::Collision` opt-in and renderer/platform independent.

One implementation worker owns `src/engine/collision/**`, new
`tests/TestCharacterTriggers.cpp`, narrow CMake registration,
`docs/CHARACTER_TRIGGERS.md`, necessary trigger-related amendments to
`docs/CHARACTER_CONTROLLER.md`/`docs/COLLISION_WORLD.md`, and this directory's
`REPORT.md` plus ignored `artifacts/m2-c/` evidence. Preserve existing movement
and query behavior/assertions. Root owns PLAN/CHECKPOINT/ACCEPTANCE and shared
readiness/index/verification/boundary/README updates. You are not alone in the
codebase: preserve and accommodate other edits. No recursive agents, staging,
commit/push, provider changes or work outside the approved paths.

The same explicitly selected Astra worker may implement this related bundle.
Reuse the recent Flash HTTP 402 evidence and the user-authorized/disclosed
fallback; no new paid routing smoke or provider changes. Root retains acceptance.

## Acceptance and verification

Separate `PhlosionTriggerTests` / `PhlosionTriggerContracts`, Collision-only,
/W4 /WX and 120-second timeout. Real Jolt authored geometry, independent expected
membership/event lists and complete metadata/order comparisons. Cover:

1. Empty world, first Enter, repeated Stay, movement Exit, re-entry, multiple
   simultaneous sensors, deterministic sorted events and mesh deduplication.
2. Precise capsule-vs-box corner/AABB false positive; rotated box and transformed
   triangle sensors, both mesh sides, zero-height sphere. Negative nonsensor
   and character overlaps. Discrete thin-sensor crossing limitation.
3. Independent movement/trigger masks and ignore IDs, bit31/zero, filter changes,
   stale/foreign ignore, full metadata and output/history ownership.
4. Teleport into/out/across sensors; blocked/invalid teleport and failed movement
   do not alter history; sampling does not change character state or world data.
5. Sensor removal and body-slot replacement yield Exit(old)/Enter(new) with old
   metadata preserved; character remove/clear and reused character capacity,
   independent worlds/characters and repeat create/use/destroy lifetimes.
6. Unknown/stale/foreign IDs and wrong-thread calls preserve sentinel output and
   history; independent owner-thread world. Dense membership up to a practical
   small configured maxColliders verifies no truncation and bounded stale history.
7. Repeated authored sample/step/teleport/filter sequences produce identical
   events and membership with expected IDs mapped per world.

Configure existing build-presentation, build all enabled Debug targets with
warnings as errors and run all CTest entries. Preserve complete native logs and
exit codes under `artifacts/m2-c/`, printing only useful summaries/failure detail.
Confirm new executable imports/link boundary remains collision/GLM/Jolt plus
platform runtimes. No GPU verification needed for this CPU-only addition; M2
native walkable/runtime/editor qualification stays explicitly pending.

Worker completes its implementation/test/debug loop and returns a ready-for-review
REPORT with paths, exact commands/results, fixed failures and limits. Send
substantive milestone messages for user communication. Up to 90 minutes for this
bounded bundle; checkpoint material conflicts instead of expanding scope.
