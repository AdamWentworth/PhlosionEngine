# View-owned native recording batches

Date: 2026-10-30. Baseline engine 1b40b4a, game 2760fe1, ModelSupport da6a969.
Implementation, optimized builds and checks run on gpu-worker Windows.
controller owns review, integration and commits. No local build/test fallback.

## Connected change

This continues SCENE_VIEW_PLAN.md and NATIVE_PARALLEL_RECORDING.md: collect and
prepare dependent views, then record immutable packet ranges using independent
native command contexts and consume them in authored order. Existing independent
source-trace anchors remain renderMaterial.cxx / PrepareRenderStagePassesRange
and renderCollector.cpp / DoCull_MainScene / DoCull_Cascades. This does not claim
the complete source scheduler or authenticated retail performance.

Field and battle geometry now own an outer indexed-submission scope per consumed
view. Nested glTF/native/foliage producers join it. Resource-scope closure drops
CPU lookup caches, but no longer prematurely flushes an active outer batch.
Native handles and append-only uploads still belong to the fenced frame; physical
resource retirement remains at frame entry after GPU completion. Pass/region/
heap changes, immediate draws, depth clears and the 256-packet limit still flush.
No cross-pass packet reordering or GPU-pass concurrency is introduced.

The two recording lanes and deterministic submission order remain. Groups below
eight packets and exhausted per-frame recording-list budgets retain the serial
fallback. All packet contents are copied before recording; workers neither
mutate material/resource caches nor allocate uploads. Resource-scope and
transparent-order regression tests exercise nested producers, same-key materials
in different scopes, first-use uploads, immediate draws, 256-packet spills,
retirement, frame-fence reuse and resize.

## Timing without fragmenting batches

CPU-only preparation scopes now use a separate optional backend interface.
Their spans retain nesting and CPU wall duration but allocate no GPU queries.
The profile's explicit gpu_timed=false flag means gpu_ms=0 is not a measured
zero GPU cost. GPU query indices are packed independently of CPU span indices.
GPU-timed pass boundaries remain ordered with queued work on both APIs.
Existing interfaces and model-provider ABI22 are unchanged.

WorldViewSubmission encloses the complete view GPU interval. Static geometry,
foliage and actor preparation are CPU subphases; view-recording measures the
outer flush. Worker counters separate setup, dispatch, joined-run wall time,
owner join wait, helper start delay, ordered execution and serial recording.
A stolen helper range is counted as owner-claimed and contributes no helper
queue delay or join waiting. Summed lane recording time includes overlap and
is not interchangeable with wall time or CPU cycles.

Optional runtime host traces now separate input, simulation, composition/present
and diagnostics. They use the displayed frame ID; battle simulation ticks are
different identifiers. Existing GetThreadTimes values are quantized and exclude
helper CPU; short-stage CPU/wall differences are not a reliable stall estimate.

## Qualification repair

The initial full Vulkan scene exposed an unconverted timestamp producer in
clustered lighting. It still indexed GPU queries by all-span token after CPU
spans had been introduced, producing VUID-vkCmdWriteTimestamp-None-00830.
The qualification stopped. Both cluster timestamp writes now use the packed
GPU query index. A new native mixed CPU/cluster/submit timing regression covers
this path on both APIs. Initial failure logs, source hashes and native results
are retained under initial-timestamps/ and the original v7 scene directories;
they are not accepted final evidence.

No shader, rain, lighting equation, weather, quality budget or authored detail
was reduced. Shared preparation/submission remains 90-95% estimated technique
coverage; this pass advances the recording portion within that range. It is
not a measured retail image/FPS parity percentage.


Recording diagnostic execute_ms measures CPU encoding of recorded ranges into primary commands, not GPU execution or queue-submit duration. CPU-only stages explicitly set gpu_timed=false; their zero GPU field is not a measurement.
## Verification

Optimized RelWithDebInfo engine, game and unchanged ModelSupport build successfully.
Sixteen CPU groups/checks pass (eight engine, one model, six game and the private
foliage check). Private foliage compares 28,200 coordinates with maximum error
1.19209e-7 m. Thirty-four native checks pass with API validation: ten glTF/relight,
sixteen shadow-cache, two placed-probe, two motion/palette, two view-recording
and two mixed CPU/cluster profiler checks. Vulkan layer activation is verified.

Twenty runtime/editor scene pairs plus a D3D12 native-afterimage repeat cover
wet/dry/orbit/field/battle, native/glTF trails and Colosseum story/interior scenes.
States and all 79 shader/material payloads remain identical. Main battle images
are pixel-identical. Small reviewed image differences and reference-scene details
are recorded in visual-review.json, scene-image-details.json,
reference-image-details.json and repeat-image-audit.json. Final totals-only frame 221 images were also reviewed.
D3D12 field images have a localized canopy-edge difference (max 63 RGB8, at most
101 pixels over 4 and 37 over 8 out of 2,073,600); the enlarged corner and complete
scenes show no visible detail loss. Vulkan and battle timing pairs differ by
at most 3 levels. This is not a pixel-identical claim. See timing-image-details.json.
Original failure evidence is retained. The reference orchestration also stopped on a stale
baseline path; it was corrected to reuse the previous qualified candidate
whose complete 124-file payload matches this task's baseline byte for byte.

The packet audit preserves total parallel-plus-serial draws in all twelve main
pairs. On both APIs, main battle batches fall 36 -> 11; wet 14 -> 11; dry 11 -> 9.
Battle parallel packets rise 1046 -> 1238 because previously serial producer tails
join the outer batch. This is grouping, not omitted work. Required cache,
resource retirement/reuse, resize and ordered transparency checks pass.
Only the documented startup/transition profiler span overflow remains; all
accepted steady-frame intervals have no dropped/invalid frames or spans.

## Matched timing

Twenty matched optimized runs use EVGA RTX 2060 SUPER GPU 1 on gpu-worker Windows,
LUID 000000000000be8e, UUID GPU_1_UUID,
native 1920x1080 Medium night/overcast. Wet and battle use ABBA order per API;
dry uses AB. Each run contributes 140 frames: wet/dry 40-179, battle 80-219.
Whole-frame profiling is enabled, per-stage profiling and battle tracing are off,
validation is off, capture occurs at frame 221 after measurement, and settings match
within each paired case. All selected runs
have inactive thermal-limit flags and no API errors. Raw runs and sensor logs
are retained. Detailed diagnostic runs below are not pooled into this table.

This removes intrusive battle/stage instrumentation used in prior milestone
tables. Absolute numbers across those tables are not directly comparable.
Preparation-stage timings are unavailable in totals-only runs, not zero.
All durations below are milliseconds.

| API / scene | GPU mean | Whole CPU mean | CPU p95 | CPU p99 |
| --- | ---: | ---: | ---: | ---: |
| d3d12 / wet | 17.217 -> 16.065 | 17.377 -> 16.013 | 27.206 -> 18.486 | 32.947 -> 26.313 |
| d3d12 / dry | 12.167 -> 12.053 | 12.230 -> 12.021 | 19.224 -> 19.803 | 21.308 -> 22.038 |
| d3d12 / battle | 17.098 -> 17.177 | 18.895 -> 19.496 | 30.285 -> 30.511 | 33.627 -> 32.739 |
| vulkan / wet | 18.369 -> 18.353 | 18.200 -> 18.190 | 26.216 -> 19.275 | 32.075 -> 19.515 |
| vulkan / dry | 13.656 -> 13.565 | 13.499 -> 13.402 | 14.679 -> 14.220 | 14.850 -> 14.984 |
| vulkan / battle | 18.061 -> 17.913 | 18.840 -> 18.991 | 28.424 -> 29.020 | 33.711 -> 36.407 |

The batching mechanism is verified, but a reliable overall frame-time gain is
not established. In the final set D3D12 wet CPU mean falls 17.377 -> 16.013 ms,
while battle rises 18.895 -> 19.496 ms (+0.601 ms, about 3.2%); battle p95 stays
near 30 ms. Vulkan battle is broadly neutral: CPU 18.840 -> 18.991 ms and GPU
18.061 -> 17.913 ms. The 16.67 ms whole-frame target is not achieved.

Repeatability limits matter. The preceding D3D12 wet ABBA group, whose capture
also lay outside its measured interval, instead changed CPU 16.805 -> 17.315 ms.
Do not market the final wet result as a stable speedup. Dry has one pair per API;
its small differences do not establish a robust gain. The architecture is
accepted under the user's explicit direction that necessary source-aligned
foundations may precede immediate performance improvements. The D3D12 battle
regression and slow-frame tails remain explicit next-work concerns.

The first 20-run protocol captured at frame 185 inside the battle interval.
Vulkan screenshot/readback/PNG work produced roughly 600 ms at that frame,
inflating the mean by about 4 ms; D3D12 host timing also included capture cost.
The complete 20-run set and all four diagnostics were repeated with capture
at frame 221, after every measured interval. No individual slow frames were trimmed.
The original runs, a software-thermal-flagged Vulkan baseline, the prelaunch
missing-helper failure and both protocol summaries are retained. Final accepted
runs have no thermal flags/API errors. These are matched engine timings, not
a same-hardware retail Cyberpunk comparison.


## Separate battle diagnostics

| API / variant | CPU mean | Recording batches | Joined-run wall | Owner join wait | Setup | Execute encoding | Serial recording |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| d3d12 / baseline | 18.757 | 36.0 | not recorded | not recorded | not recorded | not recorded | not recorded |
| d3d12 / candidate | 19.222 | 11.0 | 1.364 | 0.453 | 0.065 | 0.058 | 0.031 |
| vulkan / baseline | 19.706 | 36.0 | not recorded | not recorded | not recorded | not recorded | not recorded |
| vulkan / candidate | 18.511 | 11.0 | 0.496 | 0.112 | 0.002 | 0.016 | 0.015 |

Separate capture-free-interval diagnostics put candidate joined recording at
1.364 ms D3D12 / 0.496 ms Vulkan, including owner join waits of 0.453 / 0.112 ms.
Setup is 0.065 / 0.002 ms and primary-list execution encoding 0.058 / 0.016 ms.
The latter is CPU command encoding, not GPU execution or a queue-submit duration.
Lane recording sums overlap; do not add them to joined wall time.

The candidate view-geometry CPU scopes average 10.692 ms D3D12 / 7.479 ms Vulkan.
Actor submission accounts for 4.887 / 5.270 ms and rises to 9.391 / 9.230 ms in
each diagnostic's slowest 5% of frames. The total view geometry rises to
17.708 / 12.899 ms there, while join waiting is only 0.650 / 0.247 ms.
These are nested wall scopes, not additive CPU-cycle totals or proof of a
particular driver stall. D3D12 static geometry also costs 3.624 ms on average.
Per-frame details are in slow-frame-attribution.json.

Host input averages about 0.02 ms and simulation about 0.125 ms on both APIs.
Composition/present averages 0.408 / 0.295 ms; post-render diagnostics about 0.002 ms.
GetThreadTimes is quantized and can exceed wall time for short scopes, so its
subphase values must not be interpreted as precise blocking time. Frame-begin
time includes more than a pure GPU fence wait.

Next: follow the remaining actor/material/descriptor preparation within admitted
views through native packet construction, retain invariant material/instance
bindings, and move eligible immutable preparation into joined jobs before ordered
native consumption. Attribute driver waits with targeted sampling before assigning
them a cause. This continues PrepareRenderStagePassesRange-style preparation;
another recording-lane count change alone is not justified by these measurements.


## Evidence and remaining scope

Evidence: artifacts/lighting-foundation/view-recording-20261030/ on controller,
with source/payload SHA-256 manifests, build/test logs, API validation,
scene/state/image audits, matched timings and separate recording/host diagnostics.
Worker source and payload receipts are authoritative for each tested run.
The game dependency-lock rebuild is documented in its roadmap and the imported
locked-payload-comparison.json receipt.

Broader producer/driver scheduling, native per-camera adapters, complete C7/C8
policy, Linux qualification and the 16.67 ms whole-frame acceptance remain open.
This does not establish retail Cyberpunk 2077 FPS parity or close the full renderer.
