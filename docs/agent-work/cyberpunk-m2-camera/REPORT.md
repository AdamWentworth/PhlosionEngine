# M2-E camera implementation report

Date: 2026-09-22. Implementation baseline:
`bd763913ab35d770fec7716ad1c70ccdc1b5baa0` (accepted M2-D).
This is the explicitly authorized Astra fallback after Flash HTTP402, not a
claim of Flash inference. No recursive agents, staging, commit, push, provider
changes or dependency changes were performed. Root retains acceptance.

Ready for root review: full enabled Debug build passed and all 15 CTest entries
passed in 333.88 seconds. The camera contract passed in 0.10 seconds.

## Delivered scope

- Added `src/engine/camera/ExplorationCamera.h/.cpp`: plain settings/frame/pose
  values, immutable copied settings, noncopyable/nonmovable owner-thread rig,
  transactional pose/reset/step, bounded controls, analytic follow and outward
  recovery, immediate inward response, and real CollisionWorld sphere sweeps.
- Added `engine_camera` / `Phlosion::Camera`, publicly linking only Collision
  and GLM. The engine aggregate and accepted input/collision/render/host/editor
  implementations are unchanged.
- Added robust camera-relative movement with X-right/Y-down input conventions,
  analog preservation and finite-extreme normalization.
- Added `PhlosionCameraTests` / `PhlosionCameraContracts`, linking only Camera
  and Input, using the common warning helper and a 120-second timeout.
- Added [the public guide](../../EXPLORATION_CAMERA.md), including all ranges,
  ownership/failure behavior, precise Camera3D conversion and integration limits.

The rig checks borrowed-world ownership before frame validation. It stages all
runtime changes until controls, bounds, query and final-pose checks succeed.
Target-plus-offset, candidate desired endpoint and published output stay within
the camera's conservative position envelope. Root confirmed that collision must
not rescue an out-of-envelope desired endpoint. Follow/query/output share the
same representable pivot. The pose publishes an orthonormal up and unit forward,
plus `position + forward` as its nondegenerate look target at zero boom distance.
Hit metadata is copied, and no world pointer is retained.

## CPU coverage

Ten standalone groups use independent analytic expected results and actual Jolt
geometry:

1. Initial/reset framing, immutable authored copies, fixed orientation, offsets
   and mathematical pinhole projection at 16:9 and 21:9.
2. Yaw wrapping, pitch/distance clamping, explicit delta/rate behavior at
   different dt values, dt-zero controls and first-step snapping.
3. Analytic half-life follow/recovery, split-step equivalence for stationary
   goals, zero-half-life and zero-dt rules, immediate inward response, reset.
4. Exact sphere-radius clearance against thin walls, rotated boxes, transformed
   double-sided triangle sheets, eye-overlap checks and copied hit metadata.
5. Origin penetration/collapse, exact touching, sensor exclusion, layer 31,
   empty mask, ignored/stale/foreign IDs, removal and borrowed-world lifetime.
6. Nonfinite and invalid constructor settings, ordered ranges and inclusive
   lower/upper boundary settings.
7. Invalid frame values, pre-initialization behavior, unchanged output/runtime
   after failure, pending orbit/zoom rollback on bounds failure, final look-target
   bounds and comparison against an unaffected control rig.
8. Movement axes/quadrants, analog/diagonal normalization, FLT_MAX and tiny finite
   inputs, invalid yaw/input and output preservation.
9. All rig operations on a wrong thread, wrong-world-thread reset/step rollback,
   and independent worlds/rigs on separate threads.
10. Twice-repeated InputActions -> relative intent -> real kinematic character ->
    camera target/probe sequence, including exact walk/run displacement, wall
    blocking, focus releases and controller-neutral rearm, orbit changing the
    movement direction, teleport/reset and obstruction removal/recovery.

The separate wrong-thread-destruction process probe installs a terminate handler
inside the offending thread and exits 73 as expected. Its fallback return is 1
if wrong-owner destruction incorrectly returns normally.

Present valid camera settings/frame/filter values form a subset of Collision's
valid query envelope. Thus a boolean query failure cannot be forced through
these public camera inputs without an artificial hook or changing accepted
collision code. False propagation remains transactional in implementation;
concrete failure tests cover post-control bounds rejection and borrowed-world
thread exceptions. No solver-failure injection is claimed.

## Evidence and negative attempts

All numbered commands, complete raw logs and exit files live under ignored
`artifacts/m2-e/`. Build output was compacted through the installed RTK launcher;
native commands tee their entire output into these logs before filtering. No
warning policy was weakened and no global `/WX` qualification was repeated.

| Evidence | Result |
| --- | --- |
| `01-configure` | Existing build-presentation configured successfully |
| `02-camera-build` | Retained failure: `/W4 /WX` rejected integer literals forwarded into a test's `pair<member-pointer,float>` |
| `03-camera-build` | Passed after explicit float test literals |
| `04-camera-tests` | Retained failure: composition test omitted the accepted controller-neutral rearm after focus return; preceding nine groups passed |
| `05-camera-build`, `06-camera-tests` | Passed after the test supplied neutral rearm and explicitly checked suppression of held controller input; ten groups passed |
| `07-full-build` | Full enabled Debug build passed |
| `08-destruction` | Retained negative-process attempt: wrong-owner destruction terminated with exit 3 rather than the custom 73 because its handler was installed only on the main thread |
| `09-final-build` | Full enabled Debug build passed after installing the handler in the offending thread |
| `10-all-ctest` | All 15 CTest entries passed, exit 0, 333.88 seconds; camera contract 0.10 seconds |
| `11-destruction` | Expected termination-handler exit 73 |
| `12-imports` | Only MSVCP140D, VCRUNTIME140D, VCRUNTIME140_1D, ucrtbased and KERNEL32 imports; no SDL/graphics imports |
| `13-compiler-evidence` | Actual compiler tlogs prove `/W4 /WX` for engine_camera and PhlosionCameraTests; actual link command copied |
| `14-diff-check`, `15-ignore` | Diff whitespace check and ignored-evidence check passed |
| `16-final-evidence` | Final source/binary SHA-256 manifest, detailed CTest log, source-boundary diff and worktree status |
| `17-manifest-verifier-attempt`, `18-final-hashes` | Retained verifier-script failure from wrapping PowerShell's JSON array in another array; corrected enumeration verifies every individual source/binary hash and records the evidence manifest |

`compiler-flags.json` and copied CL/link tlogs retain exact affected compiler
and linker commands. The link record contains Camera, Input, Collision, Jolt,
GLM and normal platform/runtime libraries. `legacy-aggregate-observation.json`
records the unchanged ordinary aggregate exception: all 33 observed compiler
commands used `/W1 /WX-`. No global override was supplied.
`source-binary-sha256.json` records final delivered source/document and executable/
library SHA-256 values; `evidence-sha256.json` covers the retained evidence files.

## Boundaries

The new behavior is CPU-only API composition. It does not qualify native runtime
or editor integration, physical devices, rendered aspect-ratio scenes or GPU
parity. Existing Camera3D/event pumps/InputEvent/plugin ABI were not changed.
The probe qualifies the final radial sphere, not a swept camera trajectory,
frustum clearance, mesh volume containment or near-plane safety at every FOV.
Full host integration and authored scene/editor controls remain subsequent work.
