# M2-A: reusable static collision queries

Baseline: main `08f57919785671d1120928b74fe56cbafad5265a`, clean at orientation.
User requests the first M2 implementation pass. M1's engine-facing checklist is
accepted; private content remains separate. No automatic commit or push.

Outcome: accepted by root Astra on 2026-09-20. See [ACCEPTANCE](ACCEPTANCE.md)
for source review, routing fallback, final evidence and remaining boundaries.

## Decision and scope

Start M2 with a real CPU collision world: static boxes and triangle meshes,
layers, sensor volumes, nearest ray/capsule casts and precise capsule overlaps.
This is the dependency for the later kinematic controller, collision-aware
camera and trigger interaction. Do not claim a playable scene or complete M2.

Use Jolt through the existing pinned vcpkg manifest. At engine baseline
`b7ba15730cadac50ce5092223efc92972c0afc5b`, `joltphysics` resolves to **5.4.0#0**
(verified with `git show <baseline>:versions/baseline.json`). The local checkout's
current port is newer; do not confuse it with the selected baseline. Do not
upgrade other dependencies/baseline or enable Jolt renderer/sample features.

Rationale: Jolt already provides the required narrow-phase queries and a future
virtual-character path. Keep collision math with that implementation and own
the Phlosion API, lifetime, filtering and validation contract. Writing a custom
sweep/mesh solver increases first-pass correctness risk; a general physics
abstraction with interchangeable vendors is unnecessary for this bounded step.
No comparative performance claim is made. Upstream references:

- https://github.com/jrouwe/JoltPhysics/tree/v5.4.0
- https://github.com/jrouwe/JoltPhysics/blob/v5.4.0/HelloWorld/HelloWorld.cpp
- https://github.com/jrouwe/JoltPhysics/blob/v5.4.0/LICENSE
- https://jrouwe.github.io/JoltPhysics/class_narrow_phase_query.html

New `engine_collision` / `Phlosion::Collision` static target, independent of
render/platform/editor targets. Consumers explicitly link it. Do not change
`Phlosion::Core`, renderer dependencies, the Engine umbrella's existing link set,
editor/plugin ABI, scene formats or game repositories. The top-level manifest
and configure step may install/find Jolt, but a collision-only executable must
not link SDL/OpenGL/Vulkan/D3D12 or create a graphics context. Public headers
expose engine/GLM types only; keep Jolt implementation details behind a PImpl.

## Public contract

Own the API in `src/engine/collision/CollisionWorld.h/.cpp` (narrow private
helpers in that directory if useful), namespace `engine::collision`.

- Non-copyable/non-movable `CollisionWorld(options = {})`; owner-thread API.
  Multiple worlds may coexist and be created/destroyed repeatedly. Initialize
  Jolt's process runtime exactly once with clear ownership and shutdown order;
  never overwrite or destroy a factory owned by an external Jolt integration.
  External management of Jolt globals need not be supported: fail clearly and
  document the limitation. No simulation update/job system is needed for queries.
- `ColliderId`: opaque monotonic nonzero 64-bit identity, unique across live
  worlds and never reused after remove/clear. Zero, stale and foreign handles
  must not accidentally access/remove a body. Do not expose raw Jolt BodyIDs.
- Static collider description: rigid position/rotation (identity default),
  layer index 0..31, sensor flag, caller-owned `uint64_t userData` value.
  Boxes add positive half extents. Meshes consume vertex/index spans, indexed
  triangles only, with owned/cooked storage after insertion. No borrowed input
  storage, dynamic bodies, transform updates or scale handling in this pass.
- `bool addBox(..., ColliderId& out, string* error)` and
  `bool addTriangleMesh(..., ColliderId& out, string* error)`, plus `remove`,
  `clear`, `contains`, `colliderCount`. Reject nonfinite/invalid dimensions,
  transforms, layers, indices, empty/non-triangle and degenerate mesh input
  before reaching Jolt assertions. Normalize finite nonzero quaternions or
  require unit quaternions explicitly; document and test the chosen policy.
  Reject engine capacity/input limits with a diagnostic; failed additions leave
  output and existing world unchanged. Releasing a collider recovers capacity.
  Default collider limit 4096; validate a documented safe upper bound (<=65536).
  Bound per-mesh input counts with explicit options/defaults (<=1M vertices and
  triangles); these limits are not a process/peak-memory budget.
- `QueryFilter`: 32-bit layer mask, sensor policy Exclude (default)/Include/Only,
  optional single ignored ColliderId. Layer mask zero is a valid empty query.
  Ignore/filter applies consistently to ray, capsule sweep and capsule overlap.
- Rays are finite world-space `origin + displacement * fraction`, not an
  implicitly normalized direction. Nonzero displacement required. Valid queries
  return success independently of whether a hit exists (`optional<Hit>` output).
  Include segment endpoint hits; nearest hit fraction in [0,1], collider ID,
  copied userData/sensor metadata, point and unit outward collider normal.
  Tie ordering may be unspecified; document it. Specify/test inside-convex ray
  behavior (fraction zero allowed, with defined finite normal policy).
- Upright Y-axis capsule: center, positive radius, nonnegative cylinder
  half-height (zero permits a sphere). Sweeps use finite nonzero displacement;
  overlap has no displacement. Capsule sweep returns the nearest contact with
  hit fraction, collider contact point/normal, and explicit initial-overlap
  indicator; initial penetration must report fraction zero, never negative.
  Overlap returns unique collider metadata sorted by ColliderId; do precise
  shape/mesh testing, not an AABB-only proxy. No trigger event state machine.
- Mesh casts/overlaps are double-sided for this bounded public contract. State
  contact tolerances and avoid promising cross-platform bitwise determinism.
- Invalid queries return false with diagnostic and preserve previous output;
  successful misses publish empty output. Query calls do not mutate the world.
  No exceptions/assertions for ordinary malformed input, invalid IDs or full
  capacity; constructor/configuration/resource-exhaustion exceptions may follow
  documented C++ conventions. No GPU, disk I/O, game semantics or hidden fallback.

The worker may choose exact field/signature spelling and internal data layout
within this contract. Do not implement the future controller now or expose Jolt
handles in preparation for it. Surface a material contract conflict before
changing these boundaries.

## Tests and acceptance

Add a separate `PhlosionCollisionTests` executable and CTest entry
`PhlosionCollisionContracts`, linked only to `Phlosion::Collision`. Use real
Jolt queries and authored literal geometry, not a mocked backend. Apply the
engine's /WX settings to the new library and tests.

Tests must cover:

1. Empty/valid miss, nearest ray and capsule hit at independently calculated
   fractions/normals, thin-wall sweep (no tunnelling), rotated box, floor and
   authored ramp triangle mesh, both triangle sides and segment endpoints.
2. Initial penetration, zero-cylinder capsule, true overlap and an AABB-overlap
   false positive rejected by precise geometry. Deduplicate mesh contacts.
3. Layers including bit 31, zero mask, sensor modes and ignore filtering for
   every query family; metadata is preserved. Sensor inclusion is a query choice,
   not a claim that trigger enter/exit events or character response exists.
4. Missing/stale/foreign IDs, two simultaneous worlds, remove/clear and repeated
   create/use/destroy, ID nonreuse, capacity failure/reuse, caller geometry lifetime.
5. NaN/infinity, invalid quaternion/layer/extents/capsule, malformed triangle
   indices/degeneracy and configured size limits. Failed operations preserve
   sentinel outputs and earlier collision results. Exact configuration/count
   boundaries must be exercised without huge allocations.
6. Same scripted query sequence against floor/wall/ramp/sensor authored geometry
   produces the same results on repeated runs on this host. Tests are public CPU
   fixtures, independent of game content or rendering.

Use `cmake -S . -B build-presentation` to retain its VS2026/Debug/vcpkg/settings,
then build all enabled Debug targets `/WX` and run all CTest entries. Reuse
existing scripts/conventions. Retain full logs under ignored `artifacts/m2-a/`.
Document installed Jolt version and relevant CPU build requirements from actual
resolved headers/config (do not assume generic upstream minimum CPU features).
Verify the collision-only executable's actual dependencies/link boundary using
available Windows tooling. No native visual matrix is required because this pass
changes no renderer, host integration or visual behavior; do not claim GPU proof.

## Worker ownership and routing

One worker owns implementation/discovery/testing/debugging of this bundle:
`src/engine/collision/**`, `tests/TestCollision*.cpp` (new standalone test entry),
`CMakeLists.txt`, `vcpkg.json`, `docs/COLLISION_WORLD.md`, and
`docs/agent-work/cyberpunk-m2-collision/REPORT.md`. Narrow build integration is
allowed; do not weaken old tests or unrelated warning settings. Root owns PLAN,
CHECKPOINT, ACCEPTANCE and shared readiness/verification docs. You are not alone
in the codebase: preserve others' edits and adjust to them. No recursive agents,
commits/staging/push, provider changes, game/editor/render source edits or new
packages beyond the approved pinned Jolt port. No dependency upgrades.

Native `astra_flash_builder` is requested; doctor is static-ready with unchanged
config hash `1c901d09c3060eab1d41cf8fbde2fd6a96447bef1d2e687817d57bc124b3220a`.
Reuse prior verified routing, not a new paid smoke. The user permits Astra
fallback if Flash is unavailable. Finish internal test/fix loops, then return
ready-for-review report listing changed files, commands/exits and limitations.
Up to 120 minutes for this bounded build; checkpoint a material blocker.

## Later M2 work (direction only)

After acceptance, define the controller's fixed-step movement/ground/slope/step
policy; then camera/input and runtime/editor walkable-scene qualification,
including focus/hotplug/recovery and 16:9/21:9 on all three APIs. These are future
briefs, not authorized expansion of this first pass. M2 remains in progress.
