# Shared scene collection and joined visibility preparation

2026-10-30. Follow-up to [native resource bindings](NATIVE_RESOURCE_BINDINGS.md).
Implementation, optimized builds and every CPU/GPU check run on gpu-worker.
controller owns review, exact-delta integration and commits.

## Connected normal path

The engine's GltfSceneViewFamily collects published asset snapshots and validated
pose/placement records once per render-view family. Exploration collects the field
or interior plus visible actors. Battle collects its field and visible glTF cast
or stand-ins, including the cast's placement calculations. Per-view draws consume
these records directly instead of looking up and comparing the pose, palette and
placement again. Existing native GPU resource bindings remain in use.

Each admitted shadow, probe, reflected or primary pass supplies its ordered view
requests. The engine validates the complete request list and owns material inputs
and visibility results before exposing it to GPU drawing. Missing independent
object selections use two joined lanes when at least two selections and 256
primitives need preparation; smaller or cached sets stay serial. Jobs read
immutable inputs and write disjoint outputs. Plan-cache mutation and all backend,
motion-history and resource-scope operations stay on the render owner thread.
Workers join before result publication and authored request/primitive order is
preserved. This threshold is CPU scheduling policy, not a graphics-quality cut.

The parallel_batches diagnostic counts batches admitted to the joined scheduler;
it does not prove both lanes overlapped, since the owner may claim the helper's
work. Warm normal-scene captures report zero such batches and reuse their cached
selections. The 192-object CPU fixture exercises the joined branch. Existing
native command-recording overlap is measured separately.

A presentation-owned scheduler retains its helper and joins it before plugin
unload. A caller without a scheduler owns a temporary one. Prepared views own
their asset, pose, palette, material and selection records and can survive
producer teardown and cache eviction. Motion-history pointers and the renderer
remain caller-owned synchronous consumers. Placement changes require recollection;
published assets remain immutable. No unbounded global scene or cross-frame pose
cache is introduced. The existing per-asset cache limits remain unchanged;
family/view storage scales with submitted objects and lives with those owners.

Sky, foliage, boundary drawing, native Pokemon, held balls and effect/trail draws
retain their existing ordering and specialized paths. Transparent trail poses
stay independent. Current camera, lighting, material inputs, shadow-caster masks,
scope retirement and motion-history submission are still specific to each view.
Only passes already admitted by the existing shadow/probe policy collect requests.
No eager rendering or visibility work is added for unused probe faces or shadows.
No shader, rain, asset, light, quality-budget or feature-opt-in change is made.
Existing public renderer vtables and the game-plugin ABI remain unchanged.

## Reference and remaining scope

The independent implementation follows the prepared-stage/consumer and joined
collection boundaries already traced in RETAINED_DRAW_PREPARATION.md:
renderMaterial.cxx / PrepareRenderStagePassesRange:2785 and
renderCollector.cpp / DoCull_MainScene:339 / DoCull_Cascades:367.
No source code was copied or newly authenticated as current retail.

This delivers shared glTF collection and joined per-view visibility preparation.
It does not complete a global cross-view job graph. Native Pokemon, foliage,
boundary and effect producers do not yet enter this collected-object API; their
qualified existing paths remain. Cross-view scheduling and broader specialized
producer coverage are the next architectural targets. Full C7/C8 policy, Linux
qualification and the 16.67 ms target remain open.

Shared preparation/submission is now **80-90% estimated technique coverage**:
owned collection and joined visibility are delivered in both game hosts in
addition to retained inputs, bindings and native parallel command recording.
This is an engineering scope estimate, not a retail visual or FPS percentage.
Other matrix rows are unchanged.

## Verification

Optimized MSVC RelWithDebInfo engine/editor, game runtime/plugin and dependent
modules build. All nine CPU test groups pass. New CPU contracts compare ordered
submissions to the existing adapter, mutate source poses/material spans, reject
invalid complete views and placement changes, retain views after producer teardown,
and exceed instance/selection cache capacity. A 192-object fixture sharing one
asset exercises joined lanes and compares every draw to serial preparation.

All ten native D3D12/Vulkan validation checks pass, with explicit Vulkan layer
activation. The glTF fixture adds a third collected rendering path and requires
pixel-exact equality with both fresh and retained preparation. It covers static
and skinned primitives, changing pose/light/emission, two placements, exclusions,
reload and scope retirement/recovery. Native binding reuse remains seven geometry
and seven material preparations for 208 draws, with three stale rejections per API.

Twelve fresh runtime/editor baseline/candidate pairs cover wet, dry, moving-camera
and battle views. Native parallel recording remains active. No API errors, new
D3D12 warning IDs or invalid/dropped profile frames are accepted. All 79 shader,
material and native-shader payloads match the baseline. Gameplay, animation,
motion, rain and environment state match; scene_collection is the only added
environment diagnostic and is checked independently:
- d3d12 runtime wet: maximum RGB8 difference 1, 0 pixels above one.
- d3d12 runtime dry: maximum RGB8 difference 5, 248 pixels above one.
- d3d12 runtime orbit: maximum RGB8 difference 3, 23 pixels above one.
- d3d12 runtime battle: maximum RGB8 difference 0, 0 pixels above one.
- d3d12 editor wet: maximum RGB8 difference 1, 0 pixels above one.
- d3d12 editor battle: maximum RGB8 difference 0, 0 pixels above one.
- vulkan runtime wet: maximum RGB8 difference 0, 0 pixels above one.
- vulkan runtime dry: maximum RGB8 difference 0, 0 pixels above one.
- vulkan runtime orbit: maximum RGB8 difference 0, 0 pixels above one.
- vulkan runtime battle: maximum RGB8 difference 0, 0 pixels above one.
- vulkan editor wet: maximum RGB8 difference 0, 0 pixels above one.
- vulkan editor battle: maximum RGB8 difference 0, 0 pixels above one.

Strict maximum-one image flag: false.
Manual review and any pixel exceptions are recorded in visual-review.json;
do not reinterpret manual visual acceptance as pixel identity.

Four additional fresh baseline/candidate pairs cover the Colosseum opening and
interior on both APIs. Both interior variants reach all five residents:
- d3d12 story: maximum RGB8 difference 2, 13 pixels above one; game/animation state matches.
- d3d12 interior: maximum RGB8 difference 4, 118 pixels above one; game/animation state matches.
- vulkan story: maximum RGB8 difference 1, 0 pixels above one; game/animation state matches.
- vulkan interior: maximum RGB8 difference 0, 0 pixels above one; game/animation state matches.

Normal-path collection diagnostics:
- check-runtime-d3d12-wet-candidate-v6: {'objects': 3, 'parallel_batches': 0, 'requests': 27, 'selection_builds': 4, 'selection_reuses': 23, 'views': 11}.
- check-runtime-d3d12-dry-candidate-v6: {'objects': 3, 'parallel_batches': 0, 'requests': 21, 'selection_builds': 2, 'selection_reuses': 19, 'views': 9}.
- check-runtime-d3d12-orbit-candidate-v6: {'objects': 3, 'parallel_batches': 0, 'requests': 27, 'selection_builds': 4, 'selection_reuses': 23, 'views': 11}.
- check-runtime-d3d12-battle-candidate-v6: {'objects': 4, 'parallel_batches': 0, 'requests': 38, 'selection_builds': 4, 'selection_reuses': 34, 'views': 11}.
- check-editor-d3d12-wet-candidate-v6: {'objects': 3, 'parallel_batches': 0, 'requests': 27, 'selection_builds': 4, 'selection_reuses': 23, 'views': 11}.
- check-editor-d3d12-battle-candidate-v6: {'objects': 4, 'parallel_batches': 0, 'requests': 38, 'selection_builds': 5, 'selection_reuses': 33, 'views': 11}.
- check-runtime-vulkan-wet-candidate-v6: {'objects': 3, 'parallel_batches': 0, 'requests': 27, 'selection_builds': 4, 'selection_reuses': 23, 'views': 11}.
- check-runtime-vulkan-dry-candidate-v6: {'objects': 3, 'parallel_batches': 0, 'requests': 21, 'selection_builds': 2, 'selection_reuses': 19, 'views': 9}.
- check-runtime-vulkan-orbit-candidate-v6: {'objects': 3, 'parallel_batches': 0, 'requests': 27, 'selection_builds': 4, 'selection_reuses': 23, 'views': 11}.
- check-runtime-vulkan-battle-candidate-v6: {'objects': 4, 'parallel_batches': 0, 'requests': 38, 'selection_builds': 4, 'selection_reuses': 34, 'views': 11}.
- check-editor-vulkan-wet-candidate-v6: {'objects': 3, 'parallel_batches': 0, 'requests': 27, 'selection_builds': 4, 'selection_reuses': 23, 'views': 11}.
- check-editor-vulkan-battle-candidate-v6: {'objects': 4, 'parallel_batches': 0, 'requests': 38, 'selection_builds': 5, 'selection_reuses': 33, 'views': 11}.

The first build caught an accidental edit in the unrelated calibration branch
and an unused loop variable; both were fixed before qualification. The first CPU
fixture fell below the parallel threshold because one selection was already
cached. Its workload was expanded to exercise the intended joined branch.
Failure receipts are retained under initial-build/. The accepted evidence is
tied to tested-source.json after these corrections.

## Matched warm timing

EVGA RTX 2060 SUPER GPU 1, LUID 000000000000be8e,
UUID GPU_1_UUID. Native 1920x1080 Medium,
fixed night/overcast, heavy Hybrid rain and matched dry control. Wet ABBA plus
dry AB per API; 220 frames per run, analyzing frames 40..179. Validation off,
phase profiling on. All twelve selected runs are accepted, with matching settings
and no thermal flags. Maximum temperature 62 C.
Source and payload identities, telemetry, means, medians and tails are retained.

The last column includes scene-collect, view-collect, actor-prepare, static-geometry
and actors across all views, counting nested scopes only once. Moving work into
the new collection scopes therefore cannot manufacture a saving. Milliseconds:

| API / scene | GPU mean | Whole CPU mean | Active CPU mean | CPU p95 | CPU p99 | Collection + glTF drawing CPU |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| d3d12 / wet | 17.061 -> 17.076 | 17.177 -> 17.208 | 10.501 -> 11.404 | 26.214 -> 27.449 | 32.507 -> 30.083 | 5.927 -> 6.362 |
| d3d12 / dry | 12.144 -> 12.332 | 12.019 -> 12.503 | 6.542 -> 8.197 | 18.146 -> 21.112 | 20.870 -> 24.144 | 3.684 -> 4.605 |
| vulkan / wet | 18.383 -> 18.393 | 18.215 -> 18.233 | 5.099 -> 5.052 | 19.258 -> 19.310 | 19.882 -> 20.219 | 1.819 -> 1.712 |
| vulkan / dry | 13.552 -> 13.549 | 13.388 -> 13.377 | 3.411 -> 3.449 | 14.278 -> 14.326 | 14.705 -> 14.840 | 1.308 -> 1.286 |


This is an accepted architectural foundation with mixed CPU results, not a
whole-frame speedup. Heavy-rain GPU means are essentially flat: D3D12
17.061 -> 17.076 ms and Vulkan 18.383 -> 18.393 ms. Both remain above
16.67 ms. Wet whole-frame CPU is also essentially flat on each API.

Combined collection and glTF drawing CPU falls in Vulkan: wet
1.819 -> 1.712 ms (about 6%) and dry 1.308 -> 1.286 ms. D3D12 rises:
wet 5.927 -> 6.362 ms and dry 3.684 -> 4.605 ms. These include the new
collection scopes; the D3D12 increases must not be hidden by reporting only
the old drawing scopes. D3D12 dry GPU/whole CPU also regress
12.144 -> 12.332 / 12.019 -> 12.503 ms in this single dry pair.

Tails are mixed. D3D12 wet CPU p95 worsens 26.214 -> 27.449 ms while p99
improves 32.507 -> 30.083 ms; dry p95/p99 worsen 18.146 -> 21.112 and
20.870 -> 24.144 ms. Vulkan wet p99 rises 19.882 -> 20.219 ms. Individual
D3D12 runs vary materially. Broader producer/cross-view scheduling and the
D3D12 CPU/tail cost remain connected follow-ups, not completed performance
targets. The user-authorized architectural boundary is delivered without
lowering rendering quality.

The original measure2 timing batch passed API/thermal checks but five
Vulkan runs had 51.604-125.577 ms mean time outside recorded rendering spans,
including the unchanged baseline. Those runs are not suitable for attributing
a change to scene collection. All twelve runs were repeated as measure3 with
the same executable files, settings and frame range, replacing PowerShell
output pipes with direct inherited stdout/stderr file handles. No engine
change was made for the repeat, and no individual frames/runs were selected
for favorable timing. Initial scripts, analyses, sheets and raw results remain
in initial-timing/ and results/measure2-*. The final runs' mean time outside
recorded spans is 0.153-0.314 ms. This is consistent with a logging/harness
stall; the exact operating-system cause is not established.

Timing-image review accepts explicit small exceptions: D3D12 wet maximum
RGB8 differences 3 and 9, with 30 and 85 pixels above one; dry maximum 6,
12 pixels above one. Vulkan wet is within one level in the first pair and
maximum 3/61 pixels above one in the second; dry stays within one. Four
D3D12 wet pixels and one dry pixel exceed four levels. Whole-scene and
enlarged-crop review shows no visible change. This is not pixel identity.


Active CPU means elapsed frame time minus recorded frame-start wait/presentation,
not total execution across threads. CPU and GPU overlap. No profiling-off,
cold-loading, other-quality, Linux or new retail-game performance result is claimed.

Evidence: artifacts/lighting-foundation/view-family-collection-20261030/
on controller and WORKER_EVIDENCE_ROOT/phlosion-view-family-collection-20261030 on gpu-worker.
