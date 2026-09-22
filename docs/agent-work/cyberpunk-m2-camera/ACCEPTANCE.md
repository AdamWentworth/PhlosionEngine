# M2-E acceptance: exploration camera rig

Accepted by Astra on 2026-09-22 from baseline
`bd763913ab35d770fec7716ad1c70ccdc1b5baa0` (`Add named input actions and SDL controller support`).
M2-D was committed as requested before this pass. The user subsequently approved
committing M2-E as `Add exploration camera rig and collision-aware framing`.
Root rechecked the accepted scope and verification hashes before committing;
implementation and tests were unchanged. No push was made.

## Accepted behavior

`Phlosion::Camera` supplies a CPU exploration follow/orbit rig with immutable
copied authored settings and separate owner-thread runtime state. Explicit
angular/zoom deltas and rates drive bounded orientation/distance; analytic
half-life damping follows the target and recovers distance outward. Reset
restores authored framing immediately for teleports/cuts.

Real CollisionWorld sphere sweeps constrain the radial boom, exclude sensors
and honor layers/ignored IDs. Inward correction is immediate, even below the
authored zoom minimum. Origin penetration is explicit and retains a finite
viewing basis at zero distance. Returned hit metadata is copied; the world is
borrowed only for each call. Failed operations preserve output and runtime state.

The relative-movement helper preserves analog strength, normalizes large finite
inputs robustly and maps action/stick intent to horizontal world movement at the
camera yaw. The guide shows conversion to existing Camera3D. No accepted input,
collision, render, platform, runtime or editor implementation changed.

## Review and evidence

Root reviewed the complete public header and implementation, all ten test groups,
CMake target links and public guide against the original PLAN and baseline.
Both contract compliance and code quality passed with no outstanding findings.
Review covered staged publication, independent settings, ownership before world
queries, bounds/control rejection, dt-zero and damping semantics, initial/reset
state, finite basis construction, collision shortening and dependency boundaries.

The final enabled VS2026 Debug build passed. All fifteen CTest entries passed
in 333.88 seconds, including the camera entry in 0.10 seconds. Its ten groups
cover analytic framing at 16:9/21:9, rates/deltas, split-step damping, real thin
walls/rotated boxes/transformed meshes, radius clearance, penetration/touching,
filters/copied metadata, malformed/boundary values, transactional failure,
movement math, owner threads and independent instances. A twice-repeated actual
InputActions -> kinematic character -> camera sequence verifies walk/run,
blocking, orbit-relative movement, focus release/rearm and reset/recovery.
The separate wrong-owner destruction process exited with the expected code 73.

Actual compiler records establish `/W4 /WX` for engine_camera and its standalone
test target. Import inspection shows only Windows/C++ runtimes, without SDL or
graphics backend DLLs. The unchanged legacy aggregate `/W1 /WX-` exception is
documented in [verification](../../VERIFICATION.md); no global override was used.
The original `CMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc` remains in the local cache.

Root checked successful exit codes, raw test/build results, compiler/link/import
records and all fifteen source/document/binary hashes. Evidence under ignored
`artifacts/m2-e/` includes:

- `09-final-build.log`, `10-all-ctest.log`, `CTest-LastTest.log`;
- `11-destruction.exit.txt`, `12-imports.log`, copied CL/link tlogs;
- `source-binary-sha256.json`, `evidence-sha256.json`, `boundary-diff.txt`;
- command/exit sidecars and retained failed development/verifier attempts.

The original PLAN remains unchanged in the source hash manifest; acceptance
status and resume instructions live here and in [CHECKPOINT](CHECKPOINT.md).
No redundant full-suite rerun was needed after review; only shared documentation
changed after the worker's final verification.

## Limits and routing

Present valid camera inputs are a subset of Collision's valid query envelope.
False-query propagation is implemented but no artificial solver-failure hook
was added. Concrete failure tests exercise post-control bounds rejection and
borrowed-world thread errors, preserving prior state and caller output.

This accepts CPU API composition. It does not qualify native runtime/editor
input/camera integration, physical devices, rendered aspect-ratio scenes or GPU
parity. The final radial sphere probe does not guarantee swept eye-path,
whole-frustum or arbitrary near-plane clearance. Camera zones, authoring UI,
battle blends/shake and the full walkable-scene M2 gate remain subsequent work.

Implementation used the previously disclosed, user-authorized Astra fallback
after Flash HTTP402. The routing doctor observed unchanged static configuration;
this pass makes no Flash inference claim. Root retained architecture and final
acceptance, and explicitly interrupted the completed worker.
