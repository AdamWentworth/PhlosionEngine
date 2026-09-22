# M2-B acceptance review

Status: accepted for the bounded CPU kinematic-character contract.
Reviewer: root Astra. Date: 2026-09-22.
Baseline: clean main `b570f4e2d5ee2decf13a1d8ae50526347a4ea681`.
No staging, commit or push requested or performed.

## Accepted behavior

The existing opt-in Collision library owns opaque character lifetimes and the
pinned Jolt 5.4.0 virtual-character solver. The public API supports fixed-tick
analog walk/run, gravity/terminal fall speed, support metadata, wall/corner
sliding, slope/step/floor-following policy, shallow penetration recovery and
precise safe teleport. Characters have no physics body or mutual collision and
are absent from static queries. Sensors never block or support them.

The controller remains CPU-only and independent of Engine/Core/render/platform
targets. Public types expose Phlosion/GLM values. Existing query signatures and
tests are unchanged; `clear` now explicitly destroys characters before colliders.

## Source and specification review

Root reviewed every changed production definition, the shared identity-header
extraction, the ten test groups, narrow CMake integration, usage contracts and
retained evidence. Review covered:

- Finite settings and arithmetic, robust analog normalization, capacity and
  unique identity, owner-thread checks, world destruction order and exceptions.
- Geometric-center/padding convention, supporting volume, double-sided meshes,
  slope limits, grounded-only stair/snap attempts and actual reported velocity.
- Precise teleport rejection and preserved prior state/output on invalid calls.
- Contact refresh before removed bodies are destroyed, live identity and full
  body-generation validation, rollback metadata and separate gravity state.
- Analytic assertions for distances, gravity, clearances and ramp heights;
  blocked stairs/ceilings, disabled policies, filters, repeated ticks/sequences,
  lifecycle/thread tests, 4096-character capacity and numeric recovery rollback.
- Compiler/link dependencies and unchanged pinned dependency/aggregate boundaries.

Root spot-checked the installed Jolt 5.4.0 source: ExtendedUpdate applies steep
slope cancellation, and CharacterBase::RestoreState explicitly clears ground
user data. The implementation's live metadata fallback addresses that omission.
The approved [1,89]-degree settings range avoids Jolt's near-zero-angle special
case. These are explicit settings/solver limits, not widened test tolerances.

Two defects were caught and fixed before handoff: requested velocity was initially
reported as actual movement, and rollback initially lost copied support identity.
The corresponding failed assertions are preserved and pass with their original
tolerances. No production defect or remaining acceptance finding was identified
by root review. No additional correction cycle or duplicate full test run was
necessary; existing evidence covers the final production code.

## Evidence inspected

Full local logs are retained under ignored `artifacts/m2-b/`.

| Check | Evidence | Result |
| --- | --- | --- |
| Existing VS2026 configuration | `configure-01.log` | Exit 0 |
| Full enabled Debug build | `build-all-01.log` | Exit 0 |
| Full CTest suite | `ctest-all-01.log` | 11/11 passed, 312.42 seconds |
| Final test-only additions, all-target build | `build-all-02.log` | Exit 0 |
| Final affected character test | `ctest-character-final.log` | Passed, 0.42 seconds |
| Direct final character executable | `character-groups-final.log` | 10/10 groups passed |
| Warning/link configuration | `compile-link-settings.log` | /W4 /WX; Collision, GLM, Jolt and platform libraries |
| Executable imports | `character-imports.log` | C++/Windows runtimes only; no SDL or graphics imports |
| Initial negative and fix runs | `ctest-targets-01.log` through `ctest-targets-04.log` | Failures reproduced and fixed without weakening assertions |

The final test-only additions cover teleport filters and stale ignored collider
IDs. The affected executable was rebuilt/retested; all unchanged production code
and other tests retain the full-suite evidence. This is an incremental build of
all enabled targets on the local Windows/MSVC host, not clean-machine or
cross-platform qualification. Root documentation integration changes no code.

## Routing and next boundary

Native Flash child `/root/cyberpunk_m2_character` returned HTTP 402 before edits
and was interrupted. Root disclosed and reused the prior user-authorized Astra
fallback. Explicit Astra child `/root/cyberpunk_m2_character_astra` implemented
the bounded bundle and was interrupted after completion; root retained acceptance.
No provider configuration changed, and no successful Flash inference is claimed
for this pass.

M2 remains incomplete. The next work is trigger event state, camera/actions and
host integration, followed by public walkable runtime/editor qualification and
the game's Outskirt Stand gate. Native OpenGL/Vulkan/D3D12, 16:9/21:9, input focus,
controller hotplug and game-scale recovery have not been qualified by this pass.
No visual behavior changed and no new GPU evidence is claimed. Automatic recovery
is bounded best effort; arbitrary trapped geometry requires an explicit safe
destination. Frame-time, memory budgets and cross-platform determinism remain
unqualified.

## Authorized commit follow-up

On 2026-09-22 the user requested committing the accepted work and continuing
with the next pass. Root rechecked the full workspace scope and diff whitespace;
the production/test patch is unchanged from this acceptance and its retained
passing evidence. The commit contains accepted M2-B and documentation only.
No push was requested or performed.
