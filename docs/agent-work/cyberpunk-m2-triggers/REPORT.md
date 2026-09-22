# M2-C implementation report

Date: 2026-09-22. Baseline: `21672db1fef0a538db234b9c75386541d9af8aca`.
Status: ready for root review; all verification completed. Root retains
acceptance. This related bundle reused the previously disclosed,
user-authorized Astra fallback; no additional provider smoke or changes were
made. No recursive agents, staging, commits, pushes, dependency changes or game
edits.

## Delivered

`CollisionWorld::sampleCharacterTriggers` performs precise sensor-only capsule
overlap sampling, independently filtered from movement, then merges sorted
previous/current membership into Enter/Stay/Exit events. Each character owns
only its previous successful copied membership. Sampling is explicit and
discrete: no time step, event queue, automatic tick/teleport sampling, callback
or reconstructed movement path was added.

Both candidate output and next retained membership are built before publication.
The output move and membership swap do not allocate. Exit metadata comes from
the old snapshot even when its sensor body is removed. Public vectors contain
copied Phlosion metadata and cannot alias retained history. Removing a character
or clearing the world discards its history through existing ownership.

Owned paths changed:

- `src/engine/collision/CharacterTriggers.h` (new value types).
- `src/engine/collision/CollisionWorld.h` and `.cpp` (one API method, one retained
  vector per character, sorted merge; existing movement/query behavior unchanged).
- `tests/TestCharacterTriggers.cpp` (new seven-group standalone executable).
- Narrow `CMakeLists.txt` registration of `PhlosionTriggerTests` and
  `PhlosionTriggerContracts`, Collision-only, /W4 /WX, timeout 120 seconds.
- New `docs/CHARACTER_TRIGGERS.md`, trigger-related amendments to
  `docs/CHARACTER_CONTROLLER.md` and `docs/COLLISION_WORLD.md`, and this report.

Root's concurrent shared-documentation and PLAN/CHECKPOINT edits were preserved.
Existing character and collision test sources/assertions were not modified.

## Verification and evidence

Complete raw native logs are retained under ignored `artifacts/m2-c/`.
PowerShell redirected commands to those logs, printed useful summaries/tails,
and propagated the underlying exit codes. RTK was not used.

| Command/check | Result | Evidence |
| --- | --- | --- |
| `cmake -S . -B build-presentation` | Exit 0; existing targets/options preserved | `configure-01.log` |
| Debug build of trigger, character and collision executables | Exit 0 | `build-targets-01.log` |
| `ctest --test-dir build-presentation -C Debug -R 'Phlosion(Trigger\|Character\|Collision)Contracts' --output-on-failure` | Exit 0; 3/3 passed | `ctest-targets-01.log` |
| `cmake --build build-presentation --config Debug --parallel 8` | Exit 0; all enabled targets | `build-all-01.log` |
| `ctest --test-dir build-presentation -C Debug --output-on-failure` | Exit 0; 12/12 passed, 297.60 seconds | `ctest-all-01.log` |
| Final test-only trigger target rebuild and affected CTest | Exit 0; 1/1 passed | `build-trigger-final.log`, `ctest-trigger-final.log` |
| `build-presentation/Debug/PhlosionTriggerTests.exe` | Exit 0; 7/7 groups passed | `trigger-groups-final.log` |
| `dumpbin /dependents` final trigger executable | Exit 0; only MSVC/UCRT/KERNEL32 imports | `trigger-imports.log` |
| Generated warning/link settings | /W4, /WX; Collision, GLM, Jolt and platform libs | `compile-link-settings.log` |
| `git diff --check` | Exit 0; repository CRLF conversion notices only | `diff-check.log` |
| Final trigger executable SHA-256 | Recorded | `trigger-executable-hash.log` |

The standalone executable imports only `MSVCP140D.dll`, `VCRUNTIME140D.dll`,
`VCRUNTIME140_1D.dll`, `ucrtbased.dll`, and `KERNEL32.dll`; it has no SDL or graphics
API imports. No test or build failures occurred in this bundle. Dense-history
assertions were expanded before the full build to verify independent character
reconciliation and that stale snapshots stop producing events after one Exit.
While the existing fixture harnesses ran, two final test-only assertions were
added for creation inside a sensor removed before the first sample, and teleport
across sensors with empty endpoints. The affected target was rebuilt and rerun;
implementation code did not change after the full build.

## Contract coverage

Seven groups assert independently authored, complete membership/event lists,
metadata and order:

1. Empty samples, first Enter, repeated Stay, movement Exit, re-entry, multiple
   sensors and mixed event kinds sorted by ID rather than kind.
2. Precise capsule/box corner AABB false positives, a 0.003-unit positive gap
   proving no controller-padding expansion, a rotated/translated box, both sides
   of a transformed two-triangle mesh with deduplication, zero-height sphere,
   and exclusion of overlapping nonsensors/characters.
3. Independent movement/trigger masks and ignore IDs, bit 31 and zero mask,
   filter reconciliation, stale/foreign ignore, maximum uint64 user data, copied
   output/history ownership, and unchanged blocking movement filtering.
4. Teleport into/out/between sensors, net endpoint semantics for unsampled
   intermediate teleports, rejected blocked/nonfinite teleport, invalid and
   out-of-envelope movement preserving history, and unchanged sampled character
   position/velocity/support plus world counts and authored geometry.
5. Sixteen concurrent memberships at a configured sixteen-collider limit;
   removal/replacement creates 32 sorted Exit(old)/Enter(new) events with original
   metadata, then only 16 Stays. Other characters reconcile independently.
   Old caller output survives deletion; character capacity reuse and world clear
   reset history. Repeated create/use/remove/destroy lifetimes pass.
6. Zero/stale/foreign IDs and wrong-thread calls preserve sentinel output and
   retained history. A separate world works on its own thread. A compile-time
   check verifies output publication is a nonthrowing move assignment.
7. A 100-unit/s character passes through a 0.0002-unit sensor between endpoint
   samples and deliberately produces no event; sampling within it produces Enter.
   Five authored step/sample/teleport/filter/removal sequences match explicit
   expected lists using each world's own IDs.

## Limits and review handoff

Samples report endpoint membership only. Fast movement and teleports can cross
thin sensors without events. The guide recommends sampling after complete fixed
ticks and suitable sensor volumes, without promising continuous crossings.
Mesh sensors remain double-sided surfaces rather than closed solid volumes.
Retained membership is bounded by collider count per character; events can reach
previous+current counts. These are not total-process memory budgets.

This CPU pass does not qualify native walkable/runtime/editor integration or
character visual parity. Camera/actions, gameplay dispatch, host scheduling,
dynamic bodies and continuous path events remain outside scope. No architecture
conflict or implementation blocker was found. Root can now review the actual
diff/evidence and record acceptance; this worker does not self-approve the pass.
