# Native geometry and material bindings across views

2026-10-29. Follow-up to [shared material inputs](SHARED_MATERIAL_INPUTS.md).
All implementation, builds and tests ran on gpu-worker. controller owns review,
integration and commits. Task/evidence directory uses the UTC suffix 20261030.

## Normal rendering path

Published immutable glTF assets now prepare native geometry and material bindings
once for a managed resource scope and reuse checked handles across dependent
views. D3D12 retains the mesh plus descriptor-block index/use-texture value;
Vulkan retains the mesh plus material descriptor set. Ordinary draws no longer
repeat the geometry-key and eight-role material-identity lookup for these assets.
The existing native per-view shading, uploads, ordered packets and parallel
recording remain responsible for each actual submission.

Camera, pose/skinning, transforms, lighting, material factors and frame uploads
remain live. Binding identities and sampling must remain immutable until another
material handle is prepared. Anonymous inputs, frame-owned snapshot textures,
unmanaged assets, mutable/manual copies and unsupported backends use the existing
path. The glTF producer's authored texture identities satisfy this contract.
Shader prewarming remains mandatory even when a material binding is retained.
There is no new feature toggle, quality reduction, shader change or draw-order change.

IWorldPreparedResources is an additive owner-thread extension. Existing
IRenderBackend and WorldTextureData layouts/vtables do not change. The concrete
backend classes implement the new extension and are rebuilt with their consumers.
This is not a new plugin ABI requirement.

## Lifetime and bounds

Handles carry table identity, generation, scope and slot. Wrong owner, wrong scope,
out-of-range slots and old generations are rejected before accessing native
resources. Unique generations also prevent reuse after a renderer is reconstructed
at the same address. A pending scope release closes admission immediately.
Existing GPU fences still govern physical destruction. Handle tables invalidate
before any referenced native allocation or descriptor table is retired.

Each geometry/material table accepts at most 16,384 entries. Capacity exhaustion
keeps existing handles valid and falls back to normal resolution for further
resources. Each asset retains at most four renderer/scope records with 2 MiB of
logical token-vector storage; oversized records serve a call without retention.
These are component limits, not a global allocation budget. Global invalidation
on scope collection is conservative and may reprepare unaffected handles.

## Source mechanism and remaining work

This independently implements the preparation/consumption boundary recorded in
RETAINED_DRAW_PREPARATION.md: renderMaterial.cxx / PrepareRenderStagePassesRange
at 2785 and renderCollector.cpp / DoCull_MainScene:339 / DoCull_Cascades:367.
Existing research evidence was reused; no source implementation was copied or
newly authenticated as current retail. The native-binding step is delivered for
published glTF assets, not every specialized producer.

Next: joined producer/view collection and scheduling, reusing prepared scene
records across the full view family. Other producers, broader material variants,
complete C7/C8 policy, Linux qualification and the 16.67 ms whole-frame target
remain open. Shared preparation/submission remains **75-85% estimated technique
coverage**, not a measured retail visual/FPS ratio.

## Verification

Optimized MSVC RelWithDebInfo editor, game runtime/plugin and dependencies build.
All nine CPU groups pass, including stale/wrong-owner/wrong-scope handles,
capacity preservation, renderer-address reuse and shader-prewarm regression.
Ten native checks pass on D3D12/Vulkan, with explicit Vulkan layer activation.
The real glTF fixture covers static/skinned geometry, two placements, changing
pose/light/emission, exclusions, reload, unrelated-scope collection and release
while active. Per API, seven geometry and seven material preparations serve
208 submissions; three stale submissions are rejected. Fresh/retained images
are identical. These operation counts are not a frame-time speedup.

Twelve comparison pairs cover runtime wet/dry/orbit/battle and editor
wet/battle on both APIs. The same-task baseline images were reused after all 122
baseline payload files were verified; every final candidate was captured fresh.
Rain, motion, animation, gameplay and environment state
match. All 79 shader/material/native-shader payloads are identical to the accepted
baseline. Parallel recording remains active; no API errors or invalid/dropped
profile frames. Full sheets and outlier crops were reviewed:
- d3d12 runtime wet: maximum RGB8 difference 1; 0 pixels above one.
- d3d12 runtime dry: maximum RGB8 difference 2; 2 pixels above one.
- d3d12 runtime orbit: maximum RGB8 difference 1; 0 pixels above one.
- d3d12 runtime battle: maximum RGB8 difference 0; 0 pixels above one.
- d3d12 editor wet: maximum RGB8 difference 2; 8 pixels above one.
- d3d12 editor battle: maximum RGB8 difference 0; 0 pixels above one.
- vulkan runtime wet: maximum RGB8 difference 0; 0 pixels above one.
- vulkan runtime dry: maximum RGB8 difference 1; 0 pixels above one.
- vulkan runtime orbit: maximum RGB8 difference 0; 0 pixels above one.
- vulkan runtime battle: maximum RGB8 difference 0; 0 pixels above one.
- vulkan editor wet: maximum RGB8 difference 0; 0 pixels above one.
- vulkan editor battle: maximum RGB8 difference 0; 0 pixels above one.

Strict maximum-one image flag: false.
Any image exceptions remain explicit in visual-review.json and image details.

Normal-path counters (preparations are cumulative, not per-frame uploads):
- check-runtime-d3d12-wet-candidate-v6: 441 geometry / 87 material preparations, 35559 prepared submissions; last ten frames: {'geometry': 0, 'materials': 0, 'draws': 5664, 'stale': 0}.
- check-runtime-d3d12-dry-candidate-v6: 441 geometry / 87 material preparations, 25808 prepared submissions; last ten frames: {'geometry': 0, 'materials': 0, 'draws': 4072, 'stale': 0}.
- check-runtime-d3d12-orbit-candidate-v6: 441 geometry / 87 material preparations, 35568 prepared submissions; last ten frames: {'geometry': 0, 'materials': 0, 'draws': 5672, 'stale': 0}.
- check-runtime-d3d12-battle-candidate-v6: 441 geometry / 87 material preparations, 86462 prepared submissions; last ten frames: {'geometry': 0, 'materials': 0, 'draws': 7872, 'stale': 0}.
- check-editor-d3d12-wet-candidate-v6: 441 geometry / 87 material preparations, 35559 prepared submissions; last ten frames: {'geometry': 0, 'materials': 0, 'draws': 6372, 'stale': 0}.
- check-editor-d3d12-battle-candidate-v6: 441 geometry / 87 material preparations, 86129 prepared submissions; last ten frames: {'geometry': 0, 'materials': 0, 'draws': 8856, 'stale': 0}.
- check-runtime-vulkan-wet-candidate-v6: 441 geometry / 87 material preparations, 35860 prepared submissions; last ten frames: {'geometry': 0, 'materials': 0, 'draws': 5664, 'stale': 0}.
- check-runtime-vulkan-dry-candidate-v6: 441 geometry / 87 material preparations, 26109 prepared submissions; last ten frames: {'geometry': 0, 'materials': 0, 'draws': 4072, 'stale': 0}.
- check-runtime-vulkan-orbit-candidate-v6: 441 geometry / 87 material preparations, 35869 prepared submissions; last ten frames: {'geometry': 0, 'materials': 0, 'draws': 5672, 'stale': 0}.
- check-runtime-vulkan-battle-candidate-v6: 441 geometry / 87 material preparations, 87064 prepared submissions; last ten frames: {'geometry': 0, 'materials': 0, 'draws': 7872, 'stale': 0}.
- check-editor-vulkan-wet-candidate-v6: 441 geometry / 87 material preparations, 35860 prepared submissions; last ten frames: {'geometry': 0, 'materials': 0, 'draws': 6372, 'stale': 0}.
- check-editor-vulkan-battle-candidate-v6: 441 geometry / 87 material preparations, 86731 prepared submissions; last ten frames: {'geometry': 0, 'materials': 0, 'draws': 8856, 'stale': 0}.

Windows initially blocked an unsigned development Vulkan loader, the game
executable and the validation DLL. The user resolved the worker security setting.
Both comparison packages use the same verified signed system loader 1.4.341.0;
the original accepted baseline package is preserved. Initial Vulkan results
without the layer were not accepted and all native checks were rerun. Baseline and candidate startup
file-read stalls (including the prepared Umbreon animation package on the worker D: drive)
resolved without modifying game code; startup timing is
not included in the warm performance claim.

## Matched warm timing

EVGA RTX 2060 SUPER GPU 1, LUID 000000000000be8e,
UUID GPU_1_UUID. Native 1920x1080 Medium,
fixed night/overcast, heavy Hybrid rain and matched dry control. Twelve fresh
runs: wet ABBA and dry AB per API, 220 frames/run, analyzing frames 40..179.
Validation off, phase profiling on; all twelve selected runs pass with no thermal flags.
Maximum temperature 63 C. The initial measurement set exposed 3-4 ms of
redirected-log overhead from emitting the new counter line field by field. The
existing recording-stat convention of one buffered write fixes that instrumentation
cost; complete CPU/native checks and final scene candidates were rerun, followed
by a fresh timing set. That set recorded one software thermal flag in D3D12 wet
baseline repeat 2 (58 C reported GPU temperature); no cause is inferred from that
single sample. The entire D3D12 wet ABBA block was repeated as measure3-*, keeping
the original raw results and rejection receipt. The other eight measure2-* runs
are unchanged. selected-timing-runs.json identifies all twelve final runs;
thermal-retest.json and thermal-flag-set/ retain the original analysis.
The older unbuffered measure-* runs and diagnosis remain under
unbuffered-profile/ and results/. No individual frame was selectively removed.
Milliseconds:

| API / scene | GPU mean | Whole CPU mean | Active CPU mean | CPU p95 | CPU p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3d12 / wet | 17.088 -> 17.192 | 17.306 -> 17.316 | 12.527 -> 11.066 | 27.581 -> 26.778 | 29.811 -> 32.511 |
| d3d12 / dry | 12.588 -> 12.529 | 12.919 -> 12.749 | 9.131 -> 8.821 | 21.045 -> 21.240 | 24.175 -> 22.636 |
| vulkan / wet | 18.404 -> 18.431 | 18.218 -> 18.258 | 5.893 -> 5.736 | 19.396 -> 19.568 | 20.292 -> 20.191 |
| vulkan / dry | 13.659 -> 13.638 | 13.461 -> 13.456 | 4.414 -> 3.983 | 14.716 -> 14.672 | 15.777 -> 15.292 |


Static geometry capture CPU cost decreased in all four comparisons:
D3D12 wet 1.844 -> 1.614 ms and dry 2.113 -> 1.937 ms; Vulkan wet
0.482 -> 0.335 ms and dry 0.562 -> 0.379 ms. This agrees with the
operation counts showing native binding reuse.

Whole-frame GPU/CPU time is essentially flat. Wet GPU means are D3D12
17.088 -> 17.192 ms and Vulkan 18.404 -> 18.431 ms. Wet whole CPU means
are D3D12 17.306 -> 17.316 ms and Vulkan 18.218 -> 18.258 ms.
D3D12 wet active elapsed CPU decreased 12.527 -> 11.066 ms, but the
individual runs vary materially and some time shifts into recorded waits.
Do not interpret that entire difference as saved execution or an FPS gain.
Vulkan wet active elapsed CPU decreased 5.893 -> 5.736 ms.

D3D12 wet CPU p95 improved 27.581 -> 26.778 ms, while p99 worsened
29.811 -> 32.511 ms. Dry p95 also slightly worsened 21.045 -> 21.240 ms.
The D3D12 CPU/tail follow-up remains open. GPU means remain above the
16.67 ms target in heavy rain. This is an accepted architecture step
with measured preparation savings, not a claimed whole-frame speedup.

After buffering the diagnostic output, the candidate's mean time outside
top-level recorded CPU spans is 0.567-0.708 ms across all runs, rather
than the earlier 3-4 ms instrumentation penalty. Residual diagnostic
overhead remains; these are profiled runs, not profiling-off measurements.

Timing-image review preserves explicit exceptions: D3D12 wet pairs have
60 and 3 pixels above one RGB8 level (maximum differences 4 and 2);
dry has 16 (maximum 8, five pixels above four). Vulkan pairs remain within
one level. Whole scenes and enlarged outliers show no visible change;
this is manual visual acceptance, not strict pixel identity.


Active CPU is elapsed frame time minus recorded frame-start wait and presentation,
not total CPU execution across threads; internal/helper waits may remain.
CPU/GPU overlap. Complete distributions, tails, phase costs and telemetry remain
in analysis-matched.json. No profiling-off, retail-game comparison, cold-loading,
other quality-tier or Linux result is claimed.

Evidence: artifacts/lighting-foundation/native-resource-bindings-20261030/
in the canonical engine checkout, and WORKER_EVIDENCE_ROOT/phlosion-native-resource-bindings-20261030
on gpu-worker. Includes source/payload manifests, CPU/native checks, scene images,
state/recording/resource receipts, matched profiles and strict integration receipts.
