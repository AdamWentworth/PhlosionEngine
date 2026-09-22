# M2-F: asset-independent native exploration integration

Date: 2026-09-22. Baseline: clean main `36b6986` (accepted M2-E).
User authorizes the remaining M2 work possible before character/environment
assets arrive. Deliver a playable public course in the standalone runtime and
real editor game preview, integrating M2-A through E and qualifying native
OpenGL/Vulkan/D3D12 at 16:9 and 21:9. Do all automatable asset-independent work;
leave actual Outskirt Stand/character fitting and physical-device observations
explicit. Do not commit/push the new work automatically.

## Evidence and architecture decisions

- Accepted InputActions/SdlActionInput, CollisionWorld characters/triggers and
  ExplorationCameraRig exist. They are opt-in and currently not host-integrated.
- EditorProjectPlugin ABI 34 and InputEvent are in active use. Preserve their
  structs, enums, virtual table and fingerprint. Existing previews keep their
  legacy input route unless they explicitly advertise the new extension.
- Editor currently forwards focused keyboard/hovered mouse events but has no
  controller/focus/capture-state callback. Its fixed updates run at 1/60 with
  at most four catch-up ticks; new input must work with zero/multiple ticks.
- Existing public/scene fixture runtime hosts, editor plugins, process runner,
  image guard utilities and provenance conventions are reusable. Add a new
  exploration fixture rather than changing old cases or weakening their guards.
- Existing GltfSceneDrawOptions::instanceTransform and resourceScope permit
  bounded repeated rigid instance placement. Use that supported renderer path
  with tiny generated public box meshes/glTF, or an equivalent existing world
  instancing API. Do not generate new cache keys/resources every moving frame.
- Editor game surfaces currently fill the panel at arbitrary aspect. Add a
  generic opt-in game viewport aspect option with actual integer-ratio fitted
  content rectangle, correct pointer coordinates and letterboxing. Default
  scene/game layout stays unchanged. No renderer backend algorithm changes are
  planned; report a concrete blocker before changing one.

The one coherent implementation bundle below includes its own internal phases:
optional editor input bridge and CPU contracts; shared playable course and both
hosts; deterministic input replay and native/negative verification. Worker owns
routine discovery, implementation, debugging and QA through the whole bundle.
Root retains architecture and final acceptance, not parallel implementation.

## Optional editor input extension

Introduce a separate, explicitly SDL-specific header (for example
EditorSdlGameInput.h) and optional exported function returning a versioned callback
table. Do not append methods/fields to ABI 34 or change InputEvent. This extension
may expose SDL_Event because it is a separate opt-in platform API, just like
SdlActionInput; the existing generic boundary remains SDL-free.

The table has version 1, byte size and layout fingerprint (include SDL_Event and
context layout), with required begin-frame, event and end-frame callbacks taking
the existing runtime instance and borrowed context/event pointers. Validate
headers before reading callbacks; missing export uses the unchanged legacy route,
present malformed tables reject plugin load with a useful error. Main plugin
compiler/configuration contract validation still runs first. Store callbacks only
for that loaded library/runtime lifetime and reacquire on reload. Exercise absent,
valid and malformed extension contracts in CPU tests/probe fixtures as appropriate.

Context carries the SDL window ID, current window focus, active game viewport,
playing/paused state, viewport focus/hover, text-editing status, viewport rectangle
and dimensions. Document transient pointer lifetime and owner-thread invocation.
Call begin before host event pumping, deliver every relevant SDL window/controller/
keyboard/mouse event through event callback, end after pumping and before fixed
updates. Update window-focus state immediately on focus events. Call gates even
while inactive/paused so no held input can leak through resume; initialize disabled.
The new extension replaces legacy handleGamePreviewInput delivery for opted-in
previews only, avoiding double delivery. It never owns/consumes the host event
queue. No callbacks after runtime destruction or library unload.

Factor the input-gate decision/rectangle math into small CPU-testable functions.
Keyboard/controller gameplay requires focused window, active/focused game view,
Playing and no text editing. Mouse look begins on RMB in the active viewport;
motion/wheel are viewport-local, no stale delta on focus loss/resume/pause/switch.
Do not introduce global relative-mouse mode or steal editor UI controls. Release
events/gate closures must clear held input even when hover changes. Controller
discovery/removal continues while gameplay is disabled. Reuse SdlActionInput for
actual mapped controllers/event snapshots, balanced subsystem refs and handles.

The fixture owns InputActions definitions and publishes once per simulation tick;
queued edges/mouse deltas survive a render frame with no tick, are consumed only
once on the next tick, and are not repeated across catch-up ticks. Held rates
continue per tick. Follow accepted focus/neutral-rearm and explicit selection rules.
Document fixture selection: select a known available device once at startup;
Start on a device or a documented keyboard cycle selects explicitly; removal
clears selection without switching to another pad. Tests choose their own virtual
device, never ambient hardware. No user hints/mappings/device state changes.

Qualification adjustment (root-approved 2026-09-22): native hidden SDL windows
suppress controller events under the default background policy, although the
same owned virtual device works in the windowless CPU replay. Hidden, opt-in
replay child processes may therefore receive
`SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1` through their isolated environment.
This exception is limited to automated replay: no user/session/global hint or
mapping changes, no ambient-device selection, and no override for normal
interactive launches or the physical-device check. The runner must restore any
temporary environment in `finally`; evidence must record the override and
synthetic focus/background mode and reject inconsistent mode metadata. This
qualifies replay routing, not physical OS focus or background-device behavior.

## Shared public exploration course

Put runnable content in examples/exploration_fixture, verification/CLI/runner in
tools/exploration_fixture, automated tests in tests. Add an opt-in fixture build
option following existing CMake conventions; enable it in the existing local
build-presentation for qualification. Engine aggregate dependencies stay unchanged.

Use a reusable sample session shared by the standalone host and editor plugin:
floor, walls/thin wall, walkable ramp, nonwalkable slope, stairs/ledge, distinct
sensor zone, and a visible rigid placeholder player. Rendered geometry and
collision shapes come from one authored public course description. Use copied
poses/IDs and bounded GPU resource ownership. Include an authored NPC waypoint
example using a real second character: it walks an open route, detects a blocked
route without walking through walls, and has an explicit retry/reset policy.
This remains sample/game policy, not a new navigation subsystem.

Normal visible mode must be playable with keyboard/mouse or controller:
WASD/left stick move relative to the camera, Shift/stick-click run, RMB drag/right
stick orbit, wheel (and a documented controller control) zoom, R/Back resets to a
safe spawn with an explicit camera reset. Camera framing settings are authored
separately from runtime state and exposed through fixture configuration and/or
existing generic editor project commands. Stop/reset preserves authored settings
and recreates only runtime state. No final character animation or asset import is
claimed. Show concise controls/status for human use, without test implementation
details in the ordinary player-facing view.

Actual fixed updates must call accepted APIs in order: publish action frame,
camera-relative intent, character movement, discrete trigger sample and camera
step. Render the resulting player and camera, never an unrelated animation.
Record trigger Enter/Stay/Exit and use a visible response to sensor membership.
Reset clears runtime action/trigger history safely and returns player/camera/NPC
to authored state. Input events do not drive variable-dt physics. Both hosts
handle resize, pause/resume, loss of focus, scene/preview reset and clean teardown.

## Deterministic native input replay

Provide opt-in bounded replay tooling for both hosts using the same gate/SDL
event path as interactive input. A generic editor CLI replay flag is allowed,
with implementation in a separate small helper; keep fixture names/content out
of engine source. Reject malformed/oversized/nonfinite/out-of-order scripts.
Replay may explicitly supply simulated window/viewport/text/play states for
hidden automation. Mark that mode and these states in evidence: this validates
host routing with synthetic events, not OS focus or physical drivers.

Use actual test-owned SDL virtual mapped devices for axis/buttons and hotplug,
plus queued SDL key/mouse/wheel events. Pump only in the host. Scripted focus,
pause/viewport/text gates must use the same input-policy functions/callbacks as
normal operation. Repeated events and zero/multiple-tick schedules get CPU tests.
Keep fixtures deterministic despite ambient devices. Cleanup every owned virtual
device/handle before external SDL_Quit; preserve independent SDL references.

## Verification and native acceptance

Add appropriate standalone CPU tests for the new contract/gates/scheduling/course,
and a negative evidence harness. Keep existing contracts intact. Build all enabled
Debug targets, record actual /W4 /WX for affected new targets, run all CTest.
Known legacy aggregate /W1 /WX- stays documented; no global qualification rerun.

At minimum capture these eight meaningful states on both hosts, all three APIs,
both exact aspect ratios (96 captures): walk, run, wall-blocked, ramp, stairs,
trigger-active, camera-obstructed/orbit, recovered/reset. Capture at matching
actual simulation ticks across hosts/APIs. Case scripts additionally establish
steep/ledge behavior, trigger enter/stay/exit, NPC blocked/retry, controller
selection/hotplug, focus/viewport/text/pause rearm, mouse delta/wheel and reset.
States may use independent safe spawns in the same authored course. No fake
logical-frame labels: record actual tick counters and state transitions. A case
may freeze after its target tick for capture if this is explicit and verified.

Reuse existing process supervision and image utilities. Runner creates a fresh
run directory and isolates child environment/state, records exact source/build
binaries/hashes, actual requested/observed screenshot frame and dimensions,
handshake/trace hashes, editor metrics and requested/observed native API. Fail on
fallback, missing/extra cases, timeout, stale files, wrong ticks/dimensions/aspect,
wrong binary, malformed traces or mismatched host states. Never weaken standing
image thresholds: normalized MAE <= .01, RMSE <= .08, changed ratio <= .05 at
channel tolerance 8, applied to comparable viewport captures.

Expected-content checks must independently reject empty scenes, absent/misplaced
player, wrong camera/view, missing course landmarks and incorrect sensor response.
Derive expectations from authored course/script and analytic tolerances, not from
captured images or unvalidated producer handshakes. State assertions must establish
walk/run distances, blocking/support, trigger sequence, camera shortening/recovery,
gate/rearm, NPC behavior and identical final script state across all captures.
Retain contact sheets and inspect them visually; CPU/image cross-comparisons alone
do not replace looking at the native output. Negative harness mutates pixels,
state, ticks, aspect, backend, hashes/paths and coverage, with pristine recovery.

Because editor routing/layout changes are opt-in, run the existing 36-case scene
fixture native matrix as a regression on the final binaries as well. If shared
capture utilities or rendering behavior change, broaden regression matrices to
those affected. Do not modify existing threshold constants or source assertions.

Keep all commands/raw logs/exits, failed attempts, manifests, screenshots, trace,
compiler/import records and final hashes under ignored artifacts/m2-f/. Runtime
and real editor must both be qualified; one host or one/two APIs is incomplete.
No production game/private assets are needed. Physical-controller human checks
remain pending unless actually performed; user availability was asked separately.

## Ownership, handoff and remaining asset gate

Worker owns new example/tools/tests, optional extension/replay/policy helpers,
narrow EditorMain/EditorShell integration and CMake, docs/EXPLORATION_FIXTURE.md,
this directory's REPORT.md and ignored artifacts. Root owns PLAN/CHECKPOINT/
ACCEPTANCE and shared roadmap/verification/boundaries/index/README. You are not
alone in the codebase: preserve others' edits and accommodate them. No recursive
agents, commits/pushes, dependency/provider changes or unrelated refactoring.
Do not edit accepted input/collision/camera/renderer algorithms without surfacing
a concrete architecture blocker to root first. Prefer isolated helpers to bloating
EditorMain, and expose useful user controls rather than implementation internals.

Use the disclosed, user-authorized Astra fallback after recent Flash HTTP402.
Routing doctor is static-ready and config hash unchanged; no Flash inference claim.
Allow up to 180 minutes for the bounded implementation/debug/QA bundle; checkpoint
actual blockers or a required scope revision. Finish the full verification and
report ready for root review, not self-accepted. Root will consolidate findings.

After acceptance, remaining M2 work should be actual asset/scene fitting, real
character/environment-specific collision/camera/triggers/routes and final game
scene qualification, plus any unperformed physical-device observations. Do not
claim full M2 or asset-specific correctness from the public placeholder course.
