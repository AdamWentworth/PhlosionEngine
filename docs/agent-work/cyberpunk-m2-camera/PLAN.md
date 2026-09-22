# M2-E: exploration camera rig

Date: 2026-09-22. Baseline: clean main
`bd763913ab35d770fec7716ad1c70ccdc1b5baa0` (M2-D committed as requested).
Deliver the reusable exploration camera dependency and a real CPU composition
with the accepted input/character APIs. Native host/scene integration follows
once this camera contract is stable. Do not commit or push this new pass.

## Architecture and evidence

Existing Camera3D is a render-side position/target/lens value with orbit/pan/zoom,
not a follow rig. Application and editor own different Camera3D/event paths;
preserve both, InputEvent and the plugin ABI. Existing camera pan tests stay.
CollisionWorld already supplies sphere sweeps (capsule half-height zero), exact
overlaps, filters and static geometry without SDL/graphics. Characters are not
visible to static queries. InputActions publishes normalized vectors and edges;
mouse motion/scroll action sources are absent. Do not add a second event pump.

Add opt-in `engine_camera` / `Phlosion::Camera`, linking publicly only Collision
and GLM (plus their normal runtime dependencies), not Render/Platform/Input or
the Engine aggregate. Public files live under `src/engine/camera/`, namespace
`engine::camera`. No new dependency, global manager, worker thread or wall clock.
Use a plain authored `ExplorationCameraSettings` and separate noncopyable,
nonmovable, owner-thread `ExplorationCameraRig` runtime instance. Settings are
copied at construction and remain immutable; applying authoring edits creates a
new rig. No renderer matrix conventions or authored-scene schema changes here.

The chosen rig follows a target with a fixed world orientation by default;
explicit orbit controls are optional. This supports authored exploration framing
without baking a particular game's camera zones or names into the engine.
Battle camera cuts/blends/shake and scene/editor inspectors are later work.

## Public contract

Provide concrete public settings, frame input and pose types. Suggested API:
constructor(settings = {}), `settings()`, `pose(out, error)`,
`reset(targetPosition, aspectRatio, out, world = nullptr, error = nullptr)`,
`step(frame, out, world = nullptr, error = nullptr)` and a standalone bool
`cameraRelativeMove(screenMove, yawRadians, outWorldXZ, error = nullptr)`.
Adjust signatures for clear idiomatic C++ but preserve every behavior below.
The optional CollisionWorld is borrowed for the call, never retained; caller
and world must share their owner thread. Queries and all rig operations check
ownership before work; wrong-thread calls throw logic_error, destruction on
the wrong thread terminates. Invalid constructor settings throw invalid_argument.
Bool failures preserve complete rig state, settings and caller output, and set
the optional error; success clears it. A pose read before initialization fails.

Settings (finite values throughout):

- World-space pivot offset, default (0,1,0), each component [-100,100]. Target
  positions have components [-10000,10000]; derived pivot/eye/look target must
  also remain in this envelope, including after smoothing and collision.
- Initial yaw 0 radians (camera behind target on +Z), initial pitch .35 radians,
  min/max pitch -1.4/+1.4 radians. Pitch limits must be ordered in [-1.45,1.45]
  and contain initial pitch. Finite yaw magnitude <= 1e6; wrap published yaw
  into [-pi,pi). Positive yaw moves camera toward +X; positive pitch raises it.
- Desired distance 6, min distance .5, max distance 30. Ordered
  .1 <= min <= desired <= max <= 1000. User zoom clamps to this authored range;
  obstruction may pull closer than min, including zero.
- Target-follow half-life .12 seconds and outward distance-recovery half-life
  .15 seconds, each [0,10]. Zero means immediate. Use analytic exponential
  half-life damping, so a stationary target/clearance is partition-independent
  within floating-point tolerance. Do not smooth obstructed inward movement.
- Collision probe radius .2 in [.01,2], safety margin .03 in [0,.5], layer mask
  all bits, optional ignored ColliderId. Camera queries always exclude sensors.
- Vertical FOV 45 degrees in [1,150], near plane .1 in [.001,10], far plane 1000
  with near < far <= 100000. These are renderer-neutral lens values; output
  carries them plus the supplied aspect ratio, without constructing matrices.

Frame input contains targetPosition, aspectRatio (default 16/9, range [.25,8]),
deltaSeconds in [0,.25], lookDeltaRadians (per-event accumulated angular delta,
components [-100,100]), lookRateRadiansPerSecond (rate components [-20,20]),
zoomDelta (distance units, [-1000,1000]) and zoomRate (units/sec, same range).
Positive zoom increases desired distance. Apply deltas once, rates times dt;
dt zero still applies explicit deltas but no rates or follow/recovery time.
Each first successful step snaps pivot and boom to its computed valid state,
including that frame's controls. Reset restores authored yaw/pitch/distance,
snaps to the supplied target/aspect and recomputes obstruction immediately.
Reset is the explicit teleport/cut operation; it discards accumulated runtime
orbit, zoom and follow lag. No hidden automatic teleport threshold.

Pose contains pivot, position, normalized forward/up or a nondegenerate
lookTarget, yaw/pitch, desired and actual distance, aspect/lens, and obstruction
information with copied hit metadata where present. Never produce an invalid
lookAt pair when boom distance collapses to zero: publish a unit look direction
or a look target one unit ahead, independently of pivot-to-eye distance. If up
is exposed, document whether it is world up or the orthonormal camera basis.
Guide shows exact conversion to existing Camera3D without changing that type.

For each candidate pivot/orbit/desired distance, sphere-sweep from pivot toward
the desired eye using the real CollisionWorld. Allowed distance is hit fraction
times desired length minus safety margin, clamped [0, desired], or desired on a
miss/no world. Positive initial overlap explicitly reports originBlocked and
collapses to zero; do not silently call that a collision-free pose. Pull in
immediately when clearance shrinks; damp outward recovery toward allowed length.
Reset/first step bypass recovery damping. Query/filter failure or out-of-envelope
results reject transactionally, including pending orientation/zoom changes.
Do not enforce the authored minimum after collision or use point raycasts.
This qualifies the final radial probe, not a swept camera trajectory, frustum
clearance, mesh volume containment or physical near-plane safety at every FOV.

cameraRelativeMove uses X right, Y down/back (matching InputActions stick axes:
negative Y means forward). Return world X/Z intent at the given yaw, ignoring
pitch, preserving analog magnitudes <= 1 and normalizing larger finite input
using robust arithmetic. Reject nonfinite inputs/yaw or yaw outside the stated
envelope without modifying output. At yaw 0, (0,-1) maps to world (0,-1);
at yaw +pi/2, (0,-1) maps to world (-1,0). No input names in engine code.

## Verification and boundaries

Add standalone `PhlosionCameraTests` / `PhlosionCameraContracts`, common /W4 /WX
settings and 120-second timeout, linked only Camera and Input for composition.
Use independent analytic expected results and actual Jolt geometry. Cover:

- Initial/reset framing, fixed orientation and offsets; yaw wrapping, pitch and
  distance limits; delta vs rate semantics at different dt and dt zero; vertical
  framing at 16:9/21:9 using explicit mathematical projection checks (CPU only).
- Analytic half-life follow and boom recovery, split timestep equivalence for
  stationary goals, immediate inward response, reset without residual lag.
- Real wall/thin wall/rotated geometry and mesh probe hits, exact sphere-radius
  clearance, initial penetration/touching, sensors excluded, layers/ignore,
  removal restoring distance and copied metadata. Verify eye overlap where the
  probe starts clear with tolerances consistent with existing query contracts.
- Finite/boundary validation, invalid settings/frame values, unchanged output
  and runtime state after failures, especially a bounds/query failure after
  otherwise valid controls; owner threads and independent rigs/worlds.
- cameraRelativeMove axes/quadrants/analog/diagonal/extreme finite inputs.
- A repeatable CPU exploration sequence through InputActions -> relative intent
  -> real kinematic character -> camera target and collision probe. Exercise
  walk/run, wall blocking, orbit changing relative movement, focus release and
  explicit recovery/reset. This is real API composition, not native host proof.

Build all enabled Debug targets in existing build-presentation, then all CTest
entries. Record actual compiler flags for affected targets and executable imports
(no SDL/graphics). Ordinary legacy aggregate tests have the documented /W1 /WX-
exception; do not repeat the expensive global /WX qualification or change unrelated
policy. Do not weaken existing tests. Retain commands, raw output, exit codes and
final source/binary hashes under ignored artifacts/m2-e/. Initial failures stay.
No new GPU matrix is needed because no host/render behavior changes. Native
runtime/editor, aspect-ratio scene captures and physical devices remain future.

## Ownership and execution

Worker owns new `src/engine/camera/**`, `tests/TestExplorationCamera.cpp`, narrow
CMake integration, `docs/EXPLORATION_CAMERA.md`, this directory's REPORT.md and
ignored evidence. Root owns PLAN/CHECKPOINT/ACCEPTANCE and shared roadmap,
verification, boundaries, target guide and README/index. You are not alone in
the codebase: preserve others' edits and accommodate changes. Do not modify
accepted input/collision/render/host sources; report a concrete contract blocker.
No recursive agents, staging/commit/push, provider changes or new dependencies.

Use the existing disclosed, user-authorized Astra fallback after the Flash HTTP402.
Routing doctor remains static-ready with unchanged config hash; this is explicit
Astra execution, not verified Flash inference. Root retains final acceptance.
Budget up to 120 minutes for this bundle. Complete internal implementation/test/
debug loops, checkpoint only real blockers, then report ready for root review.
