# Repeated shadow submission cost

2026-10-17, starting from engine ebe4d1b / game b23efa5. Cloud-only root work.
Evidence is under `artifacts/lighting-foundation/shadow-submission` in the engine,
game and Autochess. Scene, lighting, shaders, geometry, weather and source assets
are unchanged. This pass improves Vulkan CPU submission, not lighting parity.

## Diagnosis and change

The initial Vulkan diagnostic run averaged 76.542ms host wall time and 50.078ms
current-thread CPU time across 200 warm frames. Local shadows averaged 18.927ms
wall and 19.141ms CPU. Windows thread accounting has coarse resolution: these
averages establish CPU work but individual short-span values are not precise.
The game capture harness now exposes the existing deferred CPU diagnostics with
`-PerformanceDiagnostics`, requiring `-Performance`; normal play remains unchanged.

Temporary per-function instrumentation, removed before final timing, found:

| Vulkan scope | Mean wall ms/frame | Calls/frame |
| --- | ---: | ---: |
| Cached geometry lookup, including index validation | 6.940 | 1110 |
| Index validation alone | 6.183 | 1110 |
| Material lookup, including texture identities | 24.881 | 1112.705 |
| Descriptor/state preparation | 6.478 | 1112.705 |
| View-state cache lookup | .768 | 1112.705 |
| Specialized material state lookup | .057 | 1112.705 |
| Skin palette lookup/upload | 3.019 | 241 |

Scopes nest and must not be summed. The profiled control and trial use identical
instrumentation over frames 30-229. Vulkan's state-cache log shows seven palette
uploads and 234 reuses per representative frame. View-state linear search was
not the principal bottleneck; do not replace it based only on the packet size.

Replace `ostringstream` texture-key formatting with reserved string appends and
`to_chars`. Preserve explicit cache-key precedence, source-name fallback,
anonymous pixel-address identity, dimensions, wrap modes, mip count and color
space. Resource scopes and every cache lookup/lifetime remain as before. The
formatter is locale-independent. It preserves legacy spelling in the engine's
normal classic locale; unlike streams, changing a global number-formatting locale
cannot silently change numeric cache identities. No GPU data or commands change.

The instrumented trial reduced material lookup to 6.582ms/frame. A clean repeat
with only the existing game diagnostics reduced mean current-thread CPU time
from 50.078 to 30.156ms and host median from 74.058 to 49.282ms. The diagnostic
comparison image is pixel-identical. The temporary profiler header and patch are
retained as artifacts, not production code.

## Matched production timing

RTX 2060 SUPER GPU 0, driver 617.42, 1920x1080 heavy-wet-night station, same front
camera, Hybrid rain, 45 seconds accumulated wetness. Sequential native runtime
runs, no competing game/editor or local AI. Each result uses 200 warm samples,
frames 30-229. Both sides have deferred wall tracing and CPU diagnostics disabled.

| API | GPU control/candidate ms | Host control/candidate ms |
| --- | ---: | ---: |
| OpenGL | 38.346 / 38.584 | 40.621 / 40.929 |
| Vulkan | 23.960 / 18.365 | 73.569 / 49.943 |
| D3D12 | 19.753 / 19.842 | 19.894 / 19.761 |

Vulkan host median improves 23.626ms (32.1%). Local-shadow submission median falls
18.410 to 10.802ms. Its remaining host p99 is 76.150ms; this is not sustained
60 FPS. The independent diagnostic repeat agrees with the host improvement.
All three timing images and scene states are identical to their controls.
GL/D3D12 have no implementation change and their small differences are variation.
The default D3D12 launch does not gain this Vulkan optimization.

GPU duration also falls in these Vulkan runs, but no GPU algorithm changed.
Do not present that whole difference as reduced shader work. The retained
nvidia-smi monitors lose their final buffered samples when stopped and cannot
establish clocks throughout each measured interval. CPU time is the stronger
evidence for this change. Cold startup is excluded and remains a separate issue.

Reproduction: game `run-matched.ps1 -Variant control|candidate` uses
`Verify-SkyWeather.ps1 -OutskirtStation -Performance -OnlyHost runtime
-OnlyAspect 16x9 -OnlyPreset night -CameraPitch .10 -RainStyle hybrid-experiment
-WeatherPreset overcast -RainPreset heavy -WetnessReviewSeconds 45`, with the
retained `frame-cost/view-front.json`. `comparison.json` is produced by
`verify_lighting_frame_cost.py`; it rejects changed API, GPU, scene state, image
pixels, diagnostic settings, incomplete samples and native failures. Raw logs
retain startup, all scopes and tails. `diagnostics.json` retains both repeats and
temporary-profile runs; engine `verify-results.py diagnostics` reproduces it.

## Verification

56 engine checks and five game environment/weather contracts pass. The new CPU
contract covers cache/source precedence, empty/null names, anonymous addresses,
all key fields, signed/unsigned integer limits, 3,750 legacy spelling cases and
global-locale independence. The diagnostics switch rejects use without performance
capture. Temporary profiling is absent from production sources and binaries.

All 36 public reflection images, 27 station/battle/control/signage images and
twelve shared character images exactly preserve the preceding checkpoint. Existing
expected-content and API/runtime/editor checks pass without threshold changes,
including the independent fixture/reflection oracles and twelve ray/box shadow
on/off probes. Warm resource totals also match on every API. These comparisons
are against each API's preceding image, not a claim of zero cross-API error.

Both engine editor directories, game/runtime/model plugins and the separate
Autochess runtime/editor pair are rebuilt. The normal D3D12 project launch passes;
staged game/model DLL hashes match their build outputs. All hidden native sessions
exited. Canonical Blender and station GLB hashes are unchanged, as are the six
healthy canopy sources, 120s daylight and gradual 180s Hybrid weather defaults.
PokemonModelSupport and Autochess source remain unchanged.

The retained `run-final-engine.ps1`, `run-final-game.ps1` and
`run-final-shared.ps1` scripts reproduce the checks. Engine `verify-results.py`
reproduces the exact image, diagnostic and resource comparisons; JSON reports,
native logs, source hashes and paired-build proof remain beside the captures.
The final shared script uses Windows PowerShell for the existing System.Drawing
tools. No capture threshold or baseline is relaxed.

Remaining measured opportunities are repeated cached-geometry index validation,
material binding lookup and skin-palette reuse. Geometry validation still runs
on every draw in this pass; any follow-up must preserve invalid-input handling,
scope retirement and immutable-key semantics. No static-shadow caching or
animation freezing is introduced. The remaining gap between thread CPU and host
wall time also warrants a separate synchronization/scheduling investigation.
