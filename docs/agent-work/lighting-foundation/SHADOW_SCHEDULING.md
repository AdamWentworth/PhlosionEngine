# Local-shadow cache ownership and spatial invalidation

Task baseline: engine 5eb7f60913eed18b8fa5e3a66a7cda31c127d612 and game
7a4274207a00c411e786d6128de7d97136b242c0. Work is performed and qualified on
gpu-worker; controller owns reviewed source integration. This is a C8 shadow-cache
milestone, not completion of C8 or the complete probe/shadow scheduler.

## Behavior

Local shadow atlas assignments now remain attached to surviving light indices.
The renderer reserves the whole admitted set before assigning new sources.
Turning off an early source, reordering requests or replacing a source therefore
does not move the other lights to different tiles. The current eight-map capacity
and admission order are unchanged. Full light-identity/version records remain
a broader C7/C8 boundary; this uses the current public light array index.

Static-caster revisions in field and battle are now computed per light from
the immutable scene generation and visible primitive membership/transforms.
The digest and actual draw share the same conservative homogeneous box test
and matrix multiplication. A caster entering OR leaving the light volume
changes the digest. Unknown/invalid bounds stay included. Skins, animated assets,
mismatched snapshots and unsupported caches retain the fresh path. Asset reloads
invalidate conservatively. Excluding a caster invalidates each affected light.
Each field/battle owner retains these spatial digests. The global scene/pose/
exclusion digest is checked once per draw; unchanged light projections reuse
their spatial digest. Scene changes recompute membership, while only lights whose
actual caster contents changed need a new static shadow map.

Cached data still requires exact scene identity, static revision, light identity,
projection, distance convention, map size and physical atlas slot. It is only
published after a completed writer. Explicit refresh/reset and exception cleanup
are retained. Receiver and moment-filter addressing both use the selected tile.

Dynamic actors, moving foliage and other fresh casters still render every frame.
Light movement/range/filter changes still invalidate their projection. Brightness
and color changes do not change depth and continue to affect direct lighting
immediately. No stale-shadow grace period, map-size reduction or new quality
toggle is introduced. Sun shadows, rain and reflection equations are unchanged.

## Source mapping and remaining work

The inspected snapshot's renderShadowManager.cpp:63-97 separates source
assignment and static/dynamic invalidation. Its collector and frame scheduling
are described in Cyberpunk2077Research/lighting/quality/README.md. This milestone
improves Phlosion's ownership and spatial dirty detection; it does not claim the
source's complete job queue, static/dynamic refresh budgets or arbitrary scene
light capacity.

The next probe producer is materially different from our existing capture:
renderReflectionProbeManager.cpp:943 consumes stored probe data and current
lighting; :968/:1062 process three faces per ordinary relight call (all six for
full refresh); :2373 consumes the streamed cubemap data and neighboring probes;
:2792 chooses non-processing resident probes by prewarm, age and distance.
A nearby comment saying one face is not the executed three-face bound.

Phlosion still draws six camera-following views and filters their color each
frame. Its shared evaluated surfaces already export albedo, mapped normal,
metal/F0/AO/roughness and emission, but their current lifetime is per frame/view.
The next connected delivery needs persistent placed-probe data, valid geometry/
material invalidation, mesh-free relighting with current lights/sky/shadows,
completed-generation publication, convolution, spatial blending and scheduled
updates. Wetness and moving-object coverage require explicit contracts.
Throttling the existing radiance recapture alone would not deliver that technique.

No category estimate is raised for this milestone. Probe scheduling, independent
shadow refresh budgets, Linux qualification and the 16.67 ms target remain open.

## Qualification

Optimized RelWithDebInfo builds passed on Windows gpu-worker using the EVGA
RTX 2060 SUPER (UUID GPU_1_UUID,
LUID 000000000000be8e). The controller performed review/integration only.

- Five CPU CTests passed, including atlas lifecycle, static cache ownership,
  shared glTF culling, field integration and the new spatial/memoized key checks.
- Sixty native D3D12/Vulkan runs cover ten scenarios in cached, fresh and
  forced-refresh modes. All forty image comparisons are pixel-identical.
  Fourteen additional clustered-shadow GPU cases passed with validation.
- Source-off warms exactly eight static maps on D3D12 and sixteen on Vulkan's
  two independently fenced targets, with no surviving-light rebuild. Replacing
  one light adds only one map per target (nine/eighteen total).
  Dynamic passes retain the same 120/128 counts as the corresponding fresh runs.
- Twelve final runtime/editor scene comparisons cover wet field, dry field,
  camera movement and battle on both APIs. D3D12 is pixel-identical; Vulkan
  differs by at most 1/255, within the unchanged limit. Validation found no GPU
  errors. Clustered stages remain connected and no redundant screen-reflection
  source draw returned.
- All six replacement-mode cases are registered in CTest. The native scenarios
  preceded the owner memoization; the final rebuilt CPU and full-scene checks
  qualify that additional game change. No renderer equations changed.

The first revision recomputed spatial membership each frame and added CPU work.
That revision is not the accepted result. The owner now memoizes spatial keys
against the existing scene/pose/exclusion digest and exact light projection.
Changed or unsupported inputs still invalidate; this is not a delayed refresh.

### Warm frame-time regression checks

Native 1920x1080, Medium, one named GPU, validation disabled, frames 40-179
(140 samples per run). Each revised candidate is paired with the committed C7
baseline. No readback occurs in that measured interval. All telemetry through
frame-185 completion has software/hardware thermal slowdown inactive. Raw
telemetry, clocks, temperatures, build/payload hashes, CPU/present distributions
and logs are retained. These short paired runs are regression checks, not a
statistically established steady-scene speedup.

| API / scene / version | GPU mean | median | p95 | p99 (ms) |
| --- | ---: | ---: | ---: | ---: |
| d3d12 / wet / C7 baseline | 28.40 | 28.41 | 28.97 | 29.28 |
| d3d12 / wet / revised cache | 28.51 | 28.51 | 29.13 | 29.31 |
| d3d12 / dry / C7 baseline | 19.89 | 20.91 | 21.29 | 21.41 |
| d3d12 / dry / revised cache | 20.81 | 21.01 | 21.29 | 21.34 |
| vulkan / wet / C7 baseline | 30.60 | 30.64 | 31.34 | 31.53 |
| vulkan / wet / revised cache | 30.61 | 30.61 | 31.34 | 31.51 |
| vulkan / dry / C7 baseline | 23.03 | 23.06 | 23.41 | 23.57 |
| vulkan / dry / revised cache | 22.25 | 22.23 | 23.15 | 23.46 |

| API / scene / version / measurement | mean | median | p95 | p99 (ms) |
| --- | ---: | ---: | ---: | ---: |
| d3d12 / wet / baseline / cpu_frame_ms | 31.11 | 27.96 | 44.68 | 49.19 |
| d3d12 / wet / baseline / present_ms | 0.17 | 0.17 | 0.22 | 0.26 |
| d3d12 / wet / revised / cpu_frame_ms | 30.95 | 28.34 | 43.81 | 44.87 |
| d3d12 / wet / revised / present_ms | 0.17 | 0.16 | 0.22 | 0.26 |
| d3d12 / dry / baseline / cpu_frame_ms | 20.80 | 16.81 | 31.33 | 36.64 |
| d3d12 / dry / baseline / present_ms | 0.19 | 0.17 | 0.25 | 0.64 |
| d3d12 / dry / revised / cpu_frame_ms | 21.93 | 20.55 | 35.33 | 41.04 |
| d3d12 / dry / revised / present_ms | 0.18 | 0.17 | 0.24 | 0.47 |
| vulkan / wet / baseline / cpu_frame_ms | 33.34 | 30.67 | 46.08 | 50.55 |
| vulkan / wet / baseline / present_ms | 0.05 | 0.05 | 0.07 | 0.09 |
| vulkan / wet / revised / cpu_frame_ms | 33.32 | 30.57 | 46.50 | 51.84 |
| vulkan / wet / revised / present_ms | 0.05 | 0.05 | 0.07 | 0.11 |
| vulkan / dry / baseline / cpu_frame_ms | 24.18 | 22.78 | 36.80 | 43.75 |
| vulkan / dry / baseline / present_ms | 0.05 | 0.05 | 0.06 | 0.08 |
| vulkan / dry / revised / cpu_frame_ms | 28.68 | 30.18 | 33.59 | 39.33 |
| vulkan / dry / revised / present_ms | 0.08 | 0.07 | 0.11 | 0.25 |

| API / scene / version | Shadow CPU wall mean | median | p95 | p99 (ms) |
| --- | ---: | ---: | ---: | ---: |
| d3d12-wet-baseline-2 | 2.78 | 1.80 | 4.69 | 6.00 |
| d3d12-wet-candidate-r2-2 | 2.68 | 1.79 | 4.37 | 4.62 |
| d3d12-dry-baseline-2 | 2.72 | 2.21 | 4.56 | 5.42 |
| d3d12-dry-candidate-r2-2 | 2.84 | 2.22 | 4.93 | 6.02 |
| vulkan-wet-baseline-2 | 2.93 | 1.96 | 4.90 | 5.88 |
| vulkan-wet-candidate-r2-2 | 2.82 | 1.96 | 4.90 | 5.65 |
| vulkan-dry-baseline-2 | 2.84 | 2.27 | 4.79 | 6.72 |
| vulkan-dry-candidate-r2-2 | 3.81 | 4.01 | 4.43 | 5.36 |

The direct benefit is bounded work when lights/casters change, not a claimed
steady-scene FPS increase: unchanged scenes already reused static maps.
The 16.67 ms target is still unmet. Complete placed-probe relighting and shadow
refresh budgets remain C8 work; category planning estimates are unchanged.

Final scene/payload generation is candidate-r2 with shadow-*-r3 captures.
Earlier attempts and the caught CPU regression are retained as pre-memo-*;
the initial stale editor project was rejected by its hash guard before launch
and fixed with an isolated task project. Final tested non-document source
hashes are in qualified-source.json; delta.json verifies these before handoff.
Evidence is under WORKER_EVIDENCE_ROOT/phlosion-probe-shadow-scheduling-20261027 on
gpu-worker and PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/probe-shadow-scheduling-20261027/final-return on controller.


### Daylight timing follow-up

The main Vulkan dry candidate had a 30.18 ms CPU-frame median versus 22.78 ms
for its paired baseline, despite a lower GPU time. This sample is retained above,
not discarded. Two additional candidate/baseline then baseline/candidate pairs
did not reproduce that CPU-median increase:

| Version / repeat | GPU mean / median / p95 / p99 | CPU-frame mean / median / p95 / p99 (ms) |
| --- | ---: | ---: |
| candidate-r2 / 3 | 22.84 / 22.82 / 23.20 / 23.45 | 24.44 / 22.59 / 37.00 / 45.16 |
| baseline / 3 | 22.80 / 22.77 / 23.39 / 23.57 | 25.26 / 22.70 / 36.93 / 38.68 |
| baseline / 4 | 22.97 / 23.03 / 23.38 / 23.55 | 24.00 / 22.72 / 36.24 / 44.70 |
| candidate-r2 / 4 | 22.93 / 22.96 / 23.38 / 23.51 | 24.60 / 22.62 / 37.72 / 43.94 |

Both pairs remain thermally valid. The added thread-CPU diagnostics quantize to
15.625 ms on this host, so their individual phase medians are unsuitable for
fine CPU-cost claims; raw values are retained in dry-repeat.json. The repeated
whole-frame measurements support accepting the cache change, but do not
establish a general CPU speedup or fix frame pacing.

D3D12 daylight's main-run means are 19.89/20.81 ms while medians are
20.91/21.01 ms and p95 is 21.29 ms in both versions. Preserve the existing mixed
daylight-distribution limitation documented in SSR_QUALITY_PERFORMANCE.md.
Do not turn these short paired observations into a universal performance gain.
