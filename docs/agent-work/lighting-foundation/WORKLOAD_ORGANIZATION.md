# Rendering workload organization: light participation and scheduled shadows/probes

2026-10-31. **100% estimated coverage of the fixed W1-W8 workload-organization
contract**, verified for the supported normal Windows D3D12/Vulkan raster path.
The contract was recorded before implementation. This closes this category's
named light-participation, shadow scheduling and placed-probe scheduling gaps;
it does not claim 100% of Cyberpunk 2077's renderer or retail frame-time identity.

## Fixed acceptance and evidence

| Contract | Implementation and verification |
| --- | --- |
| W1 | WorldEnvironmentLighting, WorldEnvironmentPacketCache, world_local_direct, clustered/forward/probe/haze consumers; all 256 receiver channels share one packed environment; independent diffuse/specular/channel/consumer oracle images are exact on both APIs. |
| W2 | Existing qualified shared surfaces, material classification, opaque primary/reflected composition and P1-P8 submission remain normal. Native surface/cluster/transparent controls and full runtime/editor profiles retain required paths without a fresh screen-reflection source draw. |
| W3 | WorldShadowUpdateCache, stable authored light identities, glTF/foliage visibility-based revisions and native actor resource/pose/transform revisions. Same-path frozen-pose model replacement is independently tested; settled scheduled/fresh shadows are pixel-exact for all twelve cases on each API. |
| W4 | WorldShadowSchedule persistent FIFO, admitted/reused/deferred/unready counters and per-tier refresh budgets. CPU starvation/reinvalidated-queue tests and GPU owner/reorder/reset/failure fixtures verify reuse and correctness. |
| W5 | WorldProbeResidency four-slot LRU with requested-set pinning, duplicate rejection, scene/placement invalidation and prewarm/age/distance selection. Native eviction, relocation and stable-capture tests verify retained materials. |
| W6 | WorldPlacedProbeCache separates six relight faces, ten filtering jobs and complete-generation publication/blend. Partial/canceled generation controls, current wet/emission controls, tier transitions and paused-clock tests pass both APIs. |
| W7 | Both ordinary Windows APIs implement the scheduling interfaces. Fenced resource ownership, backend reset, collection lifetime, native view retirement and Vulkan submission tests pass with validation; runtime/editor expose the counters. |
| W8 | 21 CPU groups, private foliage arithmetic, 120 workload-native plus 13 existing native checks, 18 runtime/editor scene pairs and two four-afterimage pairs pass. Visual review and optimized matched whole-frame cost are recorded without lowering image thresholds or inferring retail FPS. |

## Resulting normal behavior

Lights retain separate diffuse, specular, opaque, transparent, probe, fog,
particle and shadow participation and eight-bit source/receiver channels.
Diffuse-probe participation is independently controlled. The view environment is
packed once; per-object receiver channels patch the prepared metadata without
repacking all lights. Full object identity and receiver channels remain distinct.
Supported specialized and transparent materials keep their required shading.

Local lights retain stable ownership across array reordering. Each independently
fenced shadow atlas has its own persistent refresh queue. Static and dynamic
caster revisions are independent. Unchanged completed maps are reused; dirty
lights enter a persistent fair queue, and valid previous results remain available
until refreshed. Wrong-owner, wrong-projection and uninitialized maps are never
sampled. Low/Medium/High/Ultra refresh at most 2/4/6/8 local maps per frame.

Placed probes use four bounded resident slots, pin the requested set before
eviction, and prioritize cold probes, then oldest completion, then distance.
Geometry/material captures survive illumination changes. A relocated probe
invalidates its capture even when the scene revision is unchanged. Warm updates
relight 1/3/3/6 faces and perform 1/2/2/4 filter jobs per frame by tier; cold
prewarm completes one probe with six faces and ten jobs. A generation is published
only after all filtering finishes and blends with the previous complete result.
Neither bank is overwritten while needed for the blend. Canceled work cannot
publish. Updates and blending continue while the authored weather clock is paused.

Same-path native model hot reload now changes the collected resource revision.
A frozen pose therefore cannot leave stale local shadows. Model ABI26, editor
ABI47 and battle VFX ABI19 require a consistent rebuilt module set.

## Source correspondence and explicit adapters

The read-only supplied renderShadowManager.cpp snapshot has SHA-256
23f8b470d041525be6411378d7c9b97760f12893c1aae3b528cfc92ffbb745a1.
The trace connects independent dirty state, stable slots, persistent queues,
spatial invalidation and static/dynamic write/filter stages.
renderReflectionProbeManager.cpp connects eligibility/residency, no duplicate
updates, prewarm/age/distance priority, current-face relighting, mip/convolution
jobs and complete old/new generation blending. Clustered light collection and
consumer flags were traced with their callers. Exact hashes and line anchors
remain in source-audit.json; independent Phlosion code implements these contracts.

Existing public capacity remains four point plus sixteen area lights, eight
local shadow tiles and four resident probes. This pass does not introduce every
retail light/material type or world streaming. Native actors conservatively
invalidate affected scheduling inputs for all local maps; glTF and foliage use
their existing per-light conservative visibility. This is correct but not a claim
of identical retail culling efficiency. D3D12 retains one completed atlas and
Vulkan retains fenced per-frame storage. Buffer layouts, handles and API adapters
differ from the reference. The snapshot is not authenticated as current retail.

The shared surface identity plane grows from R32_UINT to R32G32_UINT, preserving
full object IDs plus channels. This costs four bytes per retained surface pixel
(7.91 MiB at 1920x1080). Probe banks retain their existing allocation. Established
FiniteLine lighting, quality sample counts, Hybrid material-specific rain impacts
and puddle rings are preserved; scheduled refresh cadence intentionally changes.

## Verification and failures fixed

21 CPU groups pass (engine 14, ModelSupport 1, game 6), plus the private foliage
arithmetic test. 120 new workload-native runs and 13 existing native checks pass
with D3D12 validation and the Vulkan Khronos layer confirmed. All 36 external
native oracle comparisons are pixel-exact, including scheduled versus freshly
rebuilt local shadows. Partial probe publication and retained/fresh relighting
also retain their internal image gates.

The relocation fixture initially exposed an ignored release during an active
frame: an old material capture survived at a new placement. Release now invalidates
idle surface metadata without destroying live GPU storage or waiting globally;
queue ordering preserves safe reuse. The fixture now observes the required
recapture. A continuous-refresh native regression then exposed queue starvation across
the Vulkan two fenced atlas copies. A shared queue could repeatedly select lights
for the same copy while starving their other copy. Queues now belong to each
independent bank; the alternating-bank CPU test services every light equally and
both native APIs match fresh shadows exactly under continuous refresh. Final
checks were rerun after this fix. The full game also exposed battle prewarm
attempting shadow scheduling outside an active frame. Prewarm now skips both
shadow admission and scheduling; the backend safety guard remains enabled.
Game CPU checks and all scene captures were repeated after that correction. The capture harness also now stages shaders at both verified runtime and editor
compiled paths; the initial Vulkan editor baseline used mismatched compute shaders
and was replaced by a correctly staged comparison. Failed and superseded evidence
is preserved, not counted as passing.
A performance-memory audit also found that variable probe face counts renumbered
later transient scene surfaces and reflection snapshots, retaining redundant
full-size allocations. Their per-kind allocation sequence is now independent
of retained probe views. The total view limit, fenced ownership and retained
probe keys remain unchanged. Full native/scene qualification and matched timings
were repeated after the correction.


18 matched runtime/editor field, wet, orbit, battle and preset pairs plus two
four-afterimage pairs cover the normal producers and required profile stages.
No fresh screen-reflection source redraw is accepted. Per-tier shadow budgets
and zero unready mappings are checked. These are not all pixel-identical images:

- d3d12: 0/9 exact pairs; maximum 7/255; largest mean channel difference 0.009972/255.
- vulkan: 0/9 exact pairs; maximum 8/255; largest mean channel difference 0.023207/255.

All six final scene contact sheets and the full-resolution Vulkan wet capture/difference were visually reviewed, plus the two-API four-afterimage sheet. No visible loss of station lights, shadow silhouettes, scene geometry, foliage, rain, wet reflections or battle actors was found. The earlier Vulkan shadow-bank starvation discrepancy is resolved in the final captures. Remaining image differences are small recorded refresh-cadence/rounding changes; the original strict one-level scene flag remains false, not relabeled as pixel equality. Every final scene obeys its shadow refresh budget and reports zero unready mappings. Both afterimage cases retain four records and eight draws; maximum difference is 2/255 with only 4 D3D12 and 10 Vulkan pixels over one level. The 36 independent native oracle comparisons remain pixel-exact.

## Matched optimized cost

Native 1920x1080 Medium, night/overcast, RelWithDebInfo, EVGA GPU1; validation off.
Wet and battle ABBA provide sixteen primary runs, each with 140 steady frames.
Four separate diagnostic runs retain phase costs. Paired settings, rain state,
thermal flags and native errors are checked.

| API / scene | GPU mean ms | CPU frame mean ms | Active CPU mean ms |
| --- | ---: | ---: | ---: |
| d3d12 / wet | 16.614 -> 15.780 | 16.610 -> 15.830 | 10.333 -> 9.078 |
| d3d12 / battle | 17.076 -> 16.401 | 19.204 -> 18.163 | 16.713 -> 15.467 |
| vulkan / wet | 18.392 -> 17.506 | 18.227 -> 17.317 | 4.729 -> 4.837 |
| vulkan / battle | 18.127 -> 17.157 | 18.100 -> 17.133 | 9.612 -> 9.640 |

All sixteen primary runs are retained and accepted: paired settings and rain state match, with no thermal-throttling flags or native errors. Starts were 42-45 C and observed peak temperature was 64 C. GPU boost clocks were not locked and telemetry is coarse. D3D12 wet baseline means varied from 15.827 to 17.402 ms and candidate means from 15.525 to 16.036 ms; its pooled result is indicative, not a guaranteed fixed-clock speedup. Both ordered battle pairs and both Vulkan wet pairs improved.

D3D12 battle GPU mean falls 17.076 -> 16.401 ms and CPU frame mean 19.204 -> 18.163 ms. Vulkan battle GPU mean falls 18.127 -> 17.157 ms and CPU frame mean 18.100 -> 17.133 ms. D3D12 active CPU falls about 1.25 ms; Vulkan active CPU changes by approximately +0.03 ms in battle and +0.11 ms in wet field. Candidate GPU p95 is still 17.44-18.46 ms across the measured cases. Sustained 16.67 ms acceptance is not established.

The final allocation fix removes the preliminary memory regression. Reported peak device memory is 1679 -> 1635 MiB for D3D12 wet field and 1781-1782 -> 1736 MiB for battle; Vulkan is 1924-1936 -> 1850-1851 MiB wet and 2025 -> 1961 MiB battle. The intermediate Vulkan candidate used 2241 MiB in the same validated wet scene; stable transient surface/snapshot allocation reduces it to 1850 MiB. This is measured device memory, not a per-allocation accounting claim.

All eight final timing image pairs were reviewed. Lighting, geometry, actors, rain and reflections remain present. The largest timing-image difference is 19/255 at two pixels in each D3D12 wet pair; Vulkan wet peaks at 13/255, and battle pairs peak at 1-2/255. These are recorded scheduling/history differences, not claims of pixel identity. Rain state is identical in every pair.

One original Vulkan baseline diagnostic had a software thermal-throttling flag and was rejected; its raw evidence is retained. The replacement diagnostic passes the unchanged gate. All four accepted diagnostic profiles show fewer view batches (11 -> 9) and draw packets, with reduced shadow CPU cost on both APIs. None of the sixteen primary measurements was excluded. These results compare Phlosion revisions on gpu-worker, not retail Cyberpunk 2077.

These compare Phlosion builds on the same worker, not measured retail Cyberpunk
performance. Whole-frame and active CPU cost are reported together; moving work
between phases alone is not a gain. Sustained 16.67 ms, Linux qualification,
broader material/temporal coverage, indirect lighting, volumes, terrain and
weather remain separate open categories. No unrelated matrix row is rescored.

A cold full-game D3D12 launch took approximately thirteen minutes during this
pass. Sampling its hot thread found 19/20 instruction addresses in D3DCOMPILER_47,
confirming shader compilation as the dominant observed startup activity. It
completed; warm captures subsequently use the cache. Compiler optimization was
not reduced. No paired cold-start baseline establishes whether this is a new
regression. The first full Vulkan launch was also sampled: 20/20 hot-thread addresses were in the NVIDIA GPU compiler module. Cold preparation/loading remains a separate unresolved category.

Evidence: artifacts/lighting-foundation/workload-scheduling-20261030.
The game roadmap records strict dependency and final payload identity checks.
