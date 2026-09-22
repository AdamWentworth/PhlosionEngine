# M2-C acceptance review

Status: accepted for explicit discrete character sensor events.
Reviewer: root Astra. Date: 2026-09-22.
Baseline: clean main `21672db1fef0a538db234b9c75386541d9af8aca`.
The user authorized committing M2-B and continuing; M2-C remains uncommitted.
No push performed.

## Accepted contract and patch review

`CollisionWorld::sampleCharacterTriggers` compares precise sensor-only capsule
overlaps against the character's previous successful membership. It publishes
current overlaps and one Enter/Stay/Exit per ID in the union, sorted by ID.
Filters are independent of movement; all metadata is copied, including removed
sensor metadata needed for Exit. Each character owns only its last membership.
There is no callback, event queue, clock or implicit movement/teleport sampling.

Root inspected the complete implementation/header/CMake diff, all seven test
groups and documentation against the captured baseline and PLAN. Review covered:

- Reuse of the accepted precise sensor-only overlap query, current geometric
  capsule rather than controller padding, and sorted/deduplicated membership.
- Merge boundary conditions for empty/old/new/common IDs, full metadata and
  ordering across event kinds, independent movement/trigger filters.
- Candidate output and copied next membership built before publication; output
  move and history swap do not allocate. A test static assertion verifies the
  output type remains nothrow move-assignable. No public history alias exists.
- Invalid-ID and wrong-thread rejection before history mutation, sensor removal
  snapshots, collider/character slot reuse, clear/destruction and independent
  character/world histories. Existing movement/query methods are unchanged.
- Authored expected events and full metadata assertions for transformed boxes,
  both sides of a transformed mesh, exact corner misses, no padding expansion,
  spheres, filters, teleport/failure/purity, lifetime/threads and repeatability.
- Sixteen-sensor capacity replaced by sixteen new sensors produces 32 sorted
  old Exit/new Enter events, then sixteen Stays with no stale-history growth.
- Standalone Collision-only CMake target, /W4 /WX, 120-second timeout and no
  new package, platform/render aggregate or game dependency.

No production defect, weakened assertion, scope violation or missing in-scope
verification was found. No correction cycle was needed. Root reused complete
worker verification evidence rather than redundantly rerunning the suite.

## Evidence inspected

Raw native logs are retained under ignored `artifacts/m2-c/`:

| Check | Evidence | Result |
| --- | --- | --- |
| Configure existing VS2026 build | `configure-01.log` | Exit 0 |
| Targeted collision/character/trigger build and CTest | `build-targets-01.log`, `ctest-targets-01.log` | Exit 0, 3/3 passed |
| Full enabled Debug build | `build-all-01.log` | Exit 0 |
| Full CTest suite | `ctest-all-01.log` | 12/12 passed, 297.60 seconds |
| Final test-only additions | `build-trigger-final.log`, `ctest-trigger-final.log` | Exit 0, affected test passed in 0.09 seconds |
| Direct final trigger executable | `trigger-groups-final.log` | Seven groups passed |
| Compiler/link settings | `compile-link-settings.log` | /W4 /WX; Collision/GLM/Jolt plus standard platform libraries |
| Final executable imports | `trigger-imports.log` | Windows/C++ runtimes only; no SDL/graphics imports |

Final additions test an unsampled sensor removed before the first call and
teleport across sensors with empty endpoints. They changed tests only; the
affected executable was rebuilt/retested. The full suite covers unchanged final
production code. There were no build/test failures during this bundle. This is
an incremental local Windows/MSVC Debug qualification, not clean-machine,
cross-platform or performance qualification.

## Routing and remaining boundaries

The related Astra child `/root/cyberpunk_m2_character_astra` implemented this
bundle under the previously user-authorized/disclosed fallback. Root reused the
recent native Flash HTTP 402 evidence; no new paid smoke or provider setting
change was needed. Root retained acceptance and interrupted the worker after
completion. No successful Flash inference is claimed for this pass.

This is discrete endpoint membership. A character moving or teleporting entirely
through a sensor between samples can produce no event; explicit tests establish
that limitation. Callers schedule samples and author appropriate sensor sizes.
Mesh sensors are surfaces, not closed-volume solid occupancy. Destruction drops
character history; callers wanting a final exit batch sample with a zero mask
before destroying it. Count limits do not establish a process-memory budget.

M2 remains incomplete. Named actions/controller handling, exploration camera,
host scheduling/integration and native OpenGL/Vulkan/D3D12 runtime/editor
qualification remain open, including 16:9/21:9, hotplug/focus recovery and the
game's Outskirt Stand scene. No renderer or host behavior changed in M2-C and
no new GPU qualification is claimed.

## Authorized commit follow-up

The user subsequently requested committing accepted M2-C and continuing on
2026-09-22. Root rechecked workspace scope and whitespace; production/tests
remain unchanged from the reviewed passing evidence. The commit includes M2-C
and documentation only. No push requested or performed.
