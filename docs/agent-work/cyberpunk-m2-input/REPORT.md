# M2-D implementation report

Date: 2026-09-22. Baseline: `6a72dedc7e5aff64cf9bbf92e7114eaa2981db84`.
Ready for root review; implementation and qualification are complete. Root review
determines acceptance. No commit, staging, push, dependency/provider change or child agent.

## Delivered

- `Phlosion::Input` / `engine_input`, independent of SDL/rendering: owned named
  actions, typed bindings, digital/analog composition, rescaled axial/radial
  deadzones, explicit frame publication and edge latching, transactional
  validation/rebinding, bounded capacity, opaque nonreused controller IDs,
  explicit selection, focus/capture gates and per-control neutral rearming.
- `SdlActionInput` in `engine_platform`: balanced subsystem reference and owned
  controller handles, mapped-device discovery/hotplug/remapping, event-value
  snapshots preserving short taps, actual polling, instance-ID mapping,
  missed-disconnect cleanup and window-filtered keyboard/mouse/focus translation
  using the existing key mapper.
- Separate pure and real-SDL virtual-device contract executables, 120-second
  timeouts and normal `/W4 /WX` project settings; narrow CMake wiring.
- [INPUT_ACTIONS guide](../../INPUT_ACTIONS.md) covering action semantics,
  lifetimes, explicit selection, frame cadence and integration examples.

No edits to `InputEvent`, `SdlKeyMap`, legacy application/editor event pumps,
Camera3D, game/plugin ABI, renderer/backend code or dependencies. Shared index,
roadmap, boundary and verification documentation is root-owned.

## Notable contract choices

- `define`/`rebind` publish only the affected action immediately, rebased without
  edges. Ordinary physical changes publish only at `advanceFrame()`.
- Gate loss discards pending presses, latches releases and clears keyboard/mouse.
  Each controller button/axis rearms independently only while both gates are
  open. The neutral axis sample that rearms contributes zero even with a zero
  binding deadzone. Later samples use normal deadzone processing.
- Query/mutation calls, including SDL adapter construction, enforce owner-thread
  use via `logic_error`. Destruction is an owner-thread lifetime precondition and
  terminates on violation; it is not a throwing destructor.
- SDL construction initializes its subsystem reference; explicit `refresh()`
  performs startup discovery. Refresh preserves per-device progress and reports
  all open/capacity errors. `poll()` reconciles known devices and missed removals.
- Newly opened devices initialize their complete cached snapshot from SDL before
  event ingestion. Host cadence is drain/forward events, poll, then publish.
  Forwarding a queued duplicate after a published poll does not duplicate an
  edge. Delaying a whole history of transitions can legitimately replay edges;
  the adapter never drains or discards host events.
- Callers leave adapter-owned registered IDs to the adapter, destroy it before
  the action owner and before external `SDL_Quit`/existing `Window` shutdown.

## Verification and evidence

Raw command files, native logs, exit codes, compiler command records, executable
imports and SHA-256 identities are retained in ignored `artifacts/m2-d/`.
No RTK filtering was used; native output was redirected in full and bounded tails
were inspected. Initial debug-loop failures remain available.

1. `configure.*`: existing `build-presentation` configured with warnings enabled
   and warnings-as-errors. Dependencies stayed on the installed versions.
2. `input-build-01` found one `/WX` test-only SDL enum narrowing warning; fixed
   with an explicit `Uint8` conversion. `input-build-02` passed.
3. `input-tests-01` / `sdl-tests-02` retained the initial SDL expectation failure:
   earlier queued poll-generated transitions had deliberately not been forwarded
   before isolating disconnect. The test now finishes that history first. The
   adapter correctly preserved it. Genuine queued down/up taps and deferred
   duplicate press/release observations have dedicated passing regressions.
4. `input-build-05` / `input-tests-02`: both suites passed, SDL runtime **2.32.10**.
   `final-build` and `final-input-tests` additionally passed after strengthening
   radial math, destroyed-instance identity and retained-device capacity tests.
5. `full-build` and `full-ctest`: all enabled Debug targets built; all **14/14**
   CTest entries passed in **299.96 seconds**. This normal build's existing
   aggregate `PhlosionEngineTests` target bypasses `phlosion_apply_settings` and
   had previously compiled at `/W1 /WX-`; new and affected targets were `/W4 /WX`.
6. Local `global-wx-configure` temporarily adds `/WX` to the existing
   `CMAKE_CXX_FLAGS` without tracked build-policy changes. `global-wx-build`
   succeeded, including recompiling the legacy aggregate test sources. Captured
   compiler records confirm all **37 enabled compiled targets / 177 commands**
   have `/WX`; all except legacy aggregate tests use `/W4`, with that target
   retaining its existing `/W1`. `all-target-compiler-commands.json` holds raw
   records, `build-target-compiler-flags.json` and `compiler-flags-summary.json`
   summarize them. The CMake compiler-identification probe is excluded because
   it is a configure probe, not an enabled project target.
7. `global-wx-ctest`: all **14/14** entries passed on the `/WX`-qualified binaries
   in **355.57 seconds**. `restore-configure` then restored the original local
   `CMAKE_CXX_FLAGS` (`/DWIN32 /D_WINDOWS /EHsc`), exit 0; the normal warning options
   remain enabled. No permanent global policy change. A final fixture review
   made the independent SDL handle RAII-owned for assertion-failure cleanup;
   `review-build`, `review-pure-tests` and `review-sdl-tests` passed afterward.
8. `final-pure-imports` and `final-sdl-imports`, `dumpbin /DEPENDENTS`, exit 0:
   pure test imports only `MSVCP140D`, `VCRUNTIME140D`, `VCRUNTIME140_1D`,
   `ucrtbased`, `KERNEL32`. SDL test adds only `SDL2d.dll`. Neither imports a
   graphics backend; pure input does not import SDL. These are executable import
   boundaries, not a claim that SDL itself has no optional graphics facilities.
9. `diff-check` passed; `artifact-ignore` confirms evidence remains ignored.

Pure coverage includes keyboard/mouse hold, repeat and short taps, OR/opposing
and normalized diagonal contributions, signed/trigger/radial math and inversion,
frame cadence and rebasing exceptions, malformed values and unchanged failures,
capacity extrema, controller selection/removal/foreign/nonreuse IDs, independent
gates and per-control recovery, all owner-thread operations and separate threaded
instances. SDL coverage uses actual mapped virtual devices for all **15 standard
buttons and six axes**, both signed endpoints and intermediate/trigger values,
queued taps, two-device selection, add/remove and missed-removal polling,
disconnection/teardown releases, mapping reconciliation, unmapped-device filtering,
window filtering, capacity errors retaining devices, no queue consumption, and
independently held SDL references/handles surviving adapter teardown. Virtual
fixtures initialize raw joystick triggers to -32768 at rest and detach only their
own devices.

## Remaining product boundary

Physical controllers, drivers, native runtime/editor integration and exploration
camera/character behavior are unverified. Virtual-device results establish this
reusable input dependency only; they do not claim the full M2 controller/scene
exit gate. Rendering and hosts are unchanged, so no new GPU parity matrix was
required or claimed. Root retains architecture review and acceptance.
