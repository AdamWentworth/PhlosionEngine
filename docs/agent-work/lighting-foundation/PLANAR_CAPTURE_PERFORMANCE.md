# Planar capture coverage and performance

2026-10-28. This is qualified groundwork with a small measured reflection-pass
saving, **not a meaningful whole-frame speedup**. The automatic D3D12/Vulkan
path keeps capture resolution, projection, refresh cadence, shader arithmetic,
quality budgets, reflected content, Hybrid rain, surface splashes and puddle
rings. The wet 16.67 ms target remains unmet.

## Change and dependency boundary

WorldPlanarCapture.h computes a conservative image rectangle from the primary
camera frustum intersected with the full accepted receiver-height slab. It
projects that domain onto the reflection plane and includes normal distortion,
roughness filter support, bilinear/edge clamping and the haze depth halo.
Singular/infinite views retain full coverage. No receiver domain retains one
unused pixel rather than relying on zero-area API behavior.

The backends apply this rectangle as a scissor, without changing viewport or
projection. Vulkan invalidates its viewport/scissor cache on capture entry/exit,
including same-size transitions. The quarter-size haze producer gets outward
rounded bounds in its own pixel coordinates and an extra coarse texel; the
main capture retains eight scene-depth pixels around the reflection support.
The haze composite returns to capture-space bounds. No GPU surfaces or public
interface changes are introduced. Full clear/copy and geometry submission remain.

This establishes safe shared bounds for later receiver/tile coverage and
secondary-view material/lighting reuse. It is independent Phlosion coverage
math, not a newly recovered Cyberpunk mechanism or completion of C7/C8.
The reflected scene still uses its separate forward material/light draw.
Main-view evaluated surfaces cannot simply replace it: offscreen reflected
geometry requires a valid secondary-view producer.

PokemonCyberpunk adds nested capture-begin, capture-sky, capture-haze and
capture-copy profiling spans in field and battle. Existing geometry/actor/
foliage spans remain. These labels exposed the real producer dependency instead
of treating the entire reflection pass as one cost.

## Matched performance

gpu-worker Windows, EVGA RTX 2060 SUPER index 1,
UUID GPU_1_UUID, adapter LUID
000000000000be8e. Optimized MSVC RelWithDebInfo, shader optimization 3,
native 1920x1080, Medium, night/overcast, identical camera and quality settings.
Wet uses heavy rain and wet surfaces; dry disables both at the same time/weather.
Validation/readbacks are disabled in timed runs. Frames 40-179 are measured;
the screenshot is after the window at frame 185. No concurrent build/processing.

The full-capture control includes the same new profiling scopes. Wet runs use
ABBA order, with one thermally flagged D3D12 candidate replaced by a separately
recorded clean run. Accepted wet pools contain 280 frames per variant/API;
dry contains one 140-frame run each. Every renderer/environment/diagnostic
setting is compared, excluding output and plugin paths: equality passes.

| GPU means (ms) | Full capture | Bounded capture |
| --- | ---: | ---: |
| D3D12 wet whole frame | 20.672 | 20.603 |
| Vulkan wet whole frame | 21.976 | 21.989 |
| D3D12 wet planar pass | 3.785 | 3.758 |
| Vulkan wet planar pass | 3.488 | 3.424 |
| D3D12 dry whole frame | 13.449 | 13.291 |
| Vulkan dry whole frame | 15.115 | 15.138 |

Planar savings are about 0.027 ms D3D12 / 0.064 ms Vulkan. Whole-frame variation
is essentially neutral; the inactive dry control also varies. These short runs
do not establish a sustained FPS gain. Weather still adds about 7.3 / 6.9 ms
GPU time in this fixture. There is no claim of a quality/performance parity increase.

| Accepted wet capture component (ms) | D3D12 | Vulkan |
| --- | ---: | ---: |
| Static reflected geometry/material/light work | 2.694 | 2.126 |
| Haze | 0.736 | 0.964 |
| Sky | 0.050 | 0.057 |
| Copy | 0.027 | 0.033 |
| Foliage | 0.173 | 0.180 |
| Actors | 0.070 | 0.061 |

Static geometry/light shading remains the largest reflected cost. Cheap below-
plane object rejection cannot remove much here: the scene survey found only
two of 196 primitives wholly below the plane (9,540 of 433,288 triangles).
The next substantive target is receiver-aware secondary-view surface capture
and material-classified/clustered lighting, keeping required specialized
materials and haze depth inputs. Avoid another sky/copy-only optimization.

GPU median/p95/p99: D3D12 full 20.464/22.204/22.355, bounded
20.497/22.256/22.436 ms; Vulkan full 21.884/23.367/23.476, bounded
21.750/23.347/23.532 ms. CPU-frame mean/p95/p99: D3D12 full
21.409/33.528/38.204, bounded 21.280/32.194/35.906 ms; Vulkan full
22.759/36.442/43.162, bounded 22.822/36.510/42.678 ms.
These include submission/waiting and do not prove smooth frame pacing.
All distributions, waits and pass nesting remain in analysis-matched.json.

## Correctness and visual qualification

- Two CPU suites pass: renderer policy and planar projection. An independent
  ray/slab oracle checks 10,760,064 bilinear samples over 200 cameras, including
  asymmetric/jittered, rolled, orthographic, extreme aspect and odd extents.
  It also checks haze upsample/depth support; singular/empty cases are explicit.
- 26 paired native cases (13 per API, 52 executions) match full capture exactly.
  Modes include on/off, offscreen, reversed winding, dry, clipping/occlusion,
  reset/extent change, emissive/rough and the new haze-inclusive regression.
- Twelve runtime/editor scene pairs cover wet, dry/day, orbit and battle.
  API/content checks pass and rain-state records match. Both hosts retain
  clustered/probe/surface composition without the redundant SSR source draw.
- Reviewed full-frame/crop images show no visible rain/lighting/content loss.
  **The strict one-byte final-scene diagnostic is not universally passed.**
  Four exceptions remain below; the diagnostic threshold was not changed.
- Frozen shader/material payload hashes match the previous accepted payload.
  Source changes do not alter assets, rain arithmetic or tier budgets.

| Strict scene exception | Maximum channel delta /255 | Pixels differing by >1 |
| --- | ---: | ---: |
| D3D12 runtime dry/day | 2 | 49 |
| D3D12 runtime orbit | 2 | 5 |
| Vulkan runtime wet | 7 | 318 |
| Vulkan runtime orbit | 5 | 69 |

Other final qualification pairs differ by at most one byte. The sparse residual
differences include the inactive dry control and are not fully attributed.
Acceptance is visual review plus native/CPU coverage evidence, not a claim of
bit-identical complete scenes.

Timing screenshots were reviewed separately. D3D12 wet pairs have maximum
deltas 30 and 9 (359 and 289 pixels >1); the repeated unchanged full-capture
control itself has maximum 30 at the same worst pixel, 377 pixels >1.
Vulkan wet pairs have maxima 3 and 4, while repeated control has maximum 3.
This demonstrates run variability without proving the cause of every pixel
difference. Rain states match. Raw diagnostics and strict failures are retained.

## Rejected trials and evidence

The first scissor incorrectly reused capture pixels in the quarter-size haze
target. That discarded needed samples and triggered expensive analytic fallback:
haze grew from roughly 0.74 to 1.66 ms D3D12 and 1.03 to 1.95 ms Vulkan.
The producer-space mapping and depth halo fix that regression. The native
haze-inclusive full-reference comparison is exact on both APIs.

timing2 comparisons are discarded: a pre-existing phase-name rule set two
legacy diagnostic flags differently in controls named diagnostic-full-*.
A source search did not establish these flags caused a speed difference.
The mismatch alone invalidates the comparison. timing3 uses forced equal
settings and an explicit equality gate.

timing3-d3d12-wet-qualified-2 is excluded because telemetry reported a software
thermal flag before the screenshot (62 C, hardware flag inactive). The clean
replacement is qualified-3; the excluded data remains. No driver, fan, clock
or power settings were changed.

Controller evidence: artifacts/lighting-foundation/planar-capture-20261028/.
Worker source evidence and frozen binaries:
WORKER_EVIDENCE_ROOT/phlosion-planar-capture-20261028/.
Key receipts: qualification-qualified.json, visual-review-qualified.json,
visual-review-timing.json, corrected-build.json, CPU/native logs,
analysis-matched.json, final-summary.json, final-shader-equivalence.json,
source transfer manifests and post-integration checks. Rejected measurements
are retained separately by their original names. qualified is the shipping
payload; older candidate/final payloads are not accepted.

Engine baseline: 24c9a2451bc1e75be1021bad04e708e5c19cdd85.
Game baseline: 6217c0474f4f80aa3b728c6c8bbae062e5d92d2c.
Controller owns integration/commits; all edits, builds, processing and tests ran
on gpu-worker. No local controller GPU/build fallback.

Linux, broader quality/hardware matrices and long-session pacing are not
qualified by this pass. Complete-technique gaps and previous percentage estimates
remain unchanged. No new optional product setting was added.
