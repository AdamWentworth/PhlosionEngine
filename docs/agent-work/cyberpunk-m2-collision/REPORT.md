# M2-A implementation worker report

Status: `ready_for_review`; root acceptance pending.

Worker: user-authorized Astra fallback after the Flash worker returned HTTP 402
before edits. No recursive delegation, provider changes, staging, commits or
pushes. Baseline: `08f57919785671d1120928b74fe56cbafad5265a`.

## Implemented scope

- `src/engine/collision/CollisionWorld.h/.cpp`: independent PImpl static-world
  library; real Jolt boxes/indexed meshes, caller geometry ownership, monotonic
  opaque IDs, metadata, one process runtime, owner-thread contract, capacity
  checks, transform/input validation, normalized quaternions, consistent
  layer/sensor/ignore filtering, nearest ray/capsule casts and precise unique
  capsule overlaps. Meshes are double-sided for all queries.
- `CMakeLists.txt`: `engine_collision` / `Phlosion::Collision`, with private
  Jolt linkage and public GLM, engine warning settings; separate
  `PhlosionCollisionTests` / `PhlosionCollisionContracts` linked only to
  `Phlosion::Collision`. Core and Engine umbrella link sets remain unchanged.
- `vcpkg.json`: adds only `joltphysics`; existing baseline/overrides unchanged.
- `tests/TestCollisionWorld.cpp`: eight CPU contract groups using authored
  literal geometry and actual Jolt narrow-phase queries.
- `docs/COLLISION_WORLD.md`: public API, numeric envelope, lifetime, contacts,
  error semantics, CPU/dependency qualification and deferred scope.
- This report. Root-owned readiness, plan, checkpoint and verification changes
  were preserved and are not part of worker ownership.

The supported numeric envelope is explicit: coordinates/displacement components
up to absolute 1e6, positive dimensions at least 1e-4, displacement length at
least 1e-4, cylinder half-height zero allowed. Input outside it returns a
diagnostic. This prevents unsupported extreme float inputs before Jolt. Ordinary
malformed inputs, missing/stale/foreign IDs and full capacity do not assert or
throw. Constructor configuration, owner-thread violations and resource failures
have the documented C++/allocator limitations.

## Commands and evidence

Working directory: `D:/Projects/Phlosion/PhlosionEngine`. Native PowerShell;
complete output redirected to ignored `artifacts/m2-a/`, with bounded tails
shown interactively. No RTK filtering was used.

| Command / check | Exit | Evidence |
| --- | --- | --- |
| `cmake -S . -B build-presentation` (initial dependency installation) | 0 | `configure-initial.log` |
| `cmake -S . -B build-presentation` (final target integration) | 0 | `configure.log` |
| `cmake --build build-presentation --config Debug --target PhlosionCollisionTests --parallel 8` | 0 | `build-collision-1.log`, `build-collision-2.log`, `build-collision-3.log` |
| `build-presentation/Debug/PhlosionCollisionTests.exe` (fixed implementation) | 0 | `run-collision-3.log`, seven PASS groups |
| `cmake --build build-presentation --config Debug --parallel 8` | 0 | `build-all.log` |
| `ctest --test-dir build-presentation -C Debug --output-on-failure` | 0 | `ctest-all.log`: 10/10 pass, 292.07 seconds |
| `cmake --build build-presentation --config Debug --target PhlosionCollisionTests --parallel 8` (review correction) | 0 | `build-collision-review-1.log` |
| `ctest --test-dir build-presentation -C Debug -R '^PhlosionCollisionContracts$' --verbose --output-on-failure` (review correction) | 0 | `ctest-collision-review-1.log`: 1/1 pass, all eight groups, 0.09 seconds |
| VS 2026 `dumpbin.exe /DEPENDENTS build-presentation/Debug/PhlosionCollisionTests.exe` | 0 | `dependents.log` |
| MSVC `/c /std:c++20 /MDd /W4 /WX` Jolt header/feature macro probe | 0 | `cpu-probe.log`, `JoltCpuProbe.cpp` |
| `git diff --check` | 0 | No whitespace errors; usual Git LF/CRLF notices only |

The first runtime test failed at a Jolt assertion: unused simulation caches
still require at least four hash buckets. Initial `ctest-collision-1.log` exited
8, and diagnostic `run-collision-2.log` records process exit -2147483645. Fixed
the internal capacities from one to four; all subsequent collision groups pass.
No query/test thresholds were weakened. Those earlier logs remain available.

The existing build retains VS 2026, x64-windows, shared installation at
`build/vcpkg_installed`, all four public fixture families, native editor, tests,
warnings and warnings-as-errors enabled (`build-settings.log`). Actual
`collision-compile-command.log` and `test-compile-command.log` contain `/W4 /WX`.
The full build is incremental against the existing qualified build directory;
it compiled new collision code/tests and built all enabled targets.

## Dependency and CPU proof

Installed package: Jolt 5.4.0#0, package ABI
`af45389283df0ee259f327d314c729712ccd661660a07156457ab4ce57e78307`.
`Jolt-package.log`, `JoltConfig.cmake`, `Jolt-Debug-CMakeCache.log` and
`Jolt-{Debug,Release}-compiler.log` preserve resolved package/build evidence.
Jolt is static, single precision, without renderer/profiler/sample features.
No baseline or unrelated dependency upgrade occurred.

The installed Jolt export and actual Ninja/MSVC flags add no AVX/SSE4 features.
The compile-time probe verifies version 5.4.0, 64-bit SSE baseline, and absence
of AVX/AVX2/AVX512/SSE4/F16C/FMA/LZCNT/TZCNT feature macros. Therefore this
resolved Windows x64 build uses the MSVC x64 SSE2 baseline; an upstream/default
AVX2 requirement must not be inferred from `USE_AVX2=ON` in the cache. The
upstream MSVC feature branch checks a VS-platform variable absent from the
vcpkg Ninja build. Other toolchains/triplets/CPUs remain unqualified.

The executable's actual linker command (`link-command.log`) includes
`engine_collision.lib`, `glm.lib`, `Jolt.lib` and standard Windows defaults.
It contains no SDL, OpenGL, Vulkan, D3D12, editor, render, platform or Core target.
`dumpbin` reports exactly these imported DLLs:

- `MSVCP140D.dll`
- `VCRUNTIME140D.dll`
- `VCRUNTIME140_1D.dll`
- `ucrtbased.dll`
- `KERNEL32.dll`

The standalone tests create no graphics context. Top-level engine configure
still discovers the engine's existing dependencies, as allowed in PLAN.

## Coverage and remaining boundaries

The eight groups cover empty and valid misses; nearest analytic ray/sweep
fractions/points/normals; thin walls; rotated and quaternion-normalized boxes;
floor/ramp meshes and both sides; ray/sweep endpoints; sphere capsules; initial
penetration; duplicate mesh contacts; precise box-corner and triangle AABB false
positives; 48 combinations of sensor policy, masks including bit 31/zero, and
ignored IDs across every query; copied metadata; two simultaneous worlds;
20 create/use/remove/clear/destroy cycles; ID nonreuse; capacity/reuse; input
geometry lifetime; NaN/infinity; bad transforms/extents/layers/capsules/policies;
empty/nontriangle/out-of-range/degenerate/oversized mesh input; preserved
sentinel outputs and previous collision results; lower/upper configuration
bounds; and six identical authored floor/wall/ramp/sensor scripted runs.

## Astra review correction

Added a floor mesh translated to (3, 2, -1) and rotated 45 degrees about Z.
The expected plane normal is (-sqrt(1/2), sqrt(1/2), 0). A six-unit ray from
three units along the normal contacts at fraction 1/2 and the translated
origin. For an upright radius-1/2, cylinder-half-height-1/2 capsule, plane support
distance is `1/2 + sqrt(1/2)/2`, which independently determines the expected
sweep fraction and contact point (2.75, 1.75, -1). Tests check these world-space
fractions, points and normals, one actual overlap and a nearby separated case.

Added wrong-thread count/read and ray-query rejection checks, catching
`std::logic_error` and confirming untouched output. That worker thread also
constructs, queries and destroys its own world while the owner world remains
alive. The tests reject cross-world IDs on both worlds and confirm owner-world
queries still work after joining the thread. No wrong-thread destruction is
attempted.

The correction changes tests and documentation only. Both added cases passed
without implementation fixes or changed tolerances. The affected `/WX` target
was rebuilt and collision CTest rerun with complete verbose logs above;
`test-compile-command-review.log` preserves the current `/W4 /WX` compiler
command. The earlier full 10/10 CTest run remains evidence for unchanged targets.
The rebuilt executable's dependencies are rechecked in `dependents-review.log`.

No controller, camera, trigger event machine, transform updates, game/host/editor
integration or renderer source changes were made. No visual matrix/GPU result,
complete M2, playable-scene result, comparative performance or cross-platform
bitwise determinism is claimed. Runtime globals cannot be independently managed
by another Jolt integration or shared through separate DLL copies; full lifetime
details and owner-thread requirements are documented. External global mutation
and out-of-memory recovery are not qualified by the contract executable.

Root performs acceptance. Worker completion is ready-for-review only.
