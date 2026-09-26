# Public exploration course

This opt-in sample integrates named actions, the SDL adapter, kinematic
characters, discrete sensor samples and the exploration camera. It runs in
`ExplorationFixtureRuntime` and the native editor's **Game** preview through
`ExplorationFixtureEditorPlugin`. It requires no private assets. The rigid,
color-coded placeholders intentionally use the existing unlit world material
path; this is not character animation or production-scene qualification.

## Provisional Outskirt Stand content

The `outskirt` case is an opt-in private-content preview. It keeps the public
course and its automated matrix unchanged. Set both paths to engine-subset GLBs
from the private GameCube research depot, then launch the existing runtime:

```powershell
$env:PHLOSION_OUTSKIRT_ENVIRONMENT_GLB = 'D:/ProjectData/Research/PokemonColosseum/OutskirtStand/output/outskirt_outdoor_engine.glb'
$env:PHLOSION_OUTSKIRT_WES_GLB = 'D:/ProjectData/Research/PokemonColosseum/Wes/jacket/output/wes_field_controller_loops.glb'
.\build-presentation\Debug\ExplorationFixtureRuntime.exe --api=d3d12 --case=outskirt
```

With both asset variables above set, set `PHLOSION_EXPLORATION_CASE=outskirt` and launch a project
using `ExplorationFixtureEditorPlugin`. The Game preview is labelled “Outskirt
Stand (provisional)”. Stage and open the private preview project with:

```powershell
& 'D:/Projects/Research/PokemonGCAssetResearch/tools/colosseum/run_outskirt_native.ps1' -SetupOnly
$env:PHLOSION_EXPLORATION_CASE = 'outskirt'
.\build-presentation\Debug\PhlosionEditor.exe --project='D:/ProjectData/Research/PokemonColosseum/OutskirtStand/editor-project/phlosion.project.json' --renderer=d3d12 --game-preview=exploration --play-game-preview --game-aspect=16:9
```

The project descriptor lives in the private depot. The plugin contains no
private binary asset paths. The scene draws the recovered
environment and Wes through the shared glTF renderer. A three-state animation
graph selects Wes's source-confirmed idle/walk/run clip slots from character
speed and uses 0.15-second crossfades. The same sampled blended pose and skin
palette feed each backend. The fixture builds static triangle collision from
four named recovered outdoor meshes: ground,
paved forecourt, raised train ramp and train body. It rejects missing nodes or
excessively degenerate geometry, and keeps authored train-side blockers and an
approach trigger where the render mesh cannot supply gameplay collision. This
is not extracted game collision. The PokemonCyberpunk encounter reads its mesh
selection, blockers, sensor, camera framing, and locomotion speeds from the
game's v4 scene config; the separate research preview retains its historical
fallback. The encounter maps left-stick magnitude through Colosseum's
slow-walk, walk, and run speed zones, then blends Wes's poses from measured
movement speed. Source materials are converted to the engine's opaque material
subset and 256-pixel images. The private research guide at
`D:/Projects/Research/PokemonGCAssetResearch/docs/colosseum/OUTSKIRT_STAND.md`
records source hashes, conversion limits, collision audit and the native
idle/blend/walk/run/ramp/wall captures at 16:9 and 21:9.
The game runs `tools/Verify-Outskirt-Encounter.ps1 -M2Scene` for 228 runtime/editor
captures at both aspects and all three APIs: idle, walk, run, the recovered
ramp's leading lip and slope, blocked train side, reset, mouse/controller camera
orbit, synthetic focus loss/resume, and virtual controller reconnect. The ramp
lip rises about 0.14 world units above the paved forecourt near X=-2; paired
walking and running replays check the support change and vertical ascent.
Physical device/focus and editor toolbar checks passed in visible D3D12
editor sessions; the game test plan and manual trace report record the evidence.
The provisional recovered scene is the user-authorized M2 content while its
eventual remake belongs to later content fitting.

The separate opt-in `outskirt-battle` case uses the recovered battle-field GLB
and four labelled Wes stand-ins for provisional actor slots. It exercises the
shared M3 battle camera with a wide four-actor shot, a two-actor blend, a cut,
impact shake and recovery. The runtime and editor share the same fixed-tick
camera sequence; the editor preview ID is `battle`. Set
`PHLOSION_OUTSKIRT_BATTLE_GLB` to the private
`output/outskirt_battle_engine.glb`, keep `PHLOSION_OUTSKIRT_WES_GLB` set, and
run the runtime with `--case=outskirt-battle`. For the editor, set
`PHLOSION_EXPLORATION_CASE=outskirt-battle` and launch with
`--game-preview=battle`. The stand-ins do not represent the eventual Pokemon
roster or battle rules. The private research runner
`tools/colosseum/run_outskirt_battle_native.ps1` stages the project and checks
both hosts on all three APIs and both target aspect ratios.

The temporary `BattleSequence` advances on fixed ticks: camera blend at 30,
counter cut and Wes gesture at 60, an animation impact marker at 90, damage
and recovery at 110, and automatic cleanup at 150. The fixture binds the
impact marker to a generated PCM cue in `AudioMixer` and a short visual flash;
four simple health bars show the scripted damage. Escape / gamepad B cancels,
Space / gamepad A skips, and R resets in the active Game view. Cancel, skip,
and natural exit clear the mixer voice, queued audio, flash, HUD and camera rig.
The interactive runtime and editor Game preview send the generated spatial cue
through the SDL audio device. Hidden capture runs use the same mixer with a null
sink; set `PHLOSION_BATTLE_AUDIO_OUTPUT=1` to exercise a hidden run with an SDL
dummy device. If opening or queuing the device fails, playback continues through
the null sink and logs the device error. Physical audibility still needs a
listening check. Battle rules and a production HUD remain separate integration
work. The private runner captures eight phases,
including the three cleanup routes, at both target aspect ratios in runtime
and editor.
The 96 native captures pass expected actor, field, HUD, flash and cleanup
checks on OpenGL, Vulkan and D3D12. All 48 matching runtime/editor viewport
pairs are pixel-identical; the 64 cross-API comparisons have maximum MAE
0.892/255 against the unchanged 1.0 limit. The machine-readable report and
images are in the private Outskirt depot.

For the provisional Colosseum presentation, set `PHLOSION_BATTLE_HUD_PNG` to
the private decoded `fight_common.fsys/menu_048.gtx` sheet (520×190 RGBA PNG).
The preview crops source status-panel and A/B command art, then draws scripted
health fills over it. Set `PHLOSION_BATTLE_VFX_PLUGIN` to the game-owned
`CyberpunkBattleVfx.dll`; it maps the impact cue into PhlosionVFX's `EffectHost`
and submits a temporary world-space glow through the shared VFX render path.
The public fallback bars and flash remain available without these inputs.
The private `run_battle_presentation_native.ps1` runner and verifier exercise
runtime and editor, both aspect ratios, eight phases and all three APIs. The
recovered sheet supplies framing only; names, HP numerals, localized command
text, final characters and authored move effects remain game work.
The 96-capture matrix passes 112 parity comparisons with maximum cross-API
MAE 0.9675/255 (limit 1.0); all 48 runtime/editor viewport pairs are exact.
The verifier checks the borrowed panel/command pixels, scripted health change,
projected glow and cleanup. Without the private inputs, the previous OpenGL
impact image is pixel-identical.

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
| Run | Left Shift | Tilt left stick farther in PokemonCyberpunk; stick click in the public fixture |
| Orbit | Hold RMB and drag inside the viewport | Right stick |
| Zoom | Mouse wheel | Left/right shoulder |
| Reset player, NPC and camera | R | View/Back (Select) |
| Select a controller | Tab cycles available devices | Menu/Start on that device |
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
neutral). Unplug/reconnect and press Menu/Start to select the reconnected device.
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
