# Shared glTF material inputs

2026-10-29. Follow-up to [scene input preparation](SCENE_INPUT_PREPARATION.md).
Implementation, optimized builds and all execution ran on gpu-worker; controller
reviews, integrates and owns the source.

## Normal rendering path

Published glTF assets now retain validated material input snapshots separately
from pose/geometry records and view selection. Tint, base-color overrides,
emission overrides and probe channels are compared by their contents, then
owned and evaluated once for reuse across shadow, probe, reflection, depth,
motion and main submissions. Same-address edits cannot reuse stale values.
Authored immutable texture/scalar templates remain owned by the original plan.

Camera, environment, alpha/material mode, composition eligibility, backend
prepared-view tokens and resource scopes remain current-view data. Only
selected materials acquire those view bindings; an empty selection prepares
none. Composition still takes precedence over culling in draw statistics.
Invalid inputs are rejected before submission, including nonfinite channels.
There is no new opt-in, shader/quality change or modified draw order.

Each asset retains at most four input snapshots and 1 MiB of logical vector
storage. Oversized records may serve a call without entering the cache.
Outstanding immutable leases survive eviction. This is not a global memory
limit: allocator/record overhead and consumer-held leases are separate.
Manual/mutable asset copies continue to build fresh owner-specific plans.
No existing public structure or plugin ABI changed.

## Source mechanism and remaining work

This independently implements the preparation/consumption split recorded in
RETAINED_DRAW_PREPARATION.md: renderMaterial.cxx / PrepareRenderStagePassesRange
at 2785 and joined collection in renderCollector.cpp. Existing research evidence
was reused; this pass did not re-audit or authenticate the supplied source snapshot
as current retail. It does not copy source implementations.

This completes the shared scalar-input step for glTF. Native geometry/material
resource bindings still resolve during individual draws (D3D12RenderBackendTextures.cpp
and VulkanRenderBackendMaterials.cpp consume WorldMaterialBindingCache).
Next is an explicit, scope-safe prepared native binding handoff across dependent
views, followed by broader producer/view collection. Existing retained geometry,
poses and parallel native command recording remain in use.
Shared preparation/submission remains **75-85% estimated technique coverage**.
C7/C8, broader specialized producers, Linux and the 16.67 ms whole-frame gate
remain open; this is not complete retail-engine or performance parity.

## Verification

Optimized MSVC RelWithDebInfo engine editor, game runtime/plugin and dependencies
build. Nine CPU groups pass. Added contracts cover value-based material reuse,
same-address color/emission/channel mutation, live view lighting, eviction with
leased inputs, empty selection and invalid material inputs. Existing contracts
cover skinning, composition, exclusions, culling, prewarm, reload and copy ownership.

Ten validation-enabled native checks pass on D3D12/Vulkan: real glTF retained
versus fresh preparation, wet relighting and emissive relighting. Native images
are identical. Each final glTF fixture makes 48 requests: six input preparations
and 42 reuses, evaluating 12 material records instead of 96. These are operation
counts, not an 87.5% frame-time improvement.

Twelve runtime/editor comparison pairs cover wet/dry/orbit/battle on both APIs.
3 accepted baseline captures were reused only after verifying all 122
baseline payload files; remaining baselines were captured fresh. Every candidate
was captured fresh. Animation, gameplay, motion, rain, surfaces, environment and
audio state match. All 79 engine shader/material/native shader payloads match.
Native parallel-recording overlap remains present. No API errors or new D3D12
warning IDs. Full sheets and worst-difference crops were reviewed:
- d3d12 runtime wet: maximum RGB8 difference 1, 0 pixels above one.
- d3d12 runtime dry: maximum RGB8 difference 5, 213 pixels above one.
- d3d12 runtime orbit: maximum RGB8 difference 1, 0 pixels above one.
- d3d12 runtime battle: maximum RGB8 difference 0, 0 pixels above one.
- d3d12 editor wet: maximum RGB8 difference 7, 23 pixels above one.
- d3d12 editor battle: maximum RGB8 difference 0, 0 pixels above one.
- vulkan runtime wet: maximum RGB8 difference 0, 0 pixels above one.
- vulkan runtime dry: maximum RGB8 difference 1, 0 pixels above one.
- vulkan runtime orbit: maximum RGB8 difference 0, 0 pixels above one.
- vulkan runtime battle: maximum RGB8 difference 0, 0 pixels above one.
- vulkan editor wet: maximum RGB8 difference 0, 0 pixels above one.
- vulkan editor battle: maximum RGB8 difference 0, 0 pixels above one.

The strict maximum-one image flag is false.
Any exceptions above remain in the evidence and are not silently relaxed.
The initial reused Vulkan baselines differed more. An unchanged fresh wet baseline
was pixel-identical to the candidate, so all six Vulkan baselines were refreshed.
Original comparisons and the control receipt are preserved; no cause is asserted
for that older-capture variation.

## Matched warm timing

EVGA RTX 2060 SUPER GPU 1, LUID 000000000000be8e,
UUID GPU_1_UUID; native 1920x1080 Medium,
night/overcast, heavy Hybrid rain and dry control. Twelve fresh runs, wet ABBA
and dry AB per API, frames 40..179. Validation off, phase profiling on; identical
renderer/weather/quality settings. No excluded runs or thermal flags; maximum
temperature 63 C. All values below are milliseconds.

| API / scene | GPU mean | Whole CPU mean | Active CPU mean | CPU p95 | CPU p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3d12 / wet | 17.652 -> 17.614 | 18.003 -> 17.930 | 13.265 -> 12.924 | 28.005 -> 28.416 | 29.870 -> 34.180 |
| d3d12 / dry | 12.400 -> 12.228 | 12.571 -> 12.396 | 8.778 -> 8.242 | 20.625 -> 20.549 | 20.982 -> 21.730 |
| vulkan / wet | 18.421 -> 18.425 | 18.233 -> 18.241 | 5.974 -> 5.924 | 19.595 -> 19.473 | 20.498 -> 20.103 |
| vulkan / dry | 13.650 -> 13.678 | 13.458 -> 13.487 | 4.233 -> 4.279 | 14.553 -> 14.630 | 16.395 -> 15.102 |


Active CPU is elapsed frame time minus recorded frame-start wait and presentation;
it can include internal waits/helper joins and is not total CPU execution across
threads. CPU/GPU work overlaps; do not add these times. Complete run distributions,
phase costs, image differences and telemetry are preserved. Small changes within
run variation are not evidence of a frame-rate gain. D3D12 wet CPU p99 worsens
from 29.870 to 34.180 ms despite the lower mean; dry p99 also worsens. Vulkan dry
active CPU increases slightly. These are retained costs, not a uniform performance
win. Per-run tail analysis is saved in d3d12-tail-analysis.json: slow frames still
spend substantial elapsed time inside static geometry and foliage submission,
but those scopes do not isolate CPU execution, helper waits or driver costs.
The user-authorized architectural foundation is accepted with this explicit
limitation; native binding work must continue measuring whole-frame tails. No profiling-off or retail
2077 comparison, Linux qualification, other quality-tier timing or clean-start
loading claim is made.

Evidence: artifacts/lighting-foundation/shared-geometry-inputs-20261029/ in
the canonical engine checkout and WORKER_EVIDENCE_ROOT/phlosion-shared-geometry-inputs-20261029
on gpu-worker. Includes tested-source.json, native/CPU receipts, baseline-reuse.json,
state-evidence.json, recording-evidence.json, qualification.json, visual reviews,
analysis-matched.json and raw per-run profiles.
