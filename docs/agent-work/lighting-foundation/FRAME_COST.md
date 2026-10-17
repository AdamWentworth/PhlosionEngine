# Lighting frame-cost follow-up

2026-10-17, starting from engine fa7ae72 / game 45f42ca. Cloud-only root work.
Preserve the accepted scene, materials, shadows, bounce, reflections and Hybrid
weather. Evidence lives under `artifacts/lighting-foundation/frame-cost` in the
engine and game. This is a performance pass, not a new Cyberpunk parity claim.

## Breakdown

Sequential D3D12 1920x1080 heavy-wet-night controls, RTX 2060 SUPER, driver
617.42, 200 warm samples (frames 30-229), with no competing game/editor process:

| Control | GPU median ms | Host median ms |
| --- | ---: | ---: |
| Baseline | 20.506 | 20.566 |
| No local shadows | 17.072 | 17.104 |
| No colored bounce | 18.425 | 18.500 |
| No scene reflections | 17.944 | 18.289 |
| No falling rain/impacts | 17.862 | 17.851 |
| No bloom | 20.316 | 21.363 |
| Baseline repeat | 20.339 | 20.612 |

These are whole-frame feature-off deltas, not isolated GPU pass timestamps.
They include dependencies and overlap and must not be summed. The bloom control
also has higher host costs, so it does not isolate bloom's GPU duration. Both
baseline runs retain the same appearance. The rain control retains accumulated
wetness but also disables the Hybrid presentation haze; it is not a pure rain
shader measurement. All diagnostic switches are confined to verification launches.

The first baseline's median host scopes are 1.676ms directional-shadow submission,
3.980ms local-shadow submission, 1.475ms main geometry, .018ms water surfaces and
7.573ms rain build/submission. These are wall-clock scopes, not GPU durations or
isolated thread CPU time. Logging is deferred until capture completion. A new
`field-reflections` / `battle-reflections` scope fills the reflected-scene gap;
normal play keeps tracing disabled. It includes capture begin, reflected sky and
geometry, capture end/copy, and restoring the main-view sky.

## Retained candidate and matched timing

On Vulkan/D3D12, evaluate existing local-shadow visibility before LTC area-light integration and
skip only exactly zero-visibility contributions. Unshadowed and partially visible
sources retain the same integration and multiplication order. No shadow map,
filter, source strength, receiver or geometry changes. The shared shader defaults
to this ordering; the OpenGL shader composer disables it and preserves its original
expressions. This changes execution cost, not the lighting model or material ABI.

| API | GPU control/final ms | Host control/final ms | Final GPU / host p99 ms |
| --- | ---: | ---: | ---: |
| OpenGL | 38.301 / 37.909 | 40.369 / 40.123 | 40.892 / 45.097 |
| Vulkan | 24.154 / 22.264 | 74.586 / 68.979 | 27.092 / 94.805 |
| D3D12 | 20.257 / 19.893 | 20.557 / 19.976 | 21.617 / 38.732 |

`comparison-specialized.json` verifies identical scene inputs and pixel-identical
1920x1080 output on every API, with 200 warm GPU, host and reflection-scope samples
per run. Controls and candidates used the same tracing, scene and weather. Runs
were sequential on GPU 0; GPU 1 remained idle. Raw traces retain p99/max and startup.
GPU monitors were retained, but stopping nvidia-smi left their last buffered samples
unflushed, so they do not establish clocks throughout every timing window.

The D3D12 saving in the final pair is .364ms (1.8%). Earlier candidates measured
19.869, 19.744 and 19.928ms against controls of 20.339-20.506ms and 20.257ms.
Vulkan's earlier candidate measured 23.481ms: its saving varies .673-1.890ms.
Do not attribute the full host-wall reduction to shader arithmetic or promise
the largest observed saving. Host tails remain high; this is not sustained 60 FPS.
First material loads after a shader-source change can still take minutes,
particularly on D3D12. Warm-frame measurements exclude this startup cost; the
existing shader-preparation/startup limitation is not resolved by this pass.

The all-API early-visibility trial gave OpenGL 50.645ms, then 37.296ms on repeat,
against its 38.301ms control. The cause of that outlier was not isolated. Rather
than adopt an inconsistent OpenGL optimization, retain its old ordering. Its final
.392ms difference is run variation, not a claimed OpenGL speedup. Keep both trial
logs (`candidate`, `gl-repeat`) alongside the final `specialized` run.

Final median reflection host scopes are 3.241/5.770/1.375ms (GL/Vulkan/D3D12).
Local-shadow scopes remain 11.937/16.489/3.881ms, and rain 8.101/7.685/7.577ms.
Vulkan still has substantial CPU-side submission/wait cost. The next performance
investigation should separate actual thread CPU time from waits and profile the
repeated shadow/material submissions before selecting a larger optimization.

The initial candidate shader patch is retained in the engine artifact directory;
the previous shader was temporarily restored and rebuilt for matched controls.
Game `run-breakdown.ps1`,
`run-matched.ps1`, `summarize.py`, raw logs, captures and GPU monitor retain the
reproduction inputs. The original accepted visual baseline remains in
`reflection-quality/final` (and its preceding control folders).

## Verification

55 engine checks and five game environment/weather contracts pass. All 36 public
reflection images and all 27 station/battle/control/signage images exactly preserve
their preceding checkpoint. Existing station, battle, neon and sign-emission
oracles pass without threshold changes; maximum station API MAE is 1.329876/255
and maximum runtime/editor MAE .0000172/255. All twelve native shadow on/off probes
pass independent ray/box occlusion and API/host checks.

The normal D3D12 launch passes with the current staged game/model DLLs, Hybrid
rain and CP audio. The probe is under the engine's `frame-cost/final/normal-launch`
root; its launch log is under the game's `frame-cost/final`. The canonical Blender
and private station GLB hashes are unchanged.

Autochess's separate runtime rebuild and paired editor/plugin build pass. Both
native character cohorts and editor body/eyes and stage checks pass on all APIs;
all twelve images exactly preserve the preceding checkpoint. Its source and
PokemonModelSupport source remain unchanged. Both engine editor build directories
and staged game/model plugins are current; all hidden native sessions exited.

The candidate is accepted as a small measured optimization with unchanged output.
All existing cost guides remain in force, and none of these results establishes
sustained 60 FPS. The 120s daylight / 180s Hybrid weather defaults are unchanged.

Reproduction uses `run-final-engine.ps1`, `run-final-game.ps1` and
`run-final-shared.ps1` under each repository's artifact root. The engine run builds
the editor/test targets, runs the 55 selected CTests and `verify_planar_quality.py`.
The game run builds/stages its hosts, runs five contracts, `Verify-Canopy-Neon.ps1`
stages matrix/battle/controls/signage, `Verify-Local-Shadows.ps1 -Stage probes`,
their existing validators and the normal launcher probe. The shared run explicitly
rebuilds the standalone executable, runs `build_editor_pair.ps1`, the two-cohort
native matrix and `check_character_materials.ps1`, using Windows PowerShell for
the established System.Drawing tooling. Preservation reports retain exact image
comparisons; no production asset, baseline or test threshold was changed.
