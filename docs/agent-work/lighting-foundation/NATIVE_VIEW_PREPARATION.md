# Collected native model view preparation

Date: 2026-10-30. Baseline engine 00a465c, game 9335492, ModelSupport da6a969.
All implementation edits, optimized builds, tests and processing ran on
gpu-worker Windows. controller owns review, integration and commits.

## Connected preparation

This continues NATIVE_MODEL_RESOURCES.md. The existing reference anchor is
renderMaterial.cxx / PrepareRenderStagePassesRange: retain prepared descriptions
for an owned allocation-frame/range instead of rebuilding them at every
consumer. This independent implementation applies that lifetime rule to
Phlosion's collected native-model views. It does not copy the complete source
scheduler or establish current retail-image/performance parity.

A collected frame now owns a bounded table of prepared batch descriptions.
The key contains camera position/up/facing, the complete copied environment
representation and lighting preset. Geometry, pose, material animation and
translucency remain owned by the immutable collection snapshot. New collection
means fresh preparation. A changed camera rebuilds billboard facing and sorting;
changed environment or preset reruns lighting preparation.

The table holds at most sixteen entries per collected object, replacing the
oldest slot when full. The existing provider also caps collected objects at 256.
Only successfully completed descriptions are published. Failed preparation leaves
previous entries intact. Entries die with the collection. The prepared vectors
own their dynamic billboard data and retain the snapshot's immutable geometry
and palettes. Environment representation comparison follows the existing
WorldEnvironmentPacketCache rule: padding can cause a conservative miss, never
reuse different values.

The existing ModelLighting callback contract is explicit: identical collected
batches/environment/preset must produce identical descriptors during a collection.
The current Pokemon source-lighting adapter satisfies this; its probe inputs are
immutable. Camera projection, dimensions, GPU scope, native pass bindings,
motion enrollment and recording are still consumed at draw time. Native
prepared-view/GPU tokens are not cached here. ABI23 and ordinary direct previews
are unchanged. This is the normal collected-model path, with no feature opt-in;
the new environment flag only emits diagnostic reuse counters.

## Mechanism and correctness evidence

Each final runtime/editor battle check reports 3 native objects, 33 view
requests, 6 preparations and 27 reuses per steady frame: 81.8% fewer repeated
view preparations. This is a count of work avoided, not an FPS percentage.
CPU contracts cover camera alternation, independent billboards, retained
palettes, changed lighting/preset, invalid camera, failed callback, bounded
replacement and new-pose/new-collection isolation.

Sixteen CPU groups/checks pass (eight engine, one ModelSupport, six game and
private foliage); ten validated native checks pass across D3D12 and Vulkan.
Four runtime/editor battle comparisons and two native-afterimage comparisons
pass the existing 1/255 channel limit. Final maximum battle difference:
0/255; afterimage:
0/255. All four ghosts remain.
Six qualified baseline captures are reused only after all 124 baseline payload
files match the previous locked build; original manifests and reuse receipts
are retained. All current candidate captures ran on gpu-worker.

All 61 shader/material payload files remain
byte-identical. Rain, lighting equations, surface splashes, quality settings,
draw counts and authored detail were not reduced. Native allocation and API
validation checks pass.

Harness setup failures are preserved in the evidence: an initial task-root
substitution wrote build records into the preceding task's directory (the build
itself used the intended source); records were recovered and the preceding
metadata/payload restored from verified controller evidence and its locked build.
A missing afterimage replay and a missing timing launcher each stopped their
launch before the game started; the inputs were restored and only the affected
checks resumed. These were not renderer failures or excluded timing samples.

## Matched optimized timing

EVGA GPU1, native 1920x1080 Medium, night/overcast, RelWithDebInfo.
Twelve fresh primary runs use battle ABBA and wet-field AB per API.
Each keeps 140 steady frames: battle 80-219, wet 40-179. Totals-only profiling
and quiet battle traces are used; screenshots occur after the measured interval.
Four separate diagnostic runs retain CPU subphase detail and reuse counters.
Settings match between baseline/candidate. Accepted runs have no API errors
or thermal-limit flags. These are matched Phlosion timings, not retail benchmarks.

| API / scene | GPU mean ms | CPU frame mean ms | CPU p95 ms |
| --- | ---: | ---: | ---: |
| d3d12 / wet | 16.993 -> 16.867 | 17.105 -> 17.169 | 27.336 -> 27.818 |
| d3d12 / battle | 17.131 -> 17.128 | 19.060 -> 19.163 | 30.891 -> 31.075 |
| vulkan / wet | 18.414 -> 18.391 | 18.241 -> 18.188 | 19.322 -> 28.790 |
| vulkan / battle | 18.046 -> 17.997 | 18.352 -> 18.395 | 27.079 -> 27.095 |

No repeatable whole-frame performance improvement is established by this pass.
The main battle GPU results are effectively flat; battle CPU means are slightly
higher in the final set, despite lower D3D12 actor time in the separate diagnostic.
D3D12 battle CPU p95 remains about 31 ms. Vulkan wet-field CPU p95 increased
from 19.322 to 28.790 ms in its single matched pair; that result is retained and
is not claimed as an improvement. The earlier primary set is preserved separately,
not selected or pooled to make the final result look better. All twelve final
primary runs were accepted, with 61-64 C peak GPU temperatures and no thermal flags.

The verified benefit is eliminating redundant native view preparation while
preserving its ownership and invalidation contracts. Diagnostic candidate
collection preparation averages 0.054 ms on D3D12 and 0.040 ms on Vulkan.
The broader actor CPU scopes still average 4.685 and 5.009 ms respectively;
those nested wall scopes include other preparation and draw work. Slow frames
also enlarge static/foliage/collection scopes, so they do not yet prove a
particular driver wait or scheduler cause. The next target is native descriptor
lowering and draw submission within that actor path, with better separation of
CPU execution and waiting before changing scheduling. Sustained 16.67 ms is
not achieved.

The six affected-path qualification pairs are pixel-identical. Separate primary
timing screenshots retain sparse differences: D3D12 wet-field maximum 63/255,
mean 0.006970/255, 37 pixels above 8/255 and three above 32/255 out of 2,073,600
pixels. They occur around thin canopy/rain edges. The full pair and largest-
difference crop were visually reviewed without a broad lighting or geometry
change; their precise cause is unresolved. Vulkan wet-field maximum is 2/255;
battle timing pairs have maximum 1-2/255. Rain-state records match in all pairs.
These timing captures are not described as pixel-identical.

Separate diagnostic scopes (nested; do not add these to whole-frame timings):

| Run | Actor CPU ms | Static geometry CPU ms |
| --- | ---: | ---: |
| diagnose2-d3d12-battle-baseline | 4.899 | 3.656 |
| diagnose2-d3d12-battle-candidate | 4.685 | 3.603 |
| diagnose2-vulkan-battle-baseline | 4.315 | 0.767 |
| diagnose2-vulkan-battle-candidate | 5.009 | 0.889 |

Collection-level native preparation timing is in native-view-cpu.json; the
baseline did not record that measure, so its absence is not zero cost.
slow-frame-attribution.json retains averages and the slowest 5% of frames.
CPU wall scopes alone do not establish a specific driver wait.

The first detailed diagnostic attempt exceeded the fixed 128-span profiler
capacity after adding a marker to every native consumer. Those incomplete
subphase traces are retained and rejected. The correction uses separate optional
collection-summary timing, leaving the existing phase stream unchanged.
The final source was rebuilt and requalified, and all twelve primary timings
plus four diagnostics repeated. First-pass primary totals were valid but are
not pooled with the final build.

## Remaining work

Shared preparation/submission remains **90-95% estimated technique coverage**.
This removes repeated collection-owned native view/material preparation.
Native texture lowering, submission sorting and backend packet construction
still run per draw/consumer; broader producer scheduling, glTF/backend work and
D3D12 slow-frame tails remain. C7/C8, Linux qualification and sustained 16.67 ms
whole-frame acceptance remain open.

Evidence: artifacts/lighting-foundation/actor-view-preparation-20261030,
including tested-source.json, capture-reuse.json, view-reuse.json,
qualification.json, trail.json, runtime-contracts.json, analysis-matched.json,
recording-diagnosis.json, slow-frame-attribution.json and the final locked-payload
comparison. See the game roadmap for its strict dependency-lock build receipt.
