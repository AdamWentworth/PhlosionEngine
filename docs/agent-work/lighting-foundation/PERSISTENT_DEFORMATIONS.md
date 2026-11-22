# Persistent deformation and shared material preparation

2026-10-30 task. Baseline engine ebce3a1, game c50eb76, ModelSupport c7f972e.
Implementation, optimized builds, CPU/GPU checks and processing ran on gpu-worker
Windows with the EVGA GPU1. controller owns integration and commits.

## Completed acceptance boundary

**8/8 fixed preparation/submission contracts verified** for the supported normal
Windows world path: collected glTF, native models/afterimages and foliage, their
camera/shadow/probe views, shared preparation and private native command recording.
The acceptance-contract.json was fixed before final qualification; the evidence
mapping is in acceptance-results.json. This replaces the repeated 90-95% estimate
for these mechanisms with an explicit, auditable completed checklist.

| Contract | Verified evidence |
| --- | --- |
| P1: Immutable, owned collection snapshots for native models, glTF, foliage and their afterimage/history consumers; explicit lifetime under resource replacement. | ModelInstance/WorldMotionHistory CPU contracts, immutable owned glTF/native/foliage records, real DLL-unload regression, two four-afterimage scene pairs. |
| P2: Admit required camera/shadow/probe views, coalesce duplicate requests, finish independent preparation jobs before any consuming draw; failures join and do not publish readiness. | PreparedViews and joined-worker failure contracts; native prepared-view fixtures; collected camera/shadow/probe scene counters. |
| P3: Prepare and retain geometry/material/pass descriptions and frame-native material constants for reuse before native command recording; separate live object/pose/view inputs. | WorldMaterialFrame CPU contracts; glTF native repeated-draw assertion; frame material build/reuse counters on both APIs. |
| P4: Registered immutable deformation versions, four retained CPU versions, unchanged GPU residency, keepalive, explicit per-view previous state/reset and layout-safe consumers. | WorldDeformationStorage retained-version/keepalive tests; unchanged glTF native frame uploads zero palettes; native owned current/previous motion readback. |
| P5: One owned deformation upload batch, disjoint joined copies, store/memory flush before publication; allocation/copy/flush failure cannot expose partial ready state. | Storage admission/copy/flush failure and retry contracts; native split admission; at most one joined upload per normal world frame. |
| P6: Bounded residency with pressure reclamation, reference lifetime, stale-reference rejection and actual GPU-fence retirement; no overwrite of in-flight ranges. | Storage GPU-lag, pressure, owner-release, stale/reset contracts; backend fence serials; native resize/retirement and full battle shutdown. |
| P7: Private native command storage/state per recording lane, explicit dependent joins, ordered pass execution and frame-fence lifetime on D3D12 and Vulkan. | WorldCommandRecording contracts; validation-enabled private view recording, ordered execution and retirement on both APIs. |
| P8: CPU failure/lifetime contracts, real native current/previous deformation checks, normal runtime/editor/quality/afterimage coverage, reviewed image/state equivalence and matched optimized costs. | 18 CPU groups plus private foliage arithmetic; 13 native checks; 18 scene pairs and 2 afterimage pairs; reviewed images and 16 fresh matched timing runs plus 4 separate diagnostics. |


## Implementation

Immutable registered pose versions now survive producer updates and collected
view/history lifetime. Four CPU versions per authored part retain unchanged and
recent poses; external views may retain older versions. Native models, glTF and
wind foliage carry these identities through current and previous consumers.
Unchanged admitted versions keep their GPU allocation without another copy.
An owned version missing admission throws instead of silently using a lazy upload.

The owner gathers one batch, reserves ranges, joins disjoint copy workers, applies
store fences/flushes, and only then publishes. Each resident range tracks its last
submitted frame serial. Actual native frame-fence completion controls reuse.
Cold completed ranges can be evicted under pressure; in-flight allocations cannot.
High/low watermarks are two thirds/one quarter. CPU ownership is weakly tracked
by residency; producer release does not keep CPU pose data alive indefinitely.

Collected glTF material values now prepare in joined jobs after visibility and
are reused at submission. D3D12/Vulkan share frame-native constant records.
D3D12 object constant reservation falls from 768 to 256 bytes; a separate aligned
material block is allocated only for a new value. Cache hits reuse its native
address. Constants still stay append-only until their frame fence. Shader math,
rain/impact behavior, assets and quality budgets are unchanged; all 61 staged
shader/material payloads are byte-identical.

D3D12 reuses its existing 128 MiB skin buffer. Vulkan adds a 64 MiB persistent
host-visible skin buffer rather than aliasing live frame-transient allocations.
That is an explicit memory cost. Registered per-owner identities can upload
identical bytes separately; this is not global cross-owner content deduplication.
Legacy immediate rain parameters remain a transient upload.

## Failure found and fixed

The first complete battle run exposed a shutdown crash: weak residency could
outlive an unloadable model DLL's shared-pointer control-block code. Symbolization
and a minimal module-unload test reproduced it. Pose references now use atomic,
non-virtual intrusive strong/weak lifetime with no saved plugin deleter function.
The regression passes while both live and expired references survive DLL unload.
All candidate native/scene checks were rerun after the fix; superseded scenes and
the original failing test are retained in the evidence. Model ABI25 and editor
ABI46 require consistent modules; the game assertion now follows the new ABI.

## Source correspondence and adapters

The supplied renderSkinManager.cxx snapshot SHA-256 is
2651cb46920e15e2dc39c3b163743dbcd8a4172b3f8ce9ae2206d68cbb5b7b54.
Register/release, retained versions, keepalive, joined flush, pressure and pinning
were traced together, alongside renderMaterial.cxx frame/stage preparation and
the collector/frame producer order. Exact source hashes/anchors are in the contract.

Phlosion uses immutable intrusive references instead of source index/generation
handles; both enforce stale lifetime rather than retaining bare reusable addresses.
Previous pose belongs to each logical view, so skipped views, reset and afterimages
do not mistake the manager's last update for that view's previous pose.
Incompatible motion layouts invalidate history and use current state. Buffer
sizes, packing, native API bindings and material lookup structures are adapters.
D3D12 still places view-projection in object constants; Vulkan uses push constants.
Pose sampling/collection remains owner-controlled; independent prepared views,
material work, copies and recording use joined jobs.
Motion history retains its CPU inspection vector as well as the registered GPU
version; removing that compatibility copy is a possible later cost cleanup.
Standalone legacy span-based consumers remain transient, while the qualified
normal glTF/native/foliage producers use registered ownership.

The snapshot is not authenticated as current retail. Passing every listed
contract does not prove pixel identity with retail, every Cyberpunk producer,
the entire renderer or identical frame time. C7/C8, other matrix categories,
Linux qualification and sustained 16.67 ms whole-frame acceptance remain separate
unfinished gates. They were not silently removed from the broader roadmap.

## Visual and runtime verification

18 CPU groups plus private foliage arithmetic and 13 validation-enabled native
checks pass. 18 runtime/editor/preset pairs and two four-afterimage pairs preserve
state and required rendering stages. No palette cache failure or bypass is
accepted. Candidate world frames use at most one preparation copy join.
Vulkan editor composition resets its world-state/material cache after the 3D
stage, so final material reuse counters survive in 18/20 runs; persistent pose
counters cover all 20. Both native APIs independently verify material reuse.
Owned native motion checks retain correct current/previous data after producer
input mutation; unchanged glTF frames issue no deformation uploads.

Existing baseline captures were reused only after full 124-file payload identity
with the preceding qualified candidate; reused-baselines.json records the sources.
Every final candidate capture and timing run is new.

- d3d12: 1/9 exact pairs; maximum 6/255; maximum mean channel difference 0.010527/255.
- vulkan: 5/9 exact pairs; maximum 1/255; maximum mean channel difference 0.000017/255.

The full image review, residual locations, afterimage comparisons and acceptance
decision are in visual-review.json and contact sheets. These captures are not all
pixel-identical. The ownership/material pass is accepted for preserved visible
output, with numerical residuals recorded rather than described as zero.

## Matched optimized cost

Native 1920x1080 Medium, night/overcast, RelWithDebInfo, EVGA GPU1; validation off.
Wet AB and battle ABBA per API provided twelve primary runs. The initial D3D12
wet pair had different GPU clocks and a slower candidate, so two extra wet pairs
ran in BA then AB order. All sixteen valid primary runs remain included, with
140 steady frames each; no favorable-run selection was performed. Four separate diagnostic runs retain phase/thread costs. Paired settings,
rain state, native errors, GPU thermal flags and timed-window boundaries are checked.

| API / scene | GPU mean ms | CPU frame mean ms | Active CPU mean ms |
| --- | ---: | ---: | ---: |
| d3d12 / wet | 16.142 -> 16.273 | 16.072 -> 16.311 | 8.744 -> 9.490 |
| d3d12 / battle | 16.909 -> 16.775 | 19.078 -> 18.494 | 16.759 -> 15.782 |
| vulkan / wet | 18.362 -> 18.292 | 18.196 -> 18.130 | 4.661 -> 4.765 |
| vulkan / battle | 18.068 -> 18.091 | 17.978 -> 18.018 | 9.231 -> 8.915 |


D3D12 battle CPU improves by 0.584 ms in this set; wet CPU rises by 0.239 ms
in aggregate and varies by run. GPU differences range from -0.134 to +0.131 ms.
Vulkan is effectively flat. The last wet D3D12 pair is 15.762 -> 15.811 ms GPU,
while the original pair was 15.886 -> 16.750 ms with different observed clocks.
All valid runs remain in the aggregate. There is no broad FPS gain claim;
battle CPU/GPU costs still leave the sustained whole-frame target open.

Timing captures add eight reviewed comparisons. Their largest residual is
63/255 beside a blue ceiling fixture, affecting ten pixels above 16/255.
Unchanged-baseline repeats also reach 63/255 with the same ten-pixel count.
This establishes baseline variation, not the exact cause. The residual images,
control comparisons and final acceptance are preserved in timing-image-review.json,
timing-review.jpg, timing-peak.jpg and final-review.json.

These compare Phlosion builds on the same worker. The table includes whole-frame
and active CPU cost; moving work between phases alone is not an optimization.
A single wet AB pair is insufficient for a stable speedup claim. A passed
mechanism checklist does not establish Cyberpunk retail performance parity.

Evidence: artifacts/lighting-foundation/persistent-deformations-20261030.
The game roadmap records strict dependency and final payload identity checks.
