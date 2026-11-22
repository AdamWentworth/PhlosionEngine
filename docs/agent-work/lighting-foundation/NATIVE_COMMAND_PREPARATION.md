# Native draw preparation and recording boundary

Follow-up: [native parallel recording](NATIVE_PARALLEL_RECORDING.md) completes the worker-storage/ordered-execution stage described below. This document records the preceding serial checkpoint.

2026-10-29. Follow-up to [retained scene preparation](RETAINED_DRAW_PREPARATION.md).
All implementation, builds, tests and GPU execution are on gpu-worker; controller
reviews, integrates and owns the source.

## Normal-path change

D3D12 cached indexed draws now resolve/upload shared view, object, skinning,
motion and material constants, descriptor handles, buffer views and pipeline
selection before calling a renderer-independent command-list adapter. Outline
and surface draws are captured together and retain their order. Immediate
non-indexed/uncached helpers reuse the extracted preparation/binding functions.

Vulkan indexed draws likewise prepare all seven descriptor sets and four dynamic
offsets, geometry, surface/outline push values, pipelines and viewport/scissor
before recording. Its binding cache and counters now belong to an explicit
command context. Existing immediate and indirect paths use that same primary
context, preserving binding reuse and frame diagnostics. Full viewport/scissor
comparison prevents same-size shadow-tile or clipping changes from being lost.

The normal production path uses these packets without a quality option or
experimental switch. The recording adapters cannot allocate uploads, look up
materials, choose render policy or access backend state. CPU values are copied;
native resource handles borrow the current frame/pass's lifetime.

## Ownership boundary and next connected step

This completes the indexed submission packet and binding-state separation.
It does **not** complete native parallel recording. Packets are consumed
immediately by the existing primary command list/buffer. They are not durable
resource leases and must not be queued beyond their owning frame/pass. The
serial preparation side still owns uploads, descriptors, transitions and
render-pass state. Private CPU context tests are not evidence of concurrently
executed native GPU command lists.

Next: frame-owned worker command allocators/pools and binding contexts; bounded
prepared packet ranges with explicit resource lifetime; ordered execution
across pass/barrier boundaries; fence retirement. Connect and qualify those
together before enabling native jobs. The concrete integration points are
D3D12RenderBackendLifecycle.cpp (frame allocator reset, ExecuteCommandLists and
frameFenceValues_) and VulkanRenderBackendLifecycle.cpp (frame.inFlight,
command-buffer reset, render-pass ownership and vkQueueSubmit). Existing
Vulkan passes record inline primary commands: secondary recording needs an
explicit compatible pass boundary, not merely a thread around each draw call.
Do not share a binding cache or reset a worker's native storage before its
owning frame fence completes.

Other producer traversal/revision work,
remaining material/camera preparation, C7/C8, shader GPU cost, cold preparation,
Linux and the 16.67 ms whole-frame target remain open.

Reference mechanism: renderMaterial.cxx / PrepareRenderStagePassesRange at 2785,
prepared stage inputs; renderCollector.cpp / DoCull_MainScene at 339 and
DoCull_Cascades at 367, owned collection work and explicit joins (including 511).
Read-only source hashes and anchors are in trace.json. This is independent
Phlosion implementation, not a source copy or proof that the supplied snapshot
equals the current retail executable.

## Qualification

Optimized MSVC RelWithDebInfo editor/runtime and required game/model modules
build. Six CPU groups pass: EngineContracts, WorldMotionHistory, EnvironmentPackets,
PreparedViews, WorldCommandRecording and ModelInstance. The new recording
contracts check frozen packet inputs, independent/concurrent command traces,
binding reuse inside one context, initial bindings in another, outline ordering,
disabled outlines, clipping/tile changes and pass resets. Mock traces check
ownership/order; the native tests below check the actual API adapters.

Twenty-eight validation-enabled native runs pass on D3D12 and Vulkan. All twenty
native reference image pairs are exact. All 61 external shader/material payload
files are unchanged, and the prior accepted 122-file payload remains intact.
Lighting, rain, Hybrid material-specific impacts and puddle rings, quality
budgets and assets are unchanged. Editor ABI45, model ABI21 and battle-VFX ABI18
are unchanged because no public asset/interface layout changed.

Two initial compile attempts failed on COM pointer dereferencing and a test
handle type; both were corrected before any candidate qualification. Their
logs are retained. No failing runtime result is represented as a pass.

Twelve fresh baseline/candidate scene pairs cover runtime wet/dry/orbit/battle
and editor wet/battle. All battle pairs are exact. Vulkan wet runtime/editor are
exact, and dry/orbit differ by at most one RGB8 level. D3D12 runtime wet/dry/orbit
maxima are 6/5/5. Editor wet max 12 is localized to a thin canopy/rain edge:
11 pixels exceed 4 and one exceeds 8. Both complete sheets and all worst crops
were inspected. No broad artifacts or missing geometry were seen.

Required clustered, shared-surface and probe phases remain, without a duplicate
SSR source pass, API errors or invalid/dropped profile frames. Paired rain states
match. Linux and additional quality tiers are not newly qualified here.

## Matched warm timing

EVGA RTX 2060 SUPER GPU 1, LUID 000000000000be8e,
UUID GPU_1_UUID. Native 1920x1080 Medium,
fixed night/overcast; heavy Hybrid rain and matched dry control. Validation off,
profiling on. Wet ABBA and dry AB on each API; 220 frames/run, analyze 40..179.
All 12 runs accepted, equal paired settings, no API errors or thermal flags;
maximum 62.0 C. No builds or competing GPU workload overlap timing.
Cold shader preparation is excluded. Milliseconds:

| API / scene | GPU mean | Whole CPU mean | Active CPU mean | Whole CPU p95 | Whole CPU p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3d12 / wet | 17.425 -> 17.241 | 17.925 -> 17.610 | 13.742 -> 12.755 | 28.064 -> 28.699 | 30.714 -> 32.044 |
| d3d12 / dry | 11.882 -> 12.442 | 12.624 -> 12.858 | 9.129 -> 9.344 | 21.936 -> 20.984 | 34.591 -> 22.501 |
| vulkan / wet | 18.359 -> 18.349 | 18.175 -> 18.203 | 5.822 -> 5.956 | 19.093 -> 19.116 | 20.339 -> 19.577 |
| vulkan / dry | 13.554 -> 13.573 | 13.388 -> 13.391 | 3.975 -> 4.012 | 14.128 -> 14.146 | 15.976 -> 14.834 |

D3D12 wet active CPU falls 13.742 -> 12.755 ms (about 7.2%), and whole CPU
17.925 -> 17.610 ms. Its whole-CPU p95 worsens 28.064 -> 28.699 ms and
p99 worsens 30.714 -> 32.044 ms. Wet GPU changes only 17.425 -> 17.241 ms.

Vulkan wet GPU is essentially flat 18.359 -> 18.349 ms; active CPU rises
5.822 -> 5.956 ms. Dry Vulkan GPU is flat 13.554 -> 13.573 ms. The single
D3D12 dry AB control worsens GPU 11.882 -> 12.442 ms and active CPU
9.129 -> 9.344 ms, while its CPU p99 improves 34.591 -> 22.501 ms.
These mixed controls do not establish a general speedup. Both wet GPU means
remain above 16.67 ms, and CPU tails still need work. The accepted benefit of
this pass is the connected native recording boundary, with unchanged visual
work; native parallel scheduling remains the next architectural step.

The original D3D12 dry timing image has a localized max 30 pump-edge difference
(35 pixels above 4). Two repeat runs show the same max 30 at the same position
between two unchanged-baseline runs; the fresh baseline/candidate pair is
within 3 everywhere. This is existing run-to-run variation, not evidence of a
new packet regression. Both repeat sheets/crops were inspected. The original
timing runs remain authoritative; these two runs only investigate image
stability. D3D12 wet timing pairs max 4/6 and Vulkan pairs at most 1 were reviewed.


The full means, medians, tails, phase breakdowns and image measurements remain
in analysis-summary.json and analysis-matched.json. Timing images were reviewed
separately; see timing-visual-review.json. The 16.67 ms whole-frame gate remains
open. This packet/context boundary prepares native parallel scheduling; it
does not itself dispatch native recording jobs.


## Matrix checkpoint

Shared view preparation and draw submission remains **65-75% estimated technique
coverage**. This step closes its native packet/context boundary; it does not
justify marking multi-list scheduling complete or raising an FPS/visual parity
claim. Other matrix rows are unchanged. See the game rendering roadmap.

Worker evidence: WORKER_EVIDENCE_ROOT/phlosion-command-recording-20261029.
Controller evidence: artifacts/lighting-foundation/command-recording-20261029.
