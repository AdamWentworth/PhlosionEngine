# Retained scene draw preparation

2026-10-29. Follow-up to [shared view submission](SHARED_VIEW_SUBMISSION.md).
Implementation and all builds/tests run on gpu-worker; controller reviews and owns
the accepted source. No local inference or local GPU execution.

## Normal-path change

Published immutable glTF assets now own material-binding templates and ordered
primitive descriptions. Opaque/masked then transparent ordering stays authored.
Pose/placement records own their inputs and composed node transforms. Per-view
selections own exclusions and culling results. The normal draw adapter consumes
these records on both Windows backends, including shadow/reflection/main views.

Input comparison checks matrix/palette contents, not merely a pose address or
timestamp. In-place animation changes, camera/culling matrices, exclusions,
translucency, prewarm and specialized material modes therefore cannot reuse
incompatible records. Live light/weather, emission, color and camera values are
still applied per call; skinning, motion and per-object native submission remain
live. Composed-material suppression retains precedence over culling statistics.
All input validation precedes GPU drawing.

Each asset retains at most four pose/placement records and sixteen view selections,
with 8 MiB and 2 MiB logical backing-buffer budgets respectively. Oversized records
can serve a call without entering the cache. Leased immutable records survive
eviction. These are per-asset cache bounds, not a global renderer memory limit;
asset metadata, allocator overhead and outstanding consumer leases are separate.

Copying or moving an asset drops owner-specific bindings; mutable/manual copies
build fresh plans. A move clears the source handle too. Review caught and fixed
the copied-owner/address-reuse edge case before final qualification. Failed loads
do not publish plans or replace the preceding asset snapshot.

The editor project ABI is 45 because the asset layout changed. Model ABI21 and
battle-VFX ABI18 remain unchanged; their renderer interfaces did not change.
Normal host dependencies rebuild all loaded model/preview/game modules.

## Reference mechanism and remaining architectural work

Read-only source anchors recorded in trace.json:
renderMaterial.cxx / PrepareRenderStagePassesRange (2785), prepared stage data
separate from per-object submission; renderCollector.cpp / DoCull_MainScene (339),
DoCull_Cascades (367), explicit collection and joined work. This is an independent
implementation of those ownership/reuse principles, not a source copy or proof
that the supplied snapshot equals current retail.

Native command recording is still serial. D3D12 uses commandList_ and mutable
per-frame upload offsets; Vulkan uses frames[currentFrame].commandBuffer and
shared binding/uniform state. Parallel recording requires per-worker command
allocators/pools, descriptor/upload ranges, immutable native submission packets,
explicit resource barriers, ordered joins and fence retirement. The owned CPU
records delivered here are a prerequisite, not that completed job system.

Other producers, game-level shadow/probe revision traversal, remaining camera/
material preparation, general visibility scheduling, C7/C8, shader GPU costs,
cold preparation, Linux and the 16.67 ms whole-frame gate remain open. Lighting,
Hybrid material-specific rain impacts/puddle rings, shaders and quality budgets
are unchanged.

## Verification and performance

Optimized MSVC RelWithDebInfo engine editor/runtime and required game/model/
preview modules build successfully. Five CPU groups pass: EngineContracts,
WorldMotionHistory, EnvironmentPackets, PreparedViews and ModelInstance.
Contracts cover reuse, same-address pose mutation, dynamic factors, exclusions,
culling/prewarm/translucency/material modes, cache eviction, invalid inputs,
reload and copied/moved ownership. Twenty repeated identical calls build one
instance/view record and reuse it nineteen times.

Twenty-eight validation-enabled native runs pass; all twenty reference image
pairs are exact on Windows D3D12/Vulkan. The new real-glTF fixture covers static
and skinned geometry, two placements, changing pose/light/emission, exclusions
and reload. Its final three frames build six instance/view records and reuse
them forty-two times instead of rebuilding all forty-eight submissions.

Twelve fresh baseline/candidate pairs cover runtime wet/dry/orbit/battle and
editor wet/battle. Required clustered, shared-surface and probe phases remain;
no duplicate SSR source pass, API errors or invalid/dropped profile frames.
Rain state matches. Both complete sheets were reviewed. Vulkan is within one
RGB8 level in every pair. D3D12 battles are exact; runtime wet/dry/orbit maxima
are 5/4/4. Editor wet max 11 is isolated at a thin canopy/rain edge: seven pixels
over 4, two over 8, none over 16. No broad artifacts or missing geometry were seen.
The initial partial qualification was superseded after the copy/move lifetime
correction; only the corrected build's v2 scenes support this acceptance.

All 122 files of the accepted previous baseline remain unchanged; all 61 external
shader/material payload files match the candidate. Lighting equations, native
shader code and sample budgets did not change. No controller build/test execution.
Linux and other quality tiers are not newly qualified by this pass.

## Matched warm timing

EVGA RTX 2060 SUPER GPU 1, LUID 000000000000be8e,
UUID GPU_1_UUID. Native 1920x1080 Medium,
fixed night/overcast, heavy Hybrid rain and matching dry control. Validation off,
profiling on. Wet ABBA and dry AB on each API, 220 frames/run; analyze 40..179.
Twelve accepted runs, equal paired settings, no API errors/thermal flags,
maximum 63 C. No builds or competing GPU workload overlapped timing. Cold shader
preparation is excluded. Milliseconds:

| API / scene | GPU mean | Whole CPU mean | Active CPU mean | Whole CPU p95 | Whole CPU p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3d12 / wet | 17.251 -> 17.245 | 17.981 -> 17.782 | 13.673 -> 13.619 | 28.084 -> 28.923 | 30.812 -> 33.184 |
| d3d12 / dry | 12.323 -> 11.747 | 13.195 -> 11.976 | 9.922 -> 8.757 | 21.626 -> 20.769 | 24.423 -> 21.715 |
| vulkan / wet | 18.430 -> 18.357 | 18.252 -> 18.180 | 6.036 -> 5.819 | 19.137 -> 19.231 | 20.209 -> 20.516 |
| vulkan / dry | 13.574 -> 13.579 | 13.396 -> 13.399 | 4.153 -> 3.823 | 14.380 -> 14.176 | 16.320 -> 14.372 |

Vulkan wet active CPU falls 0.217 ms (3.6%), dry 0.330 ms (7.9%). Its wet static
surface-capture CPU phase falls 0.542 -> 0.487 ms. Wet GPU changes only -0.073 ms;
dry GPU is effectively flat. This produces CPU headroom while wet rendering
remains GPU-bound.

D3D12 wet active CPU is effectively flat (-0.054 ms); GPU mean is also flat
(-0.006 ms). Its wet whole-CPU p95 worsens 28.084 -> 28.923 ms and p99
30.812 -> 33.184 ms; active-CPU p99 also worsens 24.386 -> 27.452 ms.
Do not claim improved wet tails. The single dry AB control improves active CPU
by 1.164 ms and whole CPU by 1.218 ms, but this is an observed paired result,
not a general GPU/shader speedup. Wet GPU medians are 17.647 -> 17.668 ms
D3D12 and 18.377 -> 18.333 ms Vulkan. All CPU/GPU medians, tails and per-phase
results remain in analysis-summary.json and analysis-matched.json.

Timing images were separately reviewed: D3D12 wet repeat2 max 8 is confined to
a thin pump/rain edge (seven pixels over 4), other D3D12 pairs max 2/4; Vulkan
pairs remain within 1. Rain states match. The 16.67 ms whole-frame gate remains
open, as do CPU tails and GPU rain/contact/haze costs.

## Matrix checkpoint and evidence

Shared view preparation and draw submission moves from 55-70% to **65-75%**
estimated technique coverage. Newly delivered: asset-owned draw/material
descriptions, pose/placement reuse, view-selection reuse, bounded ownership
and copy/move/reload invalidation in the normal path. This is an engineering
scope estimate, not a retail visual-equivalence percentage or FPS ratio.
Other matrix categories are unchanged. Native parallel recording is next.

Worker evidence: WORKER_EVIDENCE_ROOT/phlosion-retained-draws-20261029.
Compact receipts/images are mirrored to engine artifacts/lighting-foundation/
retained-draws-20261029. Source manifests, reference hashes, CPU/native/full-scene
receipts, image reviews, timing summaries and strict integration receipts
identify the exact tested sources and payload. Large raw runs remain on worker.
