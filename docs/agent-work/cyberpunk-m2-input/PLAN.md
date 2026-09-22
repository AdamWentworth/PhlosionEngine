# M2-D: named actions and SDL controller input

Date: 2026-09-22. Baseline: clean main
`6a72dedc7e5aff64cf9bbf92e7114eaa2981db84` (M2-C committed as requested).
User requests the next M2 pass. Implement the reusable input dependency before
camera and host integration. This pass must deliver actual SDL-backed controller
handling plus an independently testable action layer, not only planned types.
No automatic commit/push of this new pass.

Outcome: accepted by Astra on 2026-09-22 after source/test review and verified
build, CTest, compiler-option and import evidence. See [ACCEPTANCE](ACCEPTANCE.md)
and [CHECKPOINT](CHECKPOINT.md). M2-D remains uncommitted for user review.

## Evidence and architecture

Existing `InputEvent.h` exposes SDL-free keyboard/mouse values and crosses the
game/editor boundary. `SdlKeyMap.cpp` is in engine_platform. Application and
PhlosionEditorMain have separate legacy translators. No named actions or SDL
controller handling exists in inspected engine source. Camera3D is a camera
value/manipulation type, not an exploration rig. Installed SDL is 2.32.10.

Preserve the existing InputEvent layout/enumerations, runtime/editor event pumps,
game/editor/plugin ABI and Camera3D. New input is opt-in for consumers. Create a
small `engine_input` / `Phlosion::Input` static target for SDL-free actions, linked
only to GLM/standard runtime. Put a separate SDL adapter in existing
`engine_platform`, linking that target to Input. Reuse SdlKeyMap there; no copied
key map or new dependency/version. Do not migrate current hosts in this pass.

Use namespace `engine::input`. Suggested files are `InputActions.h/.cpp` and
`SdlActionInput.h/.cpp` under `src/engine/input`; narrow helper files are allowed.
The adapter-specific header may expose SDL_Event (it is an explicitly SDL API),
while all action/value types remain SDL-free. No global manager, threads, event
bus, filesystem persistence or automatic wall-clock/frame scheduling.

Upstream references checked alongside installed headers:
- https://wiki.libsdl.org/SDL2/SDL_JoystickAttachVirtualEx
- https://wiki.libsdl.org/SDL2/SDL_GameControllerUpdate
- https://wiki.libsdl.org/SDL2/SDL_InitSubSystem

## Action layer contract

Worker may choose exact field/method spelling within these explicit interfaces:

- Noncopyable/nonmovable owner-thread `InputActions(options={})`. Options bound
  action count (default128, range1..1024), controllers (default8, range1..32),
  bindings/action (default32, range1..64). Constructor invalid options throw
  invalid_argument. Other malformed input/capacity rejects with bool+optional
  diagnostic; rejected calls preserve configuration, physical/action state and
  caller output. Wrong-thread operations throw logic_error before mutation.
- Action definition: owned name, kind Button/Axis1D/Axis2D, owned bindings.
  Names are 1..64 ASCII letters/digits/underscore/dot/hyphen, case-sensitive and
  unique. Define, atomically replace bindings for an existing name (rebind),
  remove and query published state. Empty binding lists are valid unbound actions.
  No game action names are hardcoded. Unknown names preserve output on failure.
- Typed bindings (e.g. std::variant): existing InputEvent::Key, existing mouse
  button, engine-defined gamepad button, single gamepad axis, or 2D gamepad stick.
  Digital/scalar sources carry finite vec2 scale bounded per component [-1,1].
  Button actions accept digital bindings with unit positive X scale only;
  Axis1D requires scale.y=0 and no stick binding; Axis2D supports directional
  digital/scalar contributions and component-scaled stick vectors. Reject unknown
  enum values and incompatible binding/action types rather than ignoring them.
- Standard gamepad buttons: south/east/west/north, back/guide/start, sticks,
  shoulders and D-pad; standard six axes: left/right X/Y and two triggers.
  Stick Y follows SDL (+down); binding scale performs caller-chosen inversion.
  Signed axes normalize to [-1,1], triggers to [0,1]. Complete controller
  snapshots contain arrays for all defined buttons/axes; validate the whole
  snapshot before replacing it, avoiding partially applied malformed input.
- Each analog binding has deadzone default .15 for sticks/signed axes, .05 for
  triggers (worker may use explicit constructor defaults), finite [0,.95].
  Signed axes use a rescaled axial deadzone, triggers rescale [deadzone,1].
  Stick bindings use radial deadzone: normalize direction, clamp raw magnitude
  to1, rescale magnitude above deadzone. Reject NaN/infinity and unsupported
  ranges. Sum contributions then clamp Axis1D to [-1,1] or Axis2D length to1.
  Opposing digital bindings cancel; Button combines via OR.
- State: vec2 value, down (magnitude >1e-6), pressed and released. Values for
  Button are (0/1,0); Axis1D uses X only. Keyboard/mouse uses existing InputEvent;
  ignore unrelated event types, reject malformed identifiers on relevant events.
  Ignore key-repeat down events, including repeats when no key is currently held.
- Ingest events/snapshots updates internal physical/action values and latches
  transitions. Explicit `advanceFrame()` publishes all action values and latched
  edges, then clears edge latches for the next frame. Repeated queries do not
  consume edges. Held controls persist; another advance with no events clears
  edges. A press+release between advances reports both edges with final down=false.
  Hosts choose cadence (typically one action frame per simulation tick).
- Define/rebind rebases only the affected action to current allowed physical
  input immediately, without synthetic pressed/released flags. Clear that
  action's pending edges and expose the rebased state; other actions/latches
  remain unchanged. Explain this exception to published-frame queries. Failed
  rebind is transactional. Removing an action removes its state without a callback.

## Controller lifetime and focus contract

- Engine-owned opaque nonzero ControllerId, unique and never reused across
  InputActions instances in the module. Add/remove controllers and submit full
  snapshots; enumerate sorted copied IDs, explicitly select one controller or
  none. Zero/stale/foreign IDs reject safely. Keyboard/mouse remains available.
  Unselected controllers cannot drive actions. Removing the selected controller
  clears its contribution and selection, latching releases for held actions;
  no automatic switch to another device. Controller count limits are not a
  total process-memory budget.
- Focus and enabled are separate gates (window focus versus gameplay/viewport
  capture). Losing either clears keyboard/mouse state, neutralizes actions,
  discards pending presses and latches releases for active actions. Input while
  gated cannot activate actions. Re-enabling/focus gain does not resurrect old
  keys or auto-repeat. Fresh key/button press events are required.
- On controller selection or recovery from a closed gate, suppress each
  controller control until a neutral observation: buttons released, stick
  components abs<=.05, trigger<=.05. A held stick/button must not suddenly drive
  gameplay after resume. Complete snapshots observe/rearm controls without
  requiring a new device; once neutral is seen, later input works normally.
  Track suppression per control, document behavior, and test both focus and
  enabled transitions plus combined gates. Do not suppress fresh keyboard input
  forever waiting for an event the host cannot provide.
- State/config/controller removal and enumeration all obey owner-thread rules.
  Distinct pure action instances can operate on independent owner threads.
  Allocation/resource failure retains existing C++ limitations, not an OOM
  recovery promise. Validate and prepare allocations before committing mappings.

## SDL adapter contract

- Noncopyable owner-thread adapter references InputActions, which must outlive
  it. Own one balanced SDL_INIT_GAMECONTROLLER subsystem reference and only its
  opened SDL_GameController handles/registered IDs. Never call SDL_Quit or change
  global hints/mappings/event state. Destroy adapter before external SDL_Quit,
  including the current Window shutdown; document this existing boundary.
- Expose explicit device refresh/enumeration, SDL event ingestion, and polling
  of complete controller snapshots. Constructor may throw for subsystem init;
  subsequent open/capacity errors report diagnostics, close failed candidate
  handles and preserve already usable devices. A multi-device refresh need not
  be globally atomic; document per-device progress. Do not silently omit errors.
- Recognize SDL mapped GameControllers, not arbitrary unmapped joysticks.
  Map SDL instance IDs to engine IDs; DEVICEADDED uses a device index, REMOVED
  uses instance ID. Duplicate discovery/events do not duplicate registrations.
  Removal closes only owned handles and clears selected input. Poll must also
  detect detachment if a removal event was missed. Explicit selection stays in
  InputActions; expose copied device name/engine ID and an instance-ID lookup
  as needed. No automatic player assignment, rumble or prompt artwork.
- Accept caller-supplied window ID. Keyboard/mouse/focus events apply only to
  that window; ID0 means controller-only. Use existing key mapping; unmapped SDL
  keys are ignored. Translate supported mouse buttons. Motion/wheel action
  bindings are outside this pass. No window or graphics context is created.
- Handle controller button/axis events using their event values so a short tap
  in the host's event queue is preserved even if the final polled state is up.
  Maintain per-device cached full snapshots and submit them atomically. Polling
  refreshes actual current state and normalizes signed min/max asymmetrically
  (-32768/32768, +32767/32767), triggers from controller range0..32767.
- Do not consume SDL's event queue (no SDL_PollEvent/WaitEvent inside adapter).
  Host owns pumping and forwards events; explicit controller update/poll is
  allowed and does not dispatch game callbacks. SDL remapping events reconcile
  snapshots; no custom mapping-database management. Tests must not alter real
  connected device state or global user preferences.

## Scope and verification

No camera implementation, editor/runtime event-pump edits, InputEvent ABI changes,
input-context stack/priority consumption, mouse motion/scroll axes, text/IME,
binding-capture UI, binding-file persistence, haptics, game rules or render work.
Guide examples should show defining move/run/interact, feeding SDL events,
polling, publishing, selecting a controller and rebinding with focus gates.
Do not claim the complete M2 controller/scene exit gate from virtual devices.

Create separate `PhlosionInputTests` / `PhlosionInputContracts` (Input-only),
and `PhlosionSdlInputTests` / `PhlosionSdlInputContracts` (Platform/Input/SDL,
no renderer). Apply /W4 /WX and 120-second timeouts. Use real SDL virtual mapped
controllers via installed SDL_JoystickAttachVirtualEx, not a fake adapter. They
need no window/GPU. Detach only test-owned virtual devices; preserve ambient SDL
references and clean up on failures. Test sources also cover both public headers.

Coverage: exact keyboard/mouse press/hold/repeat/release/short tap; digital and
analog combinations, normalized diagonals, opposing keys, deadzone boundaries,
triggers/inversion; define/rebind/type validation and unchanged failed state;
controller selection/removal/nonreuse/foreign IDs/capacity; focus/enable/repeat
and neutral-on-resume behavior; frame scheduling/repeated scripts; malformed
names/enums/scales/snapshots/NaN/infinity and configured bounds; owner-thread
rejection and independent CPU instances. SDL tests cover startup discovery,
actual mapped virtual buttons/sticks/triggers, event short taps, add/remove and
missed-removal polling, two devices with explicit selection, selected disconnect
release, focus/window filtering, and adapter teardown with an independently held
SDL subsystem reference remaining usable. Validate actual runtime SDL version.

Configure existing build-presentation, build all enabled Debug targets /WX,
run the full CTest suite. Retain all raw native commands/logs/exit codes under
ignored `artifacts/m2-d/`. Check executable imports: pure input has no SDL/GPU;
SDL input has SDL plus platform runtimes and no graphics backend imports. Record
exact evidence and physical-device/native-host limitations. No GPU matrix is
needed because existing hosts/rendering are unchanged.

## Worker ownership and handoff

Worker owns new input/action/SDL files under `src/engine/input/**` (preserve
InputEvent/SdlKeyMap contracts), `tests/TestInputActions.cpp`,
`tests/TestSdlActionInput.cpp`, narrow CMake integration, `docs/INPUT_ACTIONS.md`,
and this directory's REPORT plus ignored evidence. Root owns PLAN/CHECKPOINT/
ACCEPTANCE and shared roadmap/index/verification/boundary/README. You are not
alone in the codebase: preserve others' edits and accommodate them. No recursive
agents, staging/commit/push, provider/dependency changes or unrelated refactoring.

Use the already disclosed user-authorized Astra fallback while recent Flash
HTTP402 remains applicable. Root retains architecture/acceptance. Up to 120
minutes for this bounded bundle; finish internal implementation/test/debug loops
then report ready for review. Send substantive milestones, and checkpoint a real
architecture conflict instead of silently changing the contract.
