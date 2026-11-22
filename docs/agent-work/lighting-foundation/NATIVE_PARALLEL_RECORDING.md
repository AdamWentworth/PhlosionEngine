# Native parallel command recording

2026-10-29. All implementation, builds and tests run on gpu-worker; controller owns
review, integration and canonical commits.

## Connected implementation

Normal indexed batches now feed two recording lanes (the render thread and a persistent helper) on both
backends. Preparation stays on the render thread: uploads, material/PSO lookup,
descriptors, object/skin/instance inputs and resource publication are complete
before jobs read immutable native packets. This is the ordinary renderer, with
no opt-in and no quality-budget or shader change.

D3D12 gives each recording lane a bundle allocator for each in-flight frame. Each range
gets its own bundle, with independent root/pipeline/geometry bindings. Viewport,
scissor, target state and barriers stay on the primary list; bundles inherit the
same descriptor heap and viewport/scissor. A changed heap/root/region flushes the
current group. Outline then surface and authored transparent order are preserved.

Vulkan gives each recording lane a command pool for each in-flight frame. A third,
render-thread pool records immediate segments. Each original render pass uses
secondary contents: preceding immediate commands execute first, followed by
worker ranges in authored order, then subsequent immediate work. The pass stays
open; no extra attachment stores/loads, render passes or memory barriers are
introduced. Primary transfer/compute work and existing pass dependencies stay
ordered. Post-process constants are explicitly bound in their consuming segment.
Every segment starts with independent binding state; secondary execution cannot
leak cached bindings into the next segment.

A batch holds at most 256 packets. Groups below eight packets remain serial;
each frame has at most 512 worker lists per lane, then uses the serial recorder.
Nested batch scopes flush at the outer boundary, with flushes at pass, immediate
draw and resource-scope boundaries. The caller records one range and can atomically claim an unstarted helper range.
Generation-tagged claims prevent stale wakeups from running a later job twice.
Short waits spin for at most 100 microseconds, then park; the idle helper also
parks. Both lanes are joined even on job failure. Diagnostic fields are assembled
into one line before a single output write. The initial measurements were distorted
by field-by-field writes through redirected diagnostic pipes: an isolated 300-record
ABBA check measured about 3.0 ms per multiwrite record versus 0.12 ms for one write.
Earlier scheduling variants and their timings remain in the receipts, but their
unbuffered whole-frame numbers are not accepted as clean performance evidence.
CPU packet inputs may be released after recording; native command storage,
upload slices, descriptor contents and mesh resources survive through existing
frame fences. Command pools/allocators reset only after their owning frame's
fence. Resource-scope retirement and shutdown retain their GPU completion waits.

The recording pool has no backend policy: each lane consumes its packet range
and its private command storage. CPU recording overlaps when the helper claims a range; GPU execution remains
the intentionally ordered graphics stream. No claim of concurrent GPU passes
or complete engine/source parity follows from this change.

API constraints: [D3D12 command/bundle restrictions](https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles)
and [Vulkan render-pass/secondary command semantics](https://docs.vulkan.org/spec/latest/chapters/renderpass.html).
The read-only source-traced preparation/collection mechanisms are documented in
[NATIVE_COMMAND_PREPARATION.md](NATIVE_COMMAND_PREPARATION.md), including its
trace receipt. This is independent Phlosion implementation. The supplied source
snapshot is not authenticated as current retail.

## Qualification and measured cost

Optimized MSVC RelWithDebInfo builds pass for the editor, runtime and required
game/model modules. Six CPU groups pass, including caller/helper identities, exactly-once claims,
real interval overlap, deterministic merge order and join/recovery after a job
throws. Thirty native validation runs pass with 22 exact image pairs. Two further
native runs compare the serial and parallel images exactly while exercising
overlapping transparent draws with changing colour, an immediate draw between
packet groups, nested scopes, the 256-packet spill, frame-fence reuse and resize. Three further checks exercise the native indirect
material path on each API and Vulkan editor/HDR/sprite submission, resize,
vsync changes, presentation-fence reuse and repeated shutdown.

Twelve fresh runtime/editor scene pairs cover wet/dry/orbit/battle on both Windows
APIs. Recording telemetry proves overlapping native jobs in all twelve normal
paths: 16110 parallel batches,
16103 with overlapping worker intervals,
and 629390 packets. Telemetry includes warmup; it is
mechanism evidence, not a speedup calculation.

The full native/scene suite qualified the participating scheduler. The final
change only buffered diagnostic serialization, with no change to drawing or
scheduling. A source-hash/diff audit proves that scope; all six CPU groups and
five additional native checks were repeated afterward. The final twelve matched
timing runs also provide fresh wet/dry images on both APIs.

All 61 shader/material payloads are byte-identical to the accepted baseline.
Full-scene and timing images were inspected; their measured differences and
review decisions are retained in the evidence. No lighting/rain/weather, splash,
puddle, foliage, asset or quality-setting changes were made.

Matched timings use worker EVGA RTX 2060 SUPER GPU 1, native 1080p Medium,
fixed night/overcast, validation off, phase profiling on, 220 frames with
40..179 analysed. Wet ABBA and dry AB give twelve runs. All runs are retained;
maximum GPU temperature is 64 C. This is an optimized
build comparison on the same GPU, not a retail Cyberpunk benchmark.

| API / scene | GPU mean | Whole CPU mean | Active CPU mean | Whole CPU p95 | Whole CPU p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3d12 / wet | 17.428 -> 17.710 | 17.856 -> 18.131 | 13.375 -> 13.669 | 28.498 -> 28.684 | 30.970 -> 29.817 |
| d3d12 / dry | 12.156 -> 12.408 | 12.725 -> 13.028 | 8.981 -> 9.616 | 20.993 -> 21.080 | 26.245 -> 22.458 |
| vulkan / wet | 18.346 -> 18.412 | 18.221 -> 18.226 | 5.622 -> 6.394 | 19.106 -> 19.494 | 20.030 -> 20.245 |
| vulkan / dry | 13.577 -> 13.665 | 13.397 -> 13.475 | 3.991 -> 4.705 | 14.198 -> 14.850 | 14.478 -> 16.574 |

The final buffered-report comparison does **not establish a speedup**. Wet
D3D12 GPU/whole-CPU means rise 17.428 -> 17.710 ms / 17.856 -> 18.131 ms;
active CPU rises 13.375 -> 13.669 ms. Its whole-CPU p95 rises slightly
28.498 -> 28.684 ms, while p99 falls 30.970 -> 29.817 ms.

Wet Vulkan GPU is nearly flat, 18.346 -> 18.412 ms, and whole CPU
18.221 -> 18.226 ms. Active CPU rises 5.622 -> 6.394 ms; whole-CPU
p95/p99 rise 19.106 -> 19.494 / 20.030 -> 20.245 ms. The single dry AB
pairs also carry a cost: D3D12 GPU 12.156 -> 12.408 ms and whole CPU
12.725 -> 13.028 ms; Vulkan GPU 13.577 -> 13.665 ms and whole CPU
13.397 -> 13.475 ms. Dry Vulkan p99 worsens 14.478 -> 16.574 ms.
These costs remain visible in the acceptance record.

Acceptance follows the user's explicit allowance for foundational normal-path
changes without immediate performance gains: this completes actual private native
recording, ordered execution and fence-safe reuse, while leaving visual work
unchanged. It does not close the performance gate. Both wet GPU means exceed
16.67 ms; remaining producer/preparation work and GPU costs still need attention.
Small groups already use the serial adapter; broader preparation must make
better use of the new recording boundary rather than adding more worker waits.

A final D3D12 wet image has a localized max30 pump-edge difference. The unchanged
baseline's two ABBA runs reproduce max30 at that same coordinate, while the two
candidate runs differ by at most1. The other matched D3D wet pair is max8;
D3D dry and all Vulkan timing pairs are within1. Sheets/crops and the within-build
controls were inspected. No timing run was excluded or substituted. Earlier
field-by-field logging measurements are preserved but superseded by this final
buffered-report series.

## Coverage and next work

Shared view preparation and draw submission moves from 65-75% to **75-85%
estimated technique coverage**. This stage completes private native storage,
actual parallel recording, ordered pass execution and frame-fence reuse for
the prepared indexed path. Other producers' CPU preparation, game-level
shadow/probe revision traversal, broader material/view preparation and scheduling
remain. C7/C8, remaining GPU shader costs, Linux qualification and the 16.67 ms
whole-frame target remain open. Other parity-matrix categories are unchanged.

Next: use the new recording/preparation measurements to carry retained shared
work upstream into the remaining capture/probe/shadow preparation and producers.
Do not substitute quality cuts for those missing connected mechanisms.

Evidence: artifacts/lighting-foundation/parallel-recording-20261029; complete
worker run records: WORKER_EVIDENCE_ROOT/phlosion-parallel-recording-20261029.
Raw original controls and all comparisons are retained.
