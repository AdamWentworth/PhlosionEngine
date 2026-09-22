# M2-B implementation report

Date: 2026-09-22. Baseline: `b570f4e2d5ee2decf13a1d8ae50526347a4ea681`.
Status: ready for root review; this worker does not grant acceptance or the M2
exit gate. Implemented under the root-disclosed, previously user-authorized
Astra fallback after the Flash attempt failed HTTP 402 before edits. No recursive
agents, staging, commits, pushes, dependency/provider changes or game edits.

## Delivered

- World-owned opaque character identities, capacity, owner-thread lifecycle,
  creation/read/tick/removal and safe teleport, using private Jolt 5.4.0
  `CharacterVirtual` with no inner rigid body or character-character collision.
- Analog world-XZ walk/run, fixed ticks, gravity/terminal fall speed,
  actual displacement-derived velocity, support metadata, slope/wall/corner
  response, stairs, floor following, bounded recovery and rollback when results
  leave the numeric envelope.
- Precise capsule destination validation with 0.0001 penetration tolerance;
  support refresh before removed collider bodies are destroyed; clear destroys
  characters before colliders.
- Ten real-geometry CPU test groups and a separate Collision-only executable
  inheriting /W4 /WX and a 120-second CTest timeout.

Owned paths changed:

- `src/engine/collision/CollisionWorld.cpp` and `.h`.
- New `src/engine/collision/CharacterController.h` and `CollisionTypes.h`.
  The latter shares existing collider identity/metadata definitions without a
  circular public-header dependency; existing query signatures are unchanged.
- `tests/TestCharacterController.cpp`.
- Narrow `CMakeLists.txt` character-target registration.
- `docs/CHARACTER_CONTROLLER.md`, `docs/COLLISION_WORLD.md`, and this report.

Root owns concurrent roadmap/PLAN/CHECKPOINT/ACCEPTANCE edits; those were
preserved. Existing collision tests were not modified or weakened.

## Contract decisions

Root approved [1,89] degrees as the explicit safe slope range: Jolt's
`IsSlopeTooSteep` disables slope limiting for cosine >=0.9999, roughly below
0.81 degrees. Tests cover both accepted boundaries and rejected adjacent/outside
values. Other finite bounds are documented: radius [.05,5], cylinder half-height
[0,10], speeds/gravity <=100, terminal fall speed [.01,100], step/snap [0,2],
dt [1/240,1/15], center components +/-10000, characters 1..4096.

`CharacterState::velocity` is actual last-tick center displacement/dt, including
stair/snap/recovery corrections, rather than Jolt's stored requested velocity.
A separate private downward accumulator prevents positional corrections from
launching a character. Reported velocity resets on teleport; gravity is clamped
before each tick. No clock or time accumulator was introduced.

Requested movement/stair/snap endpoints are checked before solver entry.
Out-of-envelope recovery restores Jolt state and leaves public outputs and
private gravity/velocity unchanged. Jolt's restore deliberately omits user data;
the metadata helper can resolve a live full body ID (including generation) from
owned metadata without dereferencing an old body handle.

## Commands and evidence

Complete raw logs are in ignored `artifacts/m2-b/`. Commands were executed
natively in PowerShell, redirected to those files, with compact tails printed
and the underlying exit codes preserved. RTK was not used.

| Command/check | Exit/result | Evidence |
| --- | --- | --- |
| `cmake -S . -B build-presentation` | 0 | `configure-01.log` |
| Targeted Debug build of character/collision tests | 0 | `build-targets-01.log` |
| First targeted CTest | 8; two new velocity assertions failed | `ctest-targets-01.log` |
| Targeted rebuild and CTest after velocity correction | 0; 2/2 passed | `build-targets-02.log`, `ctest-targets-02.log` |
| Expanded boundary-test build | 0 | `build-targets-03.log` |
| Boundary rollback negative run | 8; support preservation assertion failed | `ctest-targets-03.log` |
| Rebuild/test after metadata rollback correction | 0; 1/1 passed | `build-targets-04.log`, `ctest-targets-04.log` |
| `cmake --build build-presentation --config Debug --parallel 8` | 0, all enabled targets | `build-all-01.log` |
| `ctest --test-dir build-presentation -C Debug --output-on-failure` | 0; all 11 entries passed, 312.42 seconds | `ctest-all-01.log` |
| All-target Debug build after final test-only filter assertions | 0 | `build-all-02.log` |
| Final affected character CTest | 0; 1/1 passed | `ctest-character-final.log` |
| Direct final character executable | 0; 10/10 groups passed | `character-groups-final.log` |
| Generated project warning/link configuration | /W4 and /WX; Collision, GLM, Jolt and platform libs only | `compile-link-settings.log` |
| `dumpbin /dependents` character executable | 0; no SDL/graphics imports | `character-imports.log` |
| `git diff --check` | 0; only repository CRLF conversion notices | `diff-check.log` |

The final test-only assertions added after full CTest cover teleport's sensor,
zero-mask and ignored-body filters plus stale ignored IDs with reused collider
slots. All implementation code had already passed full CTest; the final affected
test was rebuilt and rerun. All 11 CTest entries include existing engine,
semantic/material contracts and public asset/animation/material/scene harnesses.
The character executable imports only `MSVCP140D.dll`, `VCRUNTIME140D.dll`,
`VCRUNTIME140_1D.dll`, `ucrtbased.dll` and `KERNEL32.dll`.

## Negative findings fixed

1. Jolt's `GetLinearVelocity` is the stored requested velocity, not the final
   collision-resolved movement. Independent zero-motion/wall-slide assertions
   exposed that mismatch. Public state now measures actual displacement with
   separately retained gravity state. No tolerance was widened.
2. A deliberately overlapping character near x=10000 recovered beyond the
   coordinate envelope; rollback restored position but omitted ground user data
   in Jolt's serializer. The live-metadata fallback now preserves support too;
   the sentinel/state preservation assertion passes unchanged.

## Behavior evidence and limits

Analytic checks verify walk/run/analog distances, diagonal/extreme-float
normalization, first gravity tick and terminal fall speed, floor height/normals,
wall/corner clearance, a 0.0002-unit thin wall at 100 units/s, a rotated/translated
mesh wall, ramp plane heights, steep-slope rejection, ceiling clearance,
below/above-height steps, disabled stepping/snap and ledge falling. Recovery and
teleport tests include exact touching, shallow tolerance, real penetration,
an AABB corner false positive, trapped geometry and safe escape. IDs, capacity
including 4096 concurrent characters, clear/recreate, support removal/reused
slots, seven wrong-thread methods, and a separate world on another thread pass.
Two render-frame batching arrangements and repeated authored sequences produce
matching states. NaN/infinity, all settings, boundary limits, sphere geometry,
failed-operation sentinels and numeric rollback have explicit assertions.

This is a CPU foundation. It does not claim native walkable/runtime/editor
integration or visual parity for characters. Static geometry only; no trigger
event state, camera/actions, host scheduler/interpolation, moving platforms,
dynamic bodies or character-character response. Best-effort recovery uses
bounded Jolt iterations/hits and does not promise escape from arbitrary trapped
geometry. Capsule geometry/contact padding can negotiate tiny edges even with
stairs disabled, as documented. Cross-platform bitwise determinism is not claimed.

Next action: root reviews the actual diff and evidence, requests any bounded
corrections, then records acceptance and roadmap state. No implementation blocker
remains.
