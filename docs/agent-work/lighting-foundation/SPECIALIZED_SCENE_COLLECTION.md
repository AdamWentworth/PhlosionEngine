# Specialized scene collection and joined preparation

Date: 2026-10-30. Baseline engine f88dc25, game eeef838, ModelSupport da6a969.
Qualified on gpu-worker Windows only; controller reviewed and integrated the exact
delta. No local builds, GPU tests or inference. No shader, material payload,
rain, sample-count, quality-budget or visual-design reduction.

## Connected implementation

This extends [glTF view-family collection](SCENE_VIEW_COLLECTION.md) to the
normal native Pokemon, foliage and attack-trail producers. Independent source
anchors remain renderMaterial.cxx / PrepareRenderStagePassesRange:2785 and
renderCollector.cpp / DoCull_MainScene:339 / DoCull_Cascades:367 in the existing
research trace. These support collection/preparation before ordered pass
consumption, not a claim to duplicate the complete engine job graph.

- ModelInstance::collect owns mesh, geometry, material, pose and transformed
  batch records. ModelCollectedFrame::prepareView changes sort depths and
  camera-facing billboards without resampling pose/material/visibility. Model
  profiles must keep those evaluations independent of camera; the current
  Pokemon profile satisfies that boundary. A collected snapshot survives
  clearing/reloading/destroying its instance. Prepared batches still borrow
  snapshot-owned geometry/material data: retain the snapshot through submission.
- Model provider ABI is 22 (previously 21), with bounded family collection,
  checked handles and RAII retirement. It fixes pose/placement/tint/translucency
  at collection; camera, environment, lighting, prewarm and motion remain live
  at draw. Wrong/inactive/expired handles fail. Translucent afterimages never
  enter opaque motion history. Renderer vtables/game-plugin ABI are unchanged.
  Runtime/preview providers are rebuilt; ModelSupport source is unchanged.
- Battle collects native actors and native/glTF afterimages before shadow,
  reflection and main consumers. Existing admissions, order and alpha behavior
  remain. Three native records serve 33 actor/view submissions in the qualified
  battle; four afterimage records serve eight submissions. These are collection
  savings, not reduced GPU draw counts.
- Foliage evaluates four species' wind state once per family and reuses bounded
  camera/matrix-keyed visibility/LOD selections. Storage survives frames but
  selection contents are invalidated; distinct shadow frusta stay distinct.
  An observed battle has nine view builds and two reuses. Current/previous
  wind motion and per-view lighting submission remain on the owner thread.
- The presentation-owned scheduler joins an owner lane and an isolated foliage
  CPU lane before dependent views. Field/interior glTF and battle preparation
  use it where applicable. Exceptions join safely; no renderer, resource cache
  or motion mutation occurs on the helper. Work stealing may execute a helper
  job on the owner, so this is not a claim of guaranteed parallel overlap.
- Portable VFX already prepare once and remain unchanged. Sky, boundary and
  held-ball paths remain specialized. There is still no common plan of all
  admitted views or complete cross-view preparation job graph.

The legacy ModelInstance::prepare/render adapters remain for callers outside
the family path. Their collection/copy overhead is not newly qualified for
Autochess; its pinned dependency is unchanged. No new game feature toggle.

## Correctness evidence

Fourteen CPU groups/checks pass: six engine CTest groups, one ModelSupport runtime
contract, six game CTest groups and an explicit private-vegetation check. The
private asset test is run directly because that optional CTest entry was not
registered in this worker build. It verifies same-camera reuse, changed LOD,
shadow frusta, new-frame invalidation and bounded eviction; the existing 28,200
wind comparisons retain maximum error 1.19209e-7 metres.

Model tests cover snapshot lifetime, sample/material counts, camera billboards,
palette pointers, atomic failure, ABI/handle rejection, collected-transform
immutability, live lighting and translucent motion exclusion. Scheduler tests
cover owner-thread execution and join/exception reuse. Builds include optimized
editor, runtime, game plugin, model/preview and battle modules.

Ten native D3D12/Vulkan checks pass with validation enabled (and Vulkan layer
activation verified): collected/fresh/retained equivalence, wet relighting and
emission relighting. Parallel recording and native resource-binding reuse
remain active. No new D3D12 warning IDs or exact warning lines.

Twenty full scene baseline/candidate pairs are accepted: twelve main runtime/
editor wet, dry, orbit and battle pairs, four story/interior pairs and four
native/glTF Quick Attack trail pairs. Gameplay, rain, weather, animation,
surface and motion states match except new collection counters. All 79 shader/
material payloads match the baseline. Full sheets and enlarged difference
crops were reviewed; this is visual acceptance, not universal pixel identity.

Main image exceptions: D3D12 daylight max RGB8 difference 5 / 358 pixels above
one; other main pairs are within one and idle battles are exact. Reference
D3D12 story max3/12 pixels above one, interior max4/67; Vulkan story within one
and interior exact. Native trail D3D12 max3/28, Vulkan exact; glTF trails exact
on both. The original trail harness rejected battle-case replay; corrected
normal encounter replay v2 passes for both baseline and candidate. Failed
initial attempts remain in the evidence, not counted as passes.

Finite profiler capacity drops a few spans at startup (frames 1/2) and encounter
transition (45-47) in both versions. correctness-audit.json records those limits;
stable ending frames and all measured timing ranges have no dropped spans.
Do not describe the entire qualification traces as untruncated.

## Matched timing

Twenty direct-file-handle timing runs, with no exclusions: wet and battle use
ABBA order on each API; dry has one AB pair per API. Same EVGA RTX 2060 SUPER,
GPU 1, LUID 000000000000be8e, UUID
GPU_1_UUID. Optimized RelWithDebInfo, native
1920x1080, Medium, night/overcast, profiling enabled and validation disabled.
Wet/dry use frames 40-179; battle uses 80-219, 140 frames per run. All measured
frames have zero dropped spans, invalid frames or dropped frames, and all runs
have inactive thermal-throttle flags and no API errors. Settings match pairwise.

Preparation + drawing includes scene-collect, specialized-collect, view-collect,
static-geometry, actors/actor-prepare and foliage scopes, counting nested work
once. It is CPU wall time, not GPU time or summed CPU cycles across threads.
Active CPU subtracts recorded frame-begin wait and present time.

| API / scene | GPU mean | Whole CPU mean | Active CPU mean | Preparation + drawing CPU | CPU p95 | CPU p99 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| d3d12 / wet | 16.439 -> 16.505 | 16.460 -> 16.441 | 9.724 -> 9.949 | 7.306 -> 7.459 | 24.756 -> 24.472 | 28.816 -> 28.053 |
| d3d12 / dry | 12.601 -> 11.913 | 12.843 -> 11.841 | 9.286 -> 6.325 | 7.312 -> 4.930 | 20.811 -> 16.783 | 26.466 -> 20.077 |
| d3d12 / battle | 17.373 -> 16.981 | 18.100 -> 18.269 | 17.840 -> 16.358 | 14.260 -> 11.751 | 19.351 -> 29.798 | 23.666 -> 31.427 |
| vulkan / wet | 18.372 -> 18.407 | 18.214 -> 18.254 | 4.931 -> 4.729 | 2.679 -> 2.580 | 19.245 -> 19.197 | 19.699 -> 19.924 |
| vulkan / dry | 13.574 -> 13.548 | 13.414 -> 13.395 | 3.297 -> 3.349 | 1.952 -> 1.975 | 14.233 -> 14.264 | 14.341 -> 14.643 |
| vulkan / battle | 17.782 -> 17.824 | 23.523 -> 22.857 | 21.784 -> 18.407 | 13.065 -> 9.447 | 32.264 -> 30.827 | 34.884 -> 34.515 |

Battle preparation/drawing falls 14.260 -> 11.751 ms on D3D12 (about 18%) and
13.065 -> 9.447 ms on Vulkan (about 28%). Native poses are no longer evaluated
for each admitted view. D3D12 wet preparation/drawing instead rises
7.306 -> 7.459 ms; Vulkan dry rises 1.952 -> 1.975 ms. The D3D12 dry improvement
comes from a single pair and is not a general speedup claim.

Whole-frame results remain mixed. D3D12 battle mean CPU rises
18.100 -> 18.269 ms and p95/p99 regress 19.351 -> 29.798 /
23.666 -> 31.427 ms. The repeat reproduced the slow-frame problem; both runs
are retained. Some candidate tails include 3-5 ms frame-begin waits alongside
larger static-geometry and battle-view CPU spans. First-use actor work on
frame 81 is also present in both variants and is retained. These observations
do not establish the complete cause. Vulkan battle whole CPU improves
23.523 -> 22.857 ms, while the GPU cost is essentially unchanged.

Wet whole CPU is 16.441 ms D3D12 / 18.254 ms Vulkan; battle is
18.269 / 22.857 ms. D3D12 wet being below 16.67 ms on average does not pass the
whole-frame target: its tails and battle still fail. Neither API has a stable
60-fps acceptance result. Architecture is accepted under the user's explicit
2026-10-28 decision that qualified foundations can precede performance wins;
the D3D12 tail regression is an open cost, not a completed optimization.

Time outside rendering spans averages roughly 1.35-2.05 ms in D3D12 battle
and 5.95-6.36 ms in Vulkan battle, versus roughly 0.15-0.23 ms in field runs.
Direct inherited output file handles avoid the prior task's pipe harness.
Do not attribute this remaining time to that old harness without new evidence.

Ten additional timing-image pairs preserve visible scene appearance and rain
state. Explicit maxima/counts live in timing-image-details.json and
visual-review.json: D3D12 dry max22/292 pixels above one, 34 above four, six
above eight; wet1 max8/446 above one, six above four. Enlarged dry peak crop
shows sparse edge differences, not changed material/geometry. Vulkan timing
battle maxima are three; field pairs stay within one. These exceptions are
accepted visually, not erased by a blanket pixel-identity claim.

## Acceptance and next work

Shared view preparation/submission is **85-90% estimated technique coverage**
(previously 80-90%). The lower bound rises for qualified native, foliage and
trail collection plus independent producer scheduling; the upper bound remains
90 because a unified admitted-view plan and broader cross-view job scheduling
are still absent. This is not a visual-parity or retail-FPS percentage. Other
matrix categories are unchanged, including lighting and wind appearance.

Next: collect the common admitted main/shadow/probe view plan before producer
preparation, schedule independent selections against it, then consume in
deterministic order. Preserve per-view motion and source ownership. Diagnose the
D3D12 battle tail regression and remaining traversal/copy/allocation costs using
whole-frame and inclusive preparation timing. C7/C8 source-feature and update
policy gaps, Linux qualification and 16.67-ms acceptance remain open.

Evidence: artifacts/lighting-foundation/specialized-collection-20261030 contains
source hashes, build/CPU/native logs, visual/state audits, all timing data,
failure history and exact-delta/locked-payload receipts. The worker task is
WORKER_EVIDENCE_ROOT/phlosion-specialized-collection-20261030. Only exact task deltas
return to canonical controller repositories; dependencies are pinned and the
strict final payload must match the qualified candidate before handoff.
