# M2-D acceptance: named actions and SDL controller input

Accepted by Astra on 2026-09-22 from baseline
`6a72dedc7e5aff64cf9bbf92e7114eaa2981db84` (`Add discrete character trigger events`).
M2-C was committed as requested before this pass. M2-D is complete and remains
uncommitted for review; no push was made.

## Delivered contract

- `Phlosion::Input` owns SDL-free named Button/Axis1D/Axis2D actions, typed
  bindings, bounded capacities, transactional validation/rebinding and explicit
  frame publication. Repeated queries preserve edges, short taps retain both
  transitions, digital combinations normalize and analog deadzones rescale.
- Controller snapshots carry opaque nonreused identities, explicit selection
  and validated complete physical state. Disconnects remove contributions;
  focus/enable changes clear stale presses and require per-control neutral
  observations before rearming controller input.
- The opt-in Platform adapter uses the existing SDL key map, owns one balanced
  subsystem reference and its opened handles, discovers mapped devices and
  reconciles hotplug/remapping. Event snapshots retain queued taps, polling
  reads actual mapped values, and the host retains event-queue ownership.
- Existing InputEvent, application/editor event pumps, camera, renderer and
  plugin ABI are unchanged. The guide documents lifetime, window filtering,
  supported event/poll cadence, per-device refresh progress and remaining scope.

## Review and verification

Root reviewed the actual public interfaces, action implementation, SDL adapter,
all test groups, target links and consumer guide. Review covered validation
before mutation, unaffected rebind state, edge publication, independent gates,
per-control rearm, ID lifetimes, selected removal, SDL handle/reference balance,
queued event/poll behavior, capacities, thread ownership and cleanup. No
outstanding findings remain.

The enabled VS2026 Debug build and all fourteen CTest entries passed in 299.96
seconds. Six pure test groups and three SDL groups exercise the new APIs with
real virtual mapped devices, all 15 buttons/six axes, queued taps, duplicate
observations after polling, selection/removal and independent SDL lifetime.
SDL runtime is 2.32.10. No window or GPU is created by these new suites.

The compiler audit found an existing warning-policy exception: the aggregate
`PhlosionEngineTests` target bypasses the common settings helper and ordinarily
uses `/W1 /WX-`. A temporary global `/WX` rebuild and second full CTest run passed
(355.57 seconds). Actual records cover 37 compiled targets / 177 commands, all
with `/WX`; only the legacy aggregate target uses `/W1`, all others `/W4`.
The local `CMAKE_CXX_FLAGS` was restored to `/DWIN32 /D_WINDOWS /EHsc` afterwards.
No unrelated target-policy change was made. A final test-only RAII cleanup
passed another full build and both input executables directly.

Root inspected raw exit codes, successful logs, compiler summaries, restored
cache options and import reports, and compared all eleven hashes in
`artifacts/m2-d/verified-file-hashes.json` with the final files. The pure test
imports only Windows/C++ runtimes. The adapter test additionally imports
SDL2d.dll, with no graphics backend imports. Retained evidence includes:

- `full-build.log`, `full-ctest.log`, `global-wx-build.log`, `global-wx-ctest.log`;
- `compiler-flags-summary.json`, `all-target-compiler-commands.json`,
  `build-target-compiler-flags.json`, `restored-flags.txt`;
- `review-build.log`, `review-pure-tests.log`, `review-sdl-tests.log`;
- `final-pure-imports.log`, `final-sdl-imports.log`, `verified-file-hashes.json`.

Command/exit sidecars and failed development runs remain under ignored
`artifacts/m2-d/`; they are not included in source changes. Initial test enum
narrowing and isolated-disconnect expectations were corrected before the
successful runs. See the [worker report](REPORT.md) for the detailed history.

## Scope and routing

This accepts the reusable input foundation, not complete M2 exploration. Physical
controllers, runtime/editor input routing, mouse-look controls, camera movement
and walkable-scene qualification remain. Context layers, capture UI, persistence
and haptics are outside this pass. Rendering did not change; no new native GPU
claim is made. Later scene integration still requires all three native APIs.

Implementation used the already disclosed, user-authorized Astra fallback after
the Flash HTTP 402 balance failure. Root retained architecture and acceptance;
no provider settings changed. The completed worker was explicitly interrupted.

Commit follow-up: the user subsequently authorized committing this accepted
pass and proceeding. Root checked the final scope against acceptance; no source
or test changes required another run. The authorized commit includes M2-D and
its documentation only, with no push.
