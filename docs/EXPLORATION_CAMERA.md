# Exploration camera

`Phlosion::Camera` is an opt-in, renderer-neutral exploration follow rig. Include
`engine/camera/ExplorationCamera.h`; its public namespace is `engine::camera`.
The static target links publicly only `Phlosion::Collision` and GLM, with Jolt
and standard runtime libraries inherited through Collision. It does not link
Render, Platform, Input or the Engine aggregate, and does not create a window,
event pump, worker thread or clock. Top-level configuration still discovers the
engine's existing dependencies.

The default framing follows a target with fixed world yaw/pitch and an offset
pivot. Projects may supply orbit and zoom controls, or simply update the target.
The rig owns no scene objects, action names, camera zones or game rules.
Native application/editor integration, authored-scene schema/inspectors, battle
cuts/blends/shake and physical input devices remain separate work.

## Ownership and transactions

`ExplorationCameraSettings` is a plain authored value. A noncopyable, nonmovable
`ExplorationCameraRig` copies it at construction and exposes a const reference
through `settings()`. Later authoring edits require a new rig. Invalid settings
throw `std::invalid_argument`.

Every rig operation belongs to its constructing thread. Wrong-thread calls
throw `std::logic_error`; wrong-thread destruction terminates. A supplied
`const CollisionWorld*` must share that owner thread, including on an invalid
frame. It is borrowed for the call and never retained. Independent rigs and
worlds may coexist on their respective owner threads. The standalone
`cameraRelativeMove` helper has no runtime ownership.

`pose(out, error)` fails before the first successful reset or step.
`reset(targetPosition, aspectRatio, out, world, error)` discards accumulated
orbit, zoom and follow/recovery lag, restores authored orientation/distance,
and recomputes obstruction immediately. It is the explicit teleport/cut API;
there is no automatic teleport threshold.

`step(frame, out, world, error)` stages all controls, follow and collision
results before publishing. Every boolean failure leaves the complete runtime
state, settings and caller output unchanged and sets the optional error string.
Success clears it. Thread errors also occur before state/output changes.
Allocation failure follows normal C++ allocation behavior, as in Collision.

## Settings and frame limits

All numeric values must be finite. Ranges include their endpoints unless stated.

| Setting | Default | Valid range |
| --- | --- | --- |
| `pivotOffset` | `(0,1,0)` | Each component `[-100,100]` |
| `initialYawRadians` | `0` | `[-1000000,1000000]` |
| `initialPitchRadians` | `.35` | Between authored pitch limits |
| `minPitchRadians`, `maxPitchRadians` | `-1.4`, `1.4` | Ordered in `[-1.45,1.45]` |
| `minDistance`, `desiredDistance`, `maxDistance` | `.5`, `6`, `30` | `.1 <= min <= desired <= max <= 1000` |
| `followHalfLifeSeconds` | `.12` | `[0,10]` |
| `recoveryHalfLifeSeconds` | `.15` | `[0,10]` |
| `probeRadius` | `.2` | `[.01,2]` |
| `safetyMargin` | `.03` | `[0,.5]` |
| `layerMask` | All bits | Any 32-bit mask; zero matches nothing |
| `ignore` | Empty ColliderId | One ignored ID; stale/foreign IDs have no effect |
| `verticalFovDegrees` | `45` | `[1,150]` |
| `nearPlane` | `.1` | `[.001,10]` |
| `farPlane` | `1000` | `nearPlane < farPlane <= 100000` |

| Frame field | Default | Valid range |
| --- | --- | --- |
| `targetPosition` | `(0,0,0)` | Each component `[-10000,10000]` |
| `aspectRatio` | `16/9` | `[.25,8]` |
| `deltaSeconds` | `0` | `[0,.25]` |
| `lookDeltaRadians` | `(0,0)` | Each component `[-100,100]`; X yaw, Y pitch |
| `lookRateRadiansPerSecond` | `(0,0)` | Each component `[-20,20]` |
| `zoomDelta`, `zoomRate` | `0`, `0` | Each `[-1000,1000]`; distance units and units/second |

The authored target-plus-offset pivot, smoothed pivot, desired eye endpoint,
final eye and published look target must also remain within `[-10000,10000]`
per component. A candidate desired endpoint outside that envelope fails before
the query; a wall cannot rescue an invalid endpoint. This conservative check
also applies when follow lag would keep the current pivot closer to the origin.

## Orientation, controls and damping

At yaw zero the eye is behind the pivot on +Z. Positive yaw moves it toward +X;
positive pitch raises it. The boom's unit vector from pivot to eye is
`(sin(yaw)*cos(pitch), sin(pitch), cos(yaw)*cos(pitch))`.
Published yaw wraps to `[-pi,pi)`; pitch and user zoom clamp to the authored
limits. Explicit angular/zoom deltas apply once; rates multiply the supplied dt.
At dt zero explicit deltas still apply, but rates and follow/recovery time do
not advance, even when a half-life is zero. Inward obstruction response still
applies immediately. The first successful step snaps pivot and boom using that
frame's controls, even at dt zero.

For positive dt, follow moves the pivot toward `targetPosition + pivotOffset`
with `alpha = 1 - exp(-ln(2) * dt / halfLife)`. Zero half-life means immediate.
The same analytic rule recovers boom distance outward toward current clearance.
A stationary target/clearance gives equivalent results under timestep splitting
within floating-point tolerance. Changing goals, orientation or obstructions
between steps does not have that partition equivalence guarantee. No inward
distance change is smoothed, including user zoom inward. Orientation is immediate.

## Collision and pose

Every candidate uses the real CollisionWorld capsule sweep with zero cylinder
half-height, making it a sphere of `probeRadius`. The cast runs from the smoothed
pivot toward the desired eye. Sensors are always excluded. The authored layer
mask and ignored collider apply; characters are invisible to static queries.
On a miss or without a world, allowed distance equals desired distance. On a
hit it is `clamp(hit.fraction * desiredDistance - safetyMargin, 0, desiredDistance)`.
Positive initial penetration reports `originBlocked=true` and allowed distance
zero. Touching may produce a zero-distance obstruction without positive overlap.
The authored minimum is never enforced after collision.

When allowed distance shrinks below current actual distance, pull in immediately.
Otherwise recover outward using the half-life, except reset/first step which snap.
`obstructed` means the current desired radial probe hit; a recovering short boom
can have `obstructed=false`. `obstruction` copies the complete Hit metadata,
fraction, point, normal and overlap flag, so snapshots remain readable after
collider/world removal. Query false results propagate transactionally. Present
camera bounds and filters are a valid subset of Collision's public query limits,
so solver/query-false injection is not exposed by this module.

The pose publishes pivot, position, yaw/pitch, desired/actual distance, supplied
aspect and unchanged authored lens values. `forward` points toward the pivot's
direction; `up` is the orthonormal camera up, not fixed world up. `lookTarget`
is always `position + forward`, independent of boom length. This remains a
nondegenerate look-at pair when the eye collapses onto the pivot. The bounded
pitch keeps the basis away from the world-up singularity. No projection/view
matrix conventions are embedded in this module.

This is a **final radial sphere probe**. It does not sweep the eye's trajectory
between frames, guarantee full-frustum/physical near-plane clearance for every
FOV, or interpret triangle sheets as closed volumes. Contact tolerances and mesh
conventions are inherited from [CollisionWorld](COLLISION_WORLD.md). Origin-blocked
poses explicitly report their penetration; they are not collision-free poses.

## Existing Camera3D conversion

The existing render-side Camera3D type and its pan/orbit behavior remain unchanged.
A renderer consumer can convert a successfully published pose exactly as follows:

```cpp
#include "engine/camera/ExplorationCamera.h"
#include "engine/render/Camera3D.h"

Camera3D makeRenderCamera(const engine::camera::ExplorationCameraPose& pose) {
    Camera3D camera(pose.verticalFovDegrees, pose.aspectRatio,
                    pose.nearPlane, pose.farPlane);
    camera.setPosition(pose.position);
    camera.lookAt(pose.lookTarget, pose.up);
    return camera;
}
```

For a persistent Camera3D with the same lens, call `setAspectRatio`, `setPosition`
and `lookAt` on each successful pose. Construct a new Camera3D when lens settings
change, since its current public API has no lens setters. The consumer links
Render itself; Camera does not acquire that dependency.

## Input and character composition

`cameraRelativeMove(screenMove, yaw, outWorldXZ, error)` maps X-right/Y-down stick
or action intent to world X/Z while ignoring pitch. At yaw zero `(0,-1)` maps to
world `(0,-1)`; at yaw +pi/2 it maps to `(-1,0)`. Analog magnitudes up to one are
preserved; larger finite vectors normalize with robust double-precision arithmetic,
including `FLT_MAX` components. Nonfinite input/yaw or yaw magnitude above one
million fails without changing output.

Games own action names and scheduling. A typical fixed tick reads published input
actions, converts movement using the current camera yaw, calls `stepCharacter`,
then supplies the resulting character center to the rig's next frame. Right-stick
actions can supply angular rates; the caller accumulates mouse angular/scroll
deltas separately because InputActions does not publish motion/scroll sources.
Do not add a second event pump. Reset the rig explicitly when teleporting/cutting.

## Verification

`PhlosionCameraTests` links only Camera and Input and registers as
`PhlosionCameraContracts` with a 120-second timeout and the common `/W4 /WX`
policy. Ten groups cover analytic framing/projection at 16:9 and 21:9, controls,
half-life partition equivalence, real Jolt thin/rotated box and mesh probes,
radius clearance, penetration/touching, filters/copied metadata, finite/boundary
validation and transactional failures, movement math, ownership and independent
instances, and a repeated InputActions -> real character -> camera sequence.
That sequence exercises walk/run, wall blocking, orbit-relative movement, focus
release/rearm and explicit obstruction recovery/reset. A separate negative-process
probe `PhlosionCameraTests --wrong-thread-destruction` installs a terminate handler
that exits 73, verifying wrong-owner destruction without aborting the test runner.

```powershell
cmake -S . -B build-presentation
cmake --build build-presentation --config Debug --parallel 4
ctest --test-dir build-presentation -C Debug --output-on-failure
```

These are CPU contracts, including mathematical projection checks and real API
composition. They do not establish native runtime/editor scenes, rendered
aspect-ratio captures, hardware input or GPU visual parity. No host/render
behavior is changed. Local commands, raw output, exit codes, flags/imports and
hashes are retained under ignored `artifacts/m2-e/`; see the
[M2-E report](agent-work/cyberpunk-m2-camera/REPORT.md).
