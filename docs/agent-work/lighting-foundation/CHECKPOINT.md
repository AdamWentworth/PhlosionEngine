# Lighting foundation checkpoint

2026-10-17. Fixture emission, local shadows, HDR, static shadow culling and colored
diffuse bounce are functionally verified, including native scene, shared-consumer
and normal-launch checks. Performance limitations remain below. Earlier phase
notes are historical. Checkpoint commits were authorized on 2026-10-17; no
numerical Cyberpunk parity is claimed.

## Colored diffuse bounce — latest verified pass

The engine bakes one diffuse bounce from static glTF geometry into 32 directional
irradiance probes beneath the canopy. Six independent unit-source bases are
relit from live pink/blue fixture radiance; source-off gives exactly zero bounce.
Characters and shaded surfaces receive the result. Generic bake, math and all
three backend bindings belong to the engine; volume/source selection belongs to
PokemonCyberpunk. No new fill lights or authored asset changes. Moving objects
do not update transport, and this is not full GI or scene reflections.

Four engine CPU contracts, five game contracts, nine native HDR tests and 64
public/station/wet/battle/control captures pass with unchanged limits. Twelve
Autochess runtime/editor material captures pass; five editor images match the
earlier HDR baseline exactly, and the sixth differs by one channel byte. The
standalone Autochess executable was rebuilt explicitly; an earlier stale-binary
attempt was rejected. Scene switching passes on all APIs, five visits each,
including clearing bounce in the Colosseum reference. The final normal D3D12
launch passes and retains the 120s day / 180s Hybrid rain cycles and CP audio.

Fresh 1080p clear-night timing on RTX 2060 SUPER, Cyberpunk closed, 200 warm
samples per control, all shadows/HDR/bloom retained:

| API | GPU bounce off/on ms | Host off/on ms |
| --- | ---: | ---: |
| OpenGL | 26.336 / 27.602 | 28.636 / 29.841 |
| Vulkan | 14.666 / 14.970 | 49.956 / 48.739 |
| D3D12 | 10.363 / 11.931 | 10.689 / 11.939 |

D3D12 reverse-order repeat: GPU 10.474 / 11.683ms, host 10.773 / 11.962ms.
Its added GPU cost spans 1.21-1.57ms, with the first result slightly above the
existing +1.5ms review guide; both results remain recorded. No threshold change.
These medians do not establish sustained 60 FPS: host p99 exceeds 16.667ms on
every API, with GL throughput and Vulkan CPU-side cost still needing work. The
toggle measures current-shader execution, not total cost versus a pre-change
binary. The machine changed from GTX 1070 to two RTX 2060 SUPER cards; do not
compare those generations as an optimization result. Only GPU 0 rendered.

Vulkan now reuses and persists a bounded, device/driver-validated pipeline cache.
CPU corruption/compatibility contracts and native load/store checks pass. Warm
normal editor launches were about 34s in retained logs; first material compiles
can still take minutes. This pass used cloud execution only, without local AI
or delegation. See [BOUNCE.md](BOUNCE.md) for architecture, exact limitations,
startup evidence and game `artifacts/lighting-foundation/bounce/rtx2060` reports.

The preservation snapshot has 422 files: 387 unchanged, 35 intentional engine/game
overlaps and 14 new files. Model-support and Autochess source are unchanged.
Canonical Blender and private station.glb hashes remain unchanged. Binaries are
rebuilt/staged and this work is checkpointed at the user's request. Next visual milestone: material-aware
scene reflections, including off-screen fallback; the current wet highlights
still represent light sources rather than reflected scene geometry.

## Static shadow culling — previous accepted pass

Generic glTF import now computes primitive bounds from decoded positions. The
draw adapter optionally rejects static PBR primitives wholly outside the supplied
pass frustum, before material/backend submission. Field and battle supply each
actual shadow matrix for the environment only. Skinning, custom vertex materials,
prewarm and absent/invalid bounds retain their existing paths. Shaders, light
settings, source geometry, rain and camera behavior are unchanged. No new workers;
root continues under the user's recorded implementation fallback approval.

Four engine CPU tests (including the expanded import/draw contract) and five game
environment contracts pass. All 19 station/battle/control images are pixel-identical
to the accepted HDR baseline; six low-sun API/host captures and the culling-off
control pass. Existing source/packet, battle, wet reflection and bloom validators
pass. Scene switching passes on all three APIs (five visits each); the final normal
D3D12 launch passes and restores the station, 120s day cycle and 180s Hybrid weather
cycle. Engine `build-presentation`, game runtime/editor and model provider targets
were rebuilt and staged together. No shader/backend change required repeating the
previous Autochess material matrix; its separate paired binaries were not replaced.

Matched clear-night 1080p tests retain HDR, bloom and all shadows, 200 warm samples:

| API | GPU off/on culling ms | Host frame off/on culling ms |
| --- | ---: | ---: |
| OpenGL | 41.115 / 36.573 | 42.801 / 38.233 |
| Vulkan | 23.194 / 22.778 | 63.291 / 48.456 |
| D3D12 | 21.721 / 12.701 | 21.982 / 12.943 |

Reverse-order D3D12 repeats at GPU 21.632 / 13.280 and host 21.939 / 13.872 ms.
GL/Vulkan performance images are identical; D3D12 has maximum one-byte difference.
These are medians, not sustained 60 FPS or rain/battle guarantees. Frame spikes and
Vulkan host cost remain. Host timings include waits. See SHADOW_CULLING.md and game
`artifacts/lighting-foundation/shadow-culling` for exact evidence and limitations.

Before-state snapshot includes 117 files across four repositories (dirty work and
affected clean inputs). 106 are byte-identical; the eleven intentional changes are
five engine glTF/test files, game field/battle presentation, its capture helper and
test plan, and this checkpoint/PLAN. Earlier lighting/material/model edits remain
intact. New files are the culling design and two game verification tools. All
changes remain uncommitted. Next visual work is colored indirect light and actual
scene reflections; further Vulkan performance investigation remains worthwhile.

## Current changes

PokemonCyberpunk `tools/outskirt_station_lighting.py` rotates pink physical
diffusers/channels/clips to emit outward and 60 degrees down, below the fascia.
All six source colors/radiance/emission/condition settings are preserved, with
no added lights. Blue and non-canopy fixtures, point lights, shelters and puddles
match the retained baseline. The normal private station export was regenerated;
canonical Blender source hash is unchanged, 196 meshes / four pumps / four columns.
`verify_station_rect_sources.py` now checks visible normals, corner joins,
18 independent interior area-integral samples at two heights, exterior receiver
orientations and a zero-direct-light back-facing control. New
`Verify-Canopy-Overlap.ps1` captures four isolated source modes under the canopy.
Game HANDOFF, TEST_PLAN and EMISSION_PASS document this step.

No engine rendering code or ABI was changed in this phase. All 52 saved pre-existing
modified/untracked files across the four repositories still matched their baseline
copies at review. The earlier material ownership cleanup remains uncommitted.

## Verification complete

- Exported source geometry/normals/extent/corner oracle passed (maximum centreline
  corner error 0.000001404 m). All 18 interior and external orientation checks pass.
- OutdoorProfile and WorldEnvironment CTests pass, two of two.
- All six wet-night station runtime/editor captures passed the existing source
  packet, fixture, allocation and parity oracle. Maximum API MAE 1.36518/255
  (limit 1.5); maximum host MAE 0.00000343/255.
- All seven D3D12 controls completed. Existing analytic reflection, puddle/dry
  state and localized-bloom checks passed at front and oblique camera angles.
- All six rainy battle runtime/editor captures passed the existing geometry,
  fixture, weather, shelter, allocation and backend parity checks.
- All four under-canopy source controls passed matching player/camera/import
  state, dry surfaces, zero point radiance and expected rectangle counts. The
  reviewed character interior has 1,303 pixels responding to each source group
  independently out of 2,408 sampled pixels. The close camera retracts through
  the established collision behavior; no new camera behavior is claimed.
- Visually reviewed source-off, pink-only, blue-only, both, dry/oblique and battle
  views. Final total: 23 native captures and two CPU contracts, all passed.
  No performance qualification or new generic renderer feature is claimed.

Evidence is in game `artifacts/lighting-foundation/emission`; stage logs are
`artifacts/lighting-foundation-emission-{matrix,controls,battle,overlap}.log`.
Native exec sessions 33367 (export), 68476 (station matrix) and 9420 (remaining
captures) exited successfully. `overlap/comparison.png` and `verification.json`
retain the source-isolation evidence. Changes are left uncommitted.

## Routing and remaining renderer work

The installed doctor saw root gpt-6-astra and pinned astra_flash_builder. Actual
worker `/root/canopy_emission` failed immediately with HTTP402 insufficient credit
and was interrupted. Root implemented the fixture bundle under the user's existing
approval to continue with the current model. No worker implementation was accepted.

At phase 1 closure, local shadows, HDR, colored indirect light and scene reflections
were unimplemented. That review identified RGBA8 world targets, per-material
mapping and format-bound PSOs as constraints. The following shadow/HDR sections
record their implementation and compatibility checks. Colored indirect light and
scene-object reflections remain later work. No new Cyberpunk capture or hardware
RT was required for the fixture step.

## Local shadows (2026-10-16)

User authorized proceeding toward the approximately 75% conventional lighting
milestone. LOCAL_SHADOWS.md specifies the first renderer bundle. Current work is
uncommitted and functionally verified, with performance limitations below. Pre-existing 60 files across four repositories
were copied with hashes to artifacts/lighting-foundation/pre-local-shadows.

Implemented optional IWorldLocalShadow, shared atlas/projection policy, 4096 D32
atlas (2048 sun plus eight 1024 local tiles), three backends, shader visibility
for rectangular diffuse/specular contributions, field/battle/calibration routing.
Added bright public `RectLightCalibration shadow` plus independent ray/box oracle.
Corrected foliage CPU culling to use each actual light matrix (including sun).
At this stage there were no HDR/bloom or indirect/reflection changes. Root performs work under
prior approval after the already documented DeepSeek 402 failure; no new worker.

Initial and final engine/game builds passed. PhlosionLocalShadows,
PhlosionRectLights, CyberpunkOutdoorProfile and CyberpunkWorldEnvironment pass.
All twelve public probe on/off captures and all policy controls pass the
independent ray/box oracle. The skinned control was corrected to compare against
its matching moved-pose off capture (not the stationary cube); no image threshold
was relaxed. D3D12 has 294 shadow samples with median reduction 64/255 and 1,416
clear samples with median change zero. Twelve dry station on/off captures also
pass API/host parity, maximum API MAE 0.60834/255, with no allocation failures.

OpenGL's first local-shadow run exposed a major cost regression: roughly 212 ms
versus 58 ms off at 832x468. Temporary opaque-only, receiver-disabled and foliage-
disabled probes isolated the dominant problem to the vertex material dispatcher
for repeated wind-deformed shadow geometry. Specializing BOTH vertex and fragment
material selectors preserves the same shader/discard/skinning path and produces
an identical station image (MAE 0, max 0), at approximately 60 ms with shadows.
Fragment-only specialization did not solve it. The temporary opaque program,
receiver bypass and palette-unroll experiments were removed. The accepted approach
caches generic per-material shadow programs and prepares them through the existing
material prewarm path. It contains no station coordinates or foliage mode numbers.
All GL public probe/policy controls were rerun after this change and pass.

The user closed Cyberpunk 2077. Clean paired 1080p tests supersede all earlier
provisional timings: game `artifacts/lighting-foundation/local-shadows/clean-gpu/
performance-summary.json`. Clear night, bloom off, 200 warm samples [30,230):

| Backend | GPU off/on ms | CPU host off/on ms |
| --- | ---: | ---: |
| OpenGL | 18.622 / 36.435 | 20.266 / 38.137 |
| Vulkan | 15.212 / 21.205 | 28.619 / 72.622 |
| D3D12 | 8.544 / 19.962 | 9.478 / 20.272 |

These shadow-only measurements predate HDR. They establish substantial GPU and
CPU cost, not 60 FPS qualification. Vulkan local submission alone is 36.708 ms.
Retain the original failed/experimental logs. Wet station (six), rainy battle
(six), and D3D12 off/dry/day/oblique controls (seven) completed and passed existing
validators. Wet maximum API MAE is 1.31/255 at the unchanged 1.5 limit.

## HDR scene acceptance

RGBA16F world targets and compatible direct/scene pipelines are implemented on
all three APIs and editor surfaces. Optional IWorldSceneRadiance resets per view;
the game opts outdoor views in. Shared bloom receives unclipped radiance, then
ACES, sRGB and display dithering run once. Sky remains unclipped. Feedback maps
into its existing RGB8 history before authored feedback. Other consumers retain
their mapping convention (float intermediate storage can change quantization).

All nine native intensity/blend/bloom/feedback tests and three legacy feedback
tests pass. Eight engine CPU/shader architecture checks and five game environment
contracts pass. Original failures are retained: SDL_setenv did not set the CRT
capture environment; the feedback test then exposed a real OpenGL first-draw
buffer initialization bug, now fixed. Sky clamping found during review is fixed.

Engine build-presentation editor, game runtime/plugin and model providers are
rebuilt and staged. Autochess engine-build editor/plugin paired proof and separate
runtime rebuild pass. The first paired build was correctly rejected because
source changed during compilation; the stable second build passes.

Final HDR wet station (six), rainy battle (six), and D3D12 controls (seven) pass
under game `artifacts/lighting-foundation/hdr/native`, with unchanged validators.
Maximum API MAE is 1.30917/255; maximum runtime/editor MAE is 0.0000155/255.
The independent front/oblique emitter projection oracle, puddle/dry response and
localized halo checks pass. Visually reviewed night, lights-off, daylight and
battle images. All nine native radiance cases pass again after strengthening the
test to delete stale screenshots and reject exposure above the supported range.

Final clean 1080p timing with HDR enabled, bloom off, 200 warm samples [30,230),
all GPU runs sequential and Cyberpunk closed:

| Backend | GPU shadows off/on ms | CPU host off/on ms |
| --- | ---: | ---: |
| OpenGL | 22.529 / 39.054 | 24.163 / 40.777 |
| Vulkan | 15.316 / 20.984 | 26.206 / 64.490 |
| D3D12 | 14.852 / 20.214 | 14.919 / 20.422 |

D3D12 with normal bloom enabled: GPU 21.536 ms, CPU host 21.828 ms. These are
clear-night station measurements, not rain/battle/whole-game frame guarantees.
Do not infer isolated HDR overhead from separate earlier runs. This is not a
60 FPS setting across APIs. Vulkan's local-shadow CPU submission is 29.850 ms;
repeated geometry submission and GL's shadow cost remain optimization targets.
Full raw samples and matching-camera/import checks are in game
`artifacts/lighting-foundation/hdr/final-performance/performance-summary.json`.
RGBA16F adds four bytes per world-target pixel (~7.91 MiB at 1080p); opting into
the 4096 D32 shadow atlas adds 48 MiB over the original 2048 map. No allocation
failures occurred. The settings allocate targets on preparation/resize, not per
frame.

Preservation audit: 53 of 60 pre-existing dirty files remain byte-identical. The
seven intentional overlaps are three generic material HDR gates, engine CMake,
PLAN/CHECKPOINT, and game TEST_PLAN. The three material files differ from their
pre-HDR copies by one radiance-mode gate each; prior ownership cleanup is intact.
All other pre-existing model/Autochess edits remain unchanged.

All three API scene-switch checks pass (five station/reference visits each).
The final D3D12 normal launch passes and restores Hybrid rain / day-night defaults.
Game logs are `artifacts/lighting-foundation/hdr/scene-switch-*.log` and
`normal-launch.log`. Both engine editor build directories and staged game/model
plugins are current; no visible editor was left running.

Autochess native checks passed: six runtime cohort images and six editor body/eyes
and stage images, including comparison against the material-ownership baseline.
Existing content, API parity and baseline limits all pass. Visually reviewed a
runtime cohort and the editor model view. Final staged Cyberpunk/model DLL hashes
match their rebuilt artifacts, and engine/game/Autochess diff checks pass.
The first artifact-only retry helper failed because its new results directory did
not exist (the historical helper assumed an earlier capture run had created it).
The PNG and error are preserved; explicit directory creation fixed log capture.
The production capture helper and image limits remain unchanged. Runtime startup
is allowed 450 seconds for cold shader variants, as in the earlier qualification.
No engine/game source change was required for this harness issue. Final summaries
are in Autochess `artifacts/lighting-foundation/hdr/runtime/captures/matrix-report.json`
and `editor/report.json`. All native sessions have completed and no game/editor
process was left running. The next engineering priority is reducing repeated
shadow submission cost before increasing lighting complexity.
No colored indirect bounce or scene-object reflections are implemented. Do not
claim the approximately 75% target as measured parity. All work is uncommitted.
