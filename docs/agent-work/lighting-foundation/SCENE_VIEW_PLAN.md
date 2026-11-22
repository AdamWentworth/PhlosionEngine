# Common scene-view planning and immutable pose reuse

Date: 2026-10-30. Baseline engine 239bcd6, game fcb6edd, ModelSupport da6a969.
Implementation, optimized builds and checks ran on gpu-worker Windows; controller
owns reviewed integration and commits. No local build/test/inference fallback.
No shader, rain, lighting-equation, quality-budget or authored-detail reduction.

## Connected implementation

This extends SPECIALIZED_SCENE_COLLECTION.md. The existing independent trace
anchors remain renderMaterial.cxx / PrepareRenderStagePassesRange:2785 and
renderCollector.cpp / DoCull_MainScene:339 / DoCull_Cascades:367. The connected
change plans required work before preparation and consumes it in authored order;
it does not claim the complete source engine scheduler.

- Field and battle plan the main, directional shadow, admitted local static/
  dynamic shadow, planar reflection and required probe-material views before
  geometry preparation. Main/reflected surface capture and composition share
  their geometry selection. Specialized camera-facing/native adaptation remains.
- IWorldLocalShadowPlan is a separate optional read-only interface. Queries use
  a copy of the future source allocation and the appropriate fenced cache bank.
  Execution verifies that admission still matches. Cache hits need no static
  geometry preparation. Existing interface signatures, shader math, source
  order and dynamic-caster drawing remain unchanged.
- Placed probes report only required material-capture faces, using the same
  cursor and revision admission as update. Existing relighting, budgets and
  completed-generation publication remain unchanged. No concurrent cache update
  is allowed between planning and consumption.
- GltfSceneViewFamily owns material/exclusion inputs and immutable object/pose
  records. It deduplicates equivalent pending visibility selections across all
  planned views and joins one two-lane batch with isolated foliage selection.
  Resource/material-cache mutation and GPU submission stay on the owner.
  Failed work is joined before rethrow; unprepared views cannot be published.
  Live motion history is bound at draw. The old prepareView adapter remains.
- Ordinary wet field preparation requests fall 27 -> 21; battle 38 -> 30.
  Both use nine planned views instead of eleven preparation calls. Four fresh
  glTF selections remain; duplicates and cached selections reuse them. This is
  not a reduction in GPU draws. Dry uses eight plans instead of nine calls.
  Work stealing can execute jobs on the owner; two lanes do not guarantee overlap.
- Foliage selection is prepared in that joined batch. Wind still evaluates once
  per species; native GPU/motion submission remains ordered on the owner.

## Pose-cache bug exposed during qualification

Initial D3D12 native afterimage comparisons varied around the moving model,
including between two executions of the same candidate. Investigation found
that both D3D12 skin upload routes trusted pointer/count/mode alone. A temporary
pose can reuse an address with different contents within one frame.

The new native regression first reproduced the defect against the unfixed
renderer: the second, moved pose was absent at the expected pixel (identity zero).
Both upload routes now compare cached CPU matrix contents before reuse, retaining
a CPU copy instead of reading write-combined GPU upload memory. The regression
covers ordinary/paired palettes and cached/transient geometry on both APIs.

ModelCollectedFrame also owns immutable palette arrays separately from view
batches. Dependent views borrow those arrays instead of copying them each time.
The collected snapshot must outlive submission, as for its geometry/materials.
Instance replacement, failed recollection, distinct poses, billboard facing,
shared palette addresses and runtime-provider motion are checked. Provider ABI
remains 22; ModelSupport source is unchanged.

The first candidate and expected failing test are preserved in initial-plan/
and palette-repro*. They are not the accepted payload or performance samples.

## Qualification and limits

Optimized RelWithDebInfo engine, game and model-provider builds pass. Fourteen
CPU groups/checks pass (six engine, one ModelSupport, six game, private foliage);
the captured wind check compares 28,200 values, maximum error 1.19209e-7 m.
Thirty validation-enabled native checks pass on D3D12 and Vulkan: ten retained
glTF/relight checks, sixteen shadow-cache scenarios, two placed-probe checks and
two native motion suites including the four same-address palette cases. Vulkan
validation-layer activation is verified. All eight shadow image pairs are exact.
There are no new D3D12 warning IDs/lines or validation errors.

Twenty canonical scene pairs cover runtime/editor, wet/daylight/orbit/battle,
story/interior (five residents), and native/glTF attack afterimages. An additional
D3D12 native-afterimage repeat is pixel-identical to the accepted candidate.
Native Vulkan and both glTF trail comparisons are exact; D3D12 native is within
one RGB8 level. Main scene comparisons are within one except D3D12 daylight:
maximum 3, 57 pixels over one. The reference exception is D3D12 interior:
maximum 7, 360 pixels over one, 10 over four. Full sheets and enlarged exception
crops were reviewed; there is no visible rain/lighting/detail reduction.
Gameplay, weather, rain, surface and animation/motion state match. All 79
shader/material payloads are byte-identical. This is not universal pixel identity.

Startup/transition profiling drops spans on frames 1/2 and 45-47 in limited
correctness runs. The empty final flush frame has no scene-plan obligation.
Stable ending frames and every measured timing frame have complete spans.
The initial missing interior-settings harness file and failed first attempt are
retained separately; corrected settings were used for accepted runs.

## Matched performance

Same EVGA RTX 2060 SUPER GPU 1, LUID 000000000000be8e, UUID
GPU_1_UUID, i7-8700. Native 1920x1080 Medium,
night/overcast, optimized RelWithDebInfo; profiling on, validation off.
Inherited output file handles avoid pipe backpressure. Wet/dry use frames
40-179, battle 80-219 (140 samples/run). Wet/battle use ABBA; dry one AB pair
per API. Selected runs have no thermal flags, API errors, invalid/dropped
frames or dropped spans, with pairwise identical settings.

The initial D3D12 wet baseline reported one software thermal-limit flag at
56 C / 1905 MHz. This does not establish overheating. The entire four-run wet
ABBA comparison was repeated; all four repeats are clean and replace the
initial four in the table. All 24 ordinary runs and two diagnostic-only runs
are preserved; the table pools the final 20 ordinary runs without cherry-picking
individual frames.

Preparation/drawing includes scene-collect, specialized-collect, view-plan
(or baseline view-collect), static-geometry, actor/actor-prepare and foliage,
counting nested time once. This is CPU wall time, not summed thread CPU.
Local-shadow key/admission work remains outside this aggregate in both paths;
whole CPU includes it. The recorded frame-begin span includes more than fence
waiting, so subtracting it does not measure pure active CPU work. The raw
analysis retains its historical active_cpu_ms field with this limitation.

| API / scene | GPU mean | Whole CPU mean | Preparation + drawing CPU | CPU p95 | CPU p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3d12 / wet | 16.330 -> 16.193 | 16.191 -> 16.038 | 6.096 -> 6.291 | 17.395 -> 16.912 | 18.232 -> 17.284 |
| d3d12 / dry | 11.806 -> 11.740 | 11.678 -> 11.609 | 4.314 -> 4.535 | 12.347 -> 12.501 | 13.244 -> 13.758 |
| d3d12 / battle | 17.109 -> 17.165 | 19.450 -> 19.532 | 12.351 -> 12.520 | 30.675 -> 30.876 | 32.794 -> 35.754 |
| vulkan / wet | 18.399 -> 18.387 | 18.246 -> 18.221 | 3.017 -> 2.961 | 20.038 -> 19.505 | 27.570 -> 20.310 |
| vulkan / dry | 13.633 -> 13.559 | 13.473 -> 13.395 | 1.936 -> 2.061 | 14.508 -> 14.240 | 14.854 -> 14.388 |
| vulkan / battle | 17.797 -> 17.790 | 23.065 -> 23.443 | 9.907 -> 10.231 | 31.072 -> 30.035 | 41.893 -> 46.462 |

No meaningful overall speedup is established. D3D12 wet CPU improves slightly,
16.191 -> 16.038 ms, but dry is a single pair and battle remains mixed.
D3D12 battle p95/p99 remain 30.876/35.754 ms; Vulkan battle mean CPU is
23.443 ms with p99 46.462 ms. First-use actor work on frame 81 is retained
in both variants. Stable 16.67 ms / 60 fps acceptance is not achieved.

Two extra D3D12 battle runs enable existing owner-thread CPU diagnostics;
they are not pooled with ordinary timing. Draw-battle wall/owner CPU averages
are baseline 16.645/16.295 ms and candidate 16.777/15.960 ms. GetThreadTimes
is quantized (short-stage CPU may exceed measured wall), excludes helper
thread CPU, and is not proof of a particular wait or scheduler bottleneck.
The final 140 draw ticks and profiler frames 80-219 are separate windows.
Field-stage records sharing tick numbers are irrelevant to the battle totals.

Both diagnostic recordings still average 36 joined batches and about 1,038
packets/frame. Summed lane wall time is 1.841 -> 1.832 ms, overlap
0.502 -> 0.462 ms. Frame/recording wall correlation is 0.757/0.732;
correlation is not causal attribution. Static-geometry, repeated native
recording joins, first-use actor work and time outside rendering spans remain
the next measurement boundary. The slow-frame regression is investigated,
not resolved or dismissed as a harness artifact.

## Coverage and next boundary

Shared preparation/submission moves from 85-90% to **90-95% estimated technique
coverage**: common admitted-view planning, equivalent-selection reuse, immutable
palettes and one joined independent selection batch are connected and qualified.
This is an engineering estimate, not a measured retail image/FPS match or a
claim that the complete source scheduler has been reproduced. Other category
ranges stay unchanged.

Next, retain the common plan through larger view-owned native recording ranges
instead of repeated object-level joins, with explicit owner preparation,
enqueue/wait/worker execution and outside-rendering phase attribution. Preserve
deterministic packet order, motion lifetimes and cache admission. Benchmark the
same scenes; do not assume lower join counts guarantee a gain. Native per-camera
adaptation, broader scheduling, C7/C8 priority/residency/refresh policy, Linux and
whole-frame 16.67 ms remain open. Architecture is accepted under the user's
explicit decision that qualified foundations may precede performance gains.

## Evidence

Controller artifact destination:
artifacts/lighting-foundation/view-plan-20261030.
Worker originals: WORKER_EVIDENCE_ROOT/phlosion-view-plan-20261030.

tested-source.json and candidate/files.json bind source and optimized payload;
visual-review.json and timing-visual-review.json record image review.
native-plan-evidence.json, main-audits.json, correctness-audit.json,
plan-audit.json, plan-counts.json, extra-correctness.json,
repeat-image-audit.json, resource-evidence.json, recording-evidence.json and
validation-warning-audit.json hold checks. analysis-matched.json,
selected-timing-runs.json, timing-image-details.json and
thread-cpu-diagnosis.json hold measurements. initial-* and the original run
directories preserve superseded evidence. Final strict dependency rebuild and
locked-payload-comparison.json must match all tested payload bytes before
the game pin is committed.
