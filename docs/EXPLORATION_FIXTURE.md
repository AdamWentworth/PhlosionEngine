# Public exploration course

This opt-in sample integrates named actions, the SDL adapter, kinematic
characters, discrete sensor samples and the exploration camera. It runs in
`ExplorationFixtureRuntime` and the native editor's **Game** preview through
`ExplorationFixtureEditorPlugin`. It requires no private assets. The rigid,
color-coded placeholders intentionally use the existing unlit world material
path; this is not character animation or production-scene qualification.

Configure `PHLOSION_BUILD_EXPLORATION_FIXTURE=ON`, then build Debug. The engine
aggregate does not gain a collision/camera dependency. The sample links those
modules explicitly. Geometry is ten tiny generated glTF cube materials loaded
from an in-memory asset store. A single authored course table supplies collider
and rendered transforms. Every moving instance reuses bounded cached geometry,
material keys and one explicitly released world-resource scope.

## Controls

| Action | Keyboard/mouse | Controller |
| --- | --- | --- |
| Move relative to camera | WASD | Left stick |
| Run | Left Shift | Left stick click |
| Orbit | Hold RMB and drag inside the viewport | Right stick |
| Zoom | Mouse wheel | Left/right shoulder |
| Reset player, NPC and camera | R | Back |
| Select a controller | Tab cycles available devices | Start on that device |
| Pause standalone | P | Use editor Pause in the editor |
| Quit standalone | Escape / close window | Close window |

Interactive startup selects one available mapped controller once. Removal clears
selection; it does not switch to another pad. A newly selected or re-enabled pad
must observe neutral controls before movement resumes. Keyboard repeat cannot
reactivate a held key after focus loss. The sensor floor and player turn green
while the player's capsule overlaps the zone. The white NPC follows its waypoint,
waits after detecting blocked progress, and retries from its authored spawn.
Leaving the finite course and falling below its safety height resets the player,
NPC and camera to the safe spawn. Both native windows support resizing.

Launch normally (visible, no replay/background override):

```powershell
.\build-presentation\Debug\ExplorationFixtureRuntime.exe --api=d3d12 --trace=artifacts/m2-f/manual-runtime.jsonl
```

The matrix runner stages a reusable project at
`artifacts/m2-f/<run>/project/phlosion.project.json`. For a visible editor check:

```powershell
.\build-presentation\Debug\PhlosionEditor.exe --project=artifacts/m2-f/<run>/project/phlosion.project.json --renderer=d3d12 --game-preview=exploration --play-game-preview --game-aspect=16:9
```

Set `PHLOSION_EXPLORATION_TRACE` to an absolute JSONL path to opt into editor
telemetry. Do not set `PHLOSION_EXPLORATION_AUTOMATION`, `--input-replay`, or the
background-controller override for the physical-device check. Telemetry records
game actions, gates, selected device names, player/NPC positions, support IDs,
sensor counters and camera/reset state. It records no raw keys or editor text.

For the short physical check, exercise move/run/orbit/zoom and reset, then return
from pause or another window while holding a stick (movement must wait for
neutral). Unplug/reconnect and press Start to select the reconnected device.
Resize both hosts and check that the editor's fitted image remains exact and
letterbox clicks do not start an orbit. In the editor, click the Game image to
give it focus; UI text editing must gate gameplay. The root review records these
human observations separately from automated replay evidence.

## SDL editor extension and scheduling

`EditorSdlGameInput.h` is a separate optional platform extension. ABI 34 and
`InputEvent` are unchanged. The export returns version, byte size, layout
fingerprint and required begin/event/end callbacks. The main plugin contract is
validated first. Absence preserves legacy delivery; a malformed present table
rejects load. The table belongs to that loaded DLL lifetime and is reacquired on
reload. No callback runs after runtime destruction. Pointers are borrowed only
for a callback, on the host owner thread.

Begin runs before the SDL pump, every event is forwarded through the extension,
and end runs before fixed updates. Focus loss updates the context immediately.
Keyboard/controller gameplay requires a focused window, active/focused Game
viewport, Playing, and no text editing. Gates run during pause/inactivity too.
Controller discovery/removal continues while disabled. Opted-in runtimes do not
receive duplicate legacy events. RMB drag starts inside the viewport; release
and gate closure clear it even after hover changes. No global relative-mouse
mode, event consumption, controller remapping or user hint changes are involved.

Actions publish once per actual simulation tick. Zero-tick render frames retain
pending edges/deltas; only the next tick consumes them. Held stick rates continue
across catch-up ticks. The fixed update order is action publication, camera-relative
intent, character movement, discrete trigger sampling, then camera step. Reset
recreates runtime characters/trigger histories and explicitly resets the camera,
preserving immutable authored camera settings.

Both hosts read optional authored framing from
`PHLOSION_EXPLORATION_CAMERA_DISTANCE`, `PHLOSION_EXPLORATION_CAMERA_PITCH`, and
`PHLOSION_EXPLORATION_CAMERA_YAW` at session creation (angles in radians).
Malformed/nonfinite values reject; the rig validates its documented bounds.
These values survive Stop/reset. Qualification clears ambient overrides and
uses the sample defaults: distance 7, pitch .48, yaw 0.

`--game-aspect=16:9` or `21:9` fits an integer multiple of the reduced ratio inside
the editor panel and returns the actual image rectangle for pointer coordinates.
The default layout is unchanged. Letterboxes are outside the hover rectangle.

## Replay and verification

`SdlInputReplay` accepts at most 1 MiB / 4096 ordered events and 10000 frames.
Its JSON schema is `phlosion-sdl-replay-v1`. Scripts use key/mouse/wheel events,
owned virtual mapped controller attach/axis/button/detach, and explicit synthetic
focus/viewport/text/play states. Hosts own the only SDL event pump. Replay owns a
balanced controller subsystem reference and detaches only its virtual device.

Hidden SDL windows normally suppress controller events. The qualification runner
sets `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1` **only in each replay child's
ProcessStartInfo environment**, without mutating the runner/user environment or
interactive policy. This approved exception is recorded with the synthetic mode
in the environment/trace/handshake and checked by the guard. It establishes host
routing under simulated focus, not physical drivers or real OS focus behavior.

```powershell
.\tools\exploration_fixture\run_capture_matrix.ps1 -RunId qualification
```

The required matrix contains walk, run, wall-blocked, ramp, stairs, trigger-active,
camera-obstructed, and recovered-reset on both hosts, OpenGL/Vulkan/D3D12, and
16:9/21:9: 96 captures. Each session freezes explicitly at actual tick 120;
screenshot frame 160 is verified separately from simulation ticks. Native runs
are hidden. The guard checks analytic movement/support/camera expectations,
continuous traces, semantic cross-host state, independent ray–box expected pixel
content, exact viewport dimensions, unchanged image thresholds, hashes, native
backend records and complete coverage. Failed attempts remain under ignored
`artifacts/m2-f/` alongside final logs, manifests, imports and contact sheets.

`-Supplemental` runs steep-slope blocking and ledge-drop recovery at 16:9 on both
hosts and all three APIs (12 more captures). Run the native negative harness after
the main matrix; it rejects 27 deliberate evidence/pixel defects and verifies
pristine recovery:

```powershell
.\tests\exploration_fixture\test_native_harness.ps1 -Manifest artifacts/m2-f/qualification/manifest.json -GuardExe build-presentation/Debug/ExplorationFixtureGuard.exe
```

Physical-controller observations and actual character/environment fitting remain
separate gates. Read the dated implementation report for the evidence actually
completed; this guide alone makes no acceptance claim.
