# Editor performance and startup

## Live viewport statistics

Enable **Stats** next to Play in the viewport toolbar, or launch with `--stats`.
The top-right overlay works in Scene and Game views, including Edit/Pause.
It labels the editor build configuration and native API. FPS and Frame use
actual elapsed time, independent of `--fixed-delta` and simulation speed.
GPU time covers the complete editor frame; Game/Scene CPU covers the viewport
render callback and its layout refresh, and Sim CPU covers gameplay ticks.
Whole-frame timings also include editor UI, driver waits and presentation.
Missing GPU queries display `n/a`. Use Development (`RelWithDebInfo`) for daily
work: optimized C++, symbols, assertions and release-runtime ABI. Release remains
the shipping baseline; Debug is a diagnostic configuration. Match the editor
and project DLL configuration; the UI calls RelWithDebInfo Development.

`--metrics-output` also records viewport/simulation CPU summaries, actual viewport
dimensions, build configuration and the live stats sample. Set
`--metrics-warmup-samples=120` to discard initial samples (default 10).
Use a separate screenshot run: image readback/writes distort frame timings.
The interactive HUD uses a bounded half-second accumulator; historical frame
arrays are collected only when an automation metrics output is requested.

## Performance recordings and standalone play

**Performance > Record 30 Seconds** (also beside Stats) excludes two seconds of
warmup, records a bounded frame history, then opens a summary. Reports contain
mean, nearest-rank p95 and worst timings, valid GPU sample counts, per-frame
CPU/GPU data, project/scene/scenario/profile/API/resolution metadata, and the
starting camera and layout. Default output is `.phlosion/performance` beneath
the project. Changing the measurement context or building code cancels a run;
partial runs are discarded. Camera movement and simulation changes within a
running scenario remain part of the measured workload.

Automation uses the same recording path:
`--record-performance-at=40 --record-performance-seconds=30
--record-performance-warmup-seconds=2 --performance-output=<file.json>`.

The **Play** menu exposes the project's `play_configurations`. Optional paired
`build_directory` and `build_target` fields run an asynchronous incremental CMake
build using the editor configuration before launch; failure never launches an
old executable. Build and working directories must be portable project-relative
paths. The host serializes these builds with gameplay reload. Environment values
exactly equal to `{renderer}` resolve to the active native API. Projects determine
whether the executable opens a fresh game or a particular scenario.

A successful standalone launch pauses embedded play and minimizes the editor.
Minimized interactive editors suspend rendering and simulation to release GPU
capacity; hidden automation continues rendering normally. Restore the window to
resume editing. `--launch-play=<id> --exit-after-play-launch` exercises the same
build/launch action in automation and reports errors as failure exit codes.

## Startup and automation

Project opening and embedded game startup are separate lifecycle phases.

- `IEditorProjectRuntime::open()` mounts the editable scene.
- `prewarm()` prepares only the active Scene viewport.
- `initializeGamePreview()` is deferred until the user selects Game Preview or
  requests one on the command line.

The editor logs each project-load phase as
`[PhlosionEditor][ProjectLoad]` and the deferred gameplay cost as
`[PhlosionEditor][GamePreviewWarmup]`. Project plugins should add their own
phase logs around expensive source decoding and scene composition.

Engine caches are content- and implementation-keyed rather than merely
filename-keyed. OpenGL program binaries include GPU/driver and shader-source
identity; model, neutral-PMREM, and project caches must likewise invalidate
when their inputs or schema change. Cache writes belong under `cache/` and are
never authoritative project content.

Use this repeatable smoke benchmark from the engine repository:

```powershell
.\build\Debug\PhlosionEditor.exe `
  --project=D:\Projects\YourGame\phlosion.project.json `
  --renderer=d3d12 --frames=2
```

Run once without `--game-preview` to measure editing startup, then with
`--game-preview=<id>` to measure the explicitly requested gameplay warmup.
Interactive Game-tab activation selects the first preview associated with the
active scene unless another preview for that scene is already active. Project
hosts may set `GameContext::deferBulkModelPrewarm` for embedded previews so
their UI is not blocked by an application-wide model preload; standalone game
hosts retain the full-prewarm default.
Embedded hosts should also set `GameContext::deferStartupFramePrewarm` so
standalone UI-card and world-layer warmup frames cannot target the editor
backbuffer before the Game viewport render surface is bound. Mouse-wheel camera
navigation remains active over the Game viewport while simulation is frozen in
Edit mode.

For unattended visual/performance qualification, use the automation switches
instead of starting an interactive editor:

```powershell
.\build\Debug\PhlosionEditor.exe `
  --project=D:\Projects\YourGame\phlosion.project.json `
  --renderer=vulkan --hidden `
  --state-directory=D:\Temp\phlosion-editor-state `
  --asset-preview=Example.phlo `
  --asset-preview-quality=ultra `
  --asset-preview-animation=bind --asset-preview-time=0 `
  --fixed-delta=0.016666667 --frames=90 `
  --metrics-output=D:\Temp\phlosion-editor-metrics.json
```

`--hidden` creates the SDL window with `SDL_WINDOW_HIDDEN` and suppresses the
startup raise/focus request, persistent recent-project writes, and persistent
window placement. It also disables vsync so frame metrics measure work rather
than the presentation interval. Pair it with a dedicated `--state-directory`
so ImGui layout state cannot affect or overwrite the interactive editor.

`--asset-preview-quality` accepts `low`, `medium`, `high`, `ultra`, or `0`-`3`.
Scene-view captures can set a deterministic inspection pose with
`--scene-camera-position=x,y,z` and `--scene-camera-target=x,y,z`. Position is
applied before the camera looks at the target, so a fresh state directory does
not need an interactive camera gesture to reproduce a close-up.
Use repeatable `--open-scene-at=FRAME:SCENE_ID` arguments to test scene changes
without restarting the renderer or discarding GPU caches. Frames are zero-based;
requests must be strictly increasing and start after frame zero, for example
`--open-scene-at=40:maps/second --open-scene-at=80:maps/first --frames=120`.
Each request follows the same action as selecting a scene in the editor and
logs `[Phlosion Editor][SceneSwitch] frame=... scene=...` after success. Invalid
scene IDs, load failures and exiting before all requests complete fail the run.
`--fixed-delta` makes animation/simulation input deterministic. The metrics
document records project-load phases, all-frame and post-warmup CPU/present/GPU
summaries, last-frame backend submission statistics, project statistics, and
selected-asset package bytes. The current direct PHLO preview has no decoded
object-cache layer, so those cache hit/miss fields are explicitly `null` rather
than fabricated; backend cached-draw counts remain available.

Screenshot capture is backend-owned through
`PHLOSION_BACKEND_SCREENSHOT_PATH` and
`PHLOSION_BACKEND_SCREENSHOT_FRAME`. Project repositories should wrap these
low-level switches in a checked baseline command that verifies editor/plugin
compatibility, isolates state, and rejects renderer fallback.
