# M2-A acceptance review

Status: accepted for the bounded M2-A CPU collision contract.
Reviewer: Astra. Baseline: `08f57919785671d1120928b74fe56cbafad5265a`.
Date: 2026-09-20. No staging, commit or push in this pass.

## Delivered boundary

The first M2 pass adds a reusable CPU collision-query world under
`src/engine/collision`, exposed as `Phlosion::Collision`. Jolt 5.4.0#0 is selected
by the unchanged vcpkg baseline; only the collision target links it. Public
headers expose Phlosion/GLM types and hide Jolt behind a PImpl.

Accepted scope is static boxes and owned indexed triangle meshes, rigid
transforms, opaque IDs, layers/sensors/ignore filtering, nearest ray and upright
capsule casts, and exact capsule overlap queries. It does not implement a
character controller, trigger events, camera/input behavior or a walkable host.

## Source and architecture review

Root inspected the complete public header, implementation, test executable,
guide, CMake/manifest diff and retained worker evidence. Review covered:

- Pre-call finite/range/count validation, quaternion normalization, Jolt cooking
  errors, preserved outputs/world on rejected operations, and capacity reuse.
- Never-reused cross-world IDs, metadata ownership, copied mesh data,
  owner-thread checks, repeated/multiple worlds and body-before-system teardown.
- Exclusive Jolt runtime ownership and explicit limits for independently
  managed integrations, separate DLL copies, external global mutation and OOM.
- Query filtering shared across all three operations, double-sided triangles,
  fraction/contact/normal conventions, exact overlap and endpoint behavior.
- Private static Jolt linkage, engine warning settings on the new library and
  tests, no graphics dependencies, and unchanged Core/Engine aggregate linkage.
- Actual Jolt 5.4.0 `MeshShape.cpp` rejects quantization-degenerate triangles
  during creation; this implementation does not call the sanitizing constructors.

No production defect was found in review. One consolidated correction added
independent world-space contact checks for nonidentity mesh transforms, plus
wrong-thread rejection and a separate world owned by another thread. Root
inspected the final test source and verbose passing evidence. The transformed
plane checks use independent analytic contacts; thread checks propagate worker
exceptions after joining and verify continued owner-world use. Both passed
without production-code or tolerance changes. No acceptance findings remain.

## Evidence inspected

Raw local evidence is retained under `artifacts/m2-a/` (ignored build artifacts):

| Check | Evidence | Result |
| --- | --- | --- |
| Pinned dependency and target configuration | `configure-initial.log`, `configure.log`, `Jolt-package.log`, `build-settings.log` | PASS |
| Full enabled VS2026 Debug build | `build-all.log` | Exit 0 |
| Full CPU/source/fixture harness regression | `ctest-all.log` | 10/10 PASS, 292.07 seconds |
| Initial collision contracts | `run-collision-3.log` | Seven groups PASS |
| Final affected rebuild and expanded collision contracts | `build-collision-review-1.log`, `ctest-collision-review-1.log` | Exit 0; eight groups PASS, 0.09 seconds |
| Actual `/W4 /WX` compile flags | `collision-compile-command.log`, `test-compile-command.log` | Confirmed |
| Standalone executable linkage/imports | `link-command.log`, `dependents.log`, `dependents-review.log` | Collision/GLM/Jolt plus standard platform/runtime libraries only |
| Resolved Jolt CPU requirements | `cpu-probe.log`, `JoltCpuProbe.cpp`, `JoltConfig.cmake`, `Jolt-{Debug,Release}-compiler.log` | Version 5.4.0, MSVC x64 SSE2 baseline; no AVX/SSE4 feature additions |

The final test compiler command (`test-compile-command-review.log`) also retains
`/W4 /WX`. The full regression includes the existing 32 Engine C++ contracts and all
existing harnesses. It is an incremental build of all enabled targets, not a
clean-room build. The initial Jolt cache-size assertion was fixed by the worker
before review; the report preserves the failed and successful evidence.
The final correction changes only collision tests and documentation; evidence
for the unchanged production code and nine other test entries was reused.

## Routing and limits

The native `astra_flash_builder` attempt returned HTTP 402 Insufficient Balance
before editing. Astra implemented through the user's explicitly authorized
fallback; root Astra retained planning and acceptance. No routing/provider
settings were changed. The implementation worker did not self-approve.

Qualification is CPU-only on this Windows x64/MSVC host. No renderer, runtime
scene or editor behavior changed; no GPU matrix or playable-scene qualification
is claimed. Cross-platform CPU requirements, comparative performance, production
content and cross-platform bitwise determinism remain unqualified.

M2 remains in progress. Its next bounded pass should implement fixed-step
kinematic movement with grounding, sliding, slope/step policy and recovery
tests, followed by camera/input and native runtime/editor scene qualification.

## Authorized commit follow-up

On 2026-09-21 the user requested committing this outstanding accepted pass.
Root rechecked the change scope, CMake/dependency and documentation diff, prior
source acceptance and retained full-regression evidence. The current collision
target rebuilt successfully and its eight contract groups passed again; logs
are `build-collision-precommit.log` and `ctest-collision-precommit.log` under
`artifacts/m2-a/`. No implementation change or broader testing was needed.
The commit contains only the accepted collision module, tests, integration and
documentation. No Wes research/assets, game changes or push are included.
