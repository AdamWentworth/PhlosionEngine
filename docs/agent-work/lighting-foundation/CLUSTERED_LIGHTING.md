# Connected clustered local lighting

## 2026-10-28 material-reuse follow-up

Standard opaque composition now reads the existing evaluated surface planes on
both Windows APIs, including the actual game-composed shaders. Consumer isolation
and full-scene checks pass; see [material reuse and costs](OPAQUE_REUSE_PERFORMANCE.md).
This supersedes the standard-PBR material-reuse gap in the historical checkpoint
below. Specialized reuse, geometry/coverage replay, source feature/channel
coverage, C8 scheduling and Linux remain open.

2026-10-27. This is the ordinary standard-PBR local-light path in the
PokemonCyberpunk field and battle views, on Windows D3D12 and Vulkan. No quality
switch enables it. Specialized/transparent materials retain their required
forward paths. This connects the primary C7 stages; it does not complete C1-C8,
all source material/light variants, Linux qualification or the 16.67 ms goal.

## Source-to-implementation map

The privately supplied snapshot is not authenticated as the current retail
revision. The independent implementation follows the recorded call chain in
Cyberpunk2077Research/lighting/reflections/SOURCE_RENDER_CALL_PATH.md:

| Source anchor | Phlosion producer and consumer | Verification |
| --- | --- | --- |
| clusteredDeferred.cpp:156/194/629, collection and cascaded spatial culling | WorldClusteredLighting packs the current four point/sixteen area sources; clustered_common and clustered_entry build coarse/fine GPU masks. | GPU list readback checked against an independent double-precision full-list frustum/AABB oracle, including camera/light motion and orthographic projection. |
| renderNode_Lighting.cpp:1164, material tile lists | Shared evaluated albedo/normal/material/depth/identity feed two standard-PBR material classes; GPU append lists and indirect arguments. | Actual list uniqueness, bounds, occupancy, masked/unsupported coverage and resize checks. |
| renderNode_Lighting.cpp:224/330; lights_compute.fx:270 | Indirect compute dispatch visits each class tile and only its spatial light list. D3D12/Vulkan own resources, barriers, frame/view lifetime and profiling. | Native fixtures and ordinary runtime/editor captures expose cull, classify and shade phases. |
| include_lightingHelpers.fx:465/820 | Existing qualified indexed point/FiniteLine/LTC components and local shadow visibility. | Existing 4,096-case component oracle per API; native shadow fixtures through the new dispatch. |
| lights_compute.fx:431; lightIntegrate.fx:96/97/157 | Separate diffuse/specular outputs are consumed by engine built-in and composed PBR templates, including Vulkan indexed-material shaders. | A consumer fixture removes every forward local-light input after compute; the rendered image must still equal the clustered reference. Forward/clustered reference images remain within 1/255. |

No source-snapshot code or assets were added. Sphere/AABB culling, grid dimensions,
packing, participation tags, derivative handling and material classes are Phlosion
adapters. This does not prove every source flag, authored fixture branch or retail
lighting parameter.

## Data and execution

The existing surface pass evaluates mapped normals and wet material state before
local lighting. Standard opaque/masked surfaces participate; specialized-forward,
motion-only, transparent and empty pixels do not. Dielectric and metallic/mixed
tiles have separate indirect work lists. Their current BRDF core is shared; this
is not the source's full material-feature permutation set.

A 16x16-pixel, 32-depth-slice grid uses logarithmic depth boundaries and a
four-times-coarser parent grid. Each cluster's 20-bit mask represents the entire
current public light capacity without truncation. Point and LTC bounds retain
their current range semantics. Finite source bounds include source extents and a
0.1% conservative pad for the recovered reciprocal-root approximation. Increasing
the public capacity requires expanding this representation; it must not silently
discard sources.

The two full-size RGBA16F outputs hold **material-weighted outgoing linear
radiance**, matching the indexed evaluator contract. Consumers add diffuse and
specular once; they do not multiply material factors again. Probe bounce, sunlight,
reflections, haze and presentation remain separate. Alpha on diffuse marks valid
standard-PBR output. View projection and depth reject unrelated/overdrawn pixels;
unsupported coverage retains its established forward response. The captured,
depth-tested opaque replay selects a composition pipeline with the old per-pixel
standard forward fallback compiled out. Its valid pixels come from this same
surface capture; hidden overdraw does not re-evaluate local lights. Auxiliary
captures, transparent/depth-disabled draws and specialized shading keep their
general paths. Vulkan includes the indexed-material route. Empty spatial light
lists return zero before normal/shadow/BRDF work.

PCF uses explicit receiver-quad position/normal differences in compute. At missing
or different-object neighbors, the adapter extends the receiver plane instead of
borrowing another object's depth. Moment shadows sample each API's actual final
filtered atlas. These adapters retain the native border/self/range-edge tests.

Resources are owned per frame in flight and surface slot. Resize waits for safe
replacement; new/cleared surfaces invalidate the consumer; renderer teardown
releases the resources. Ordinary rendering performs no diagnostic readback.

At 1920x1080, the diffuse/specular pair is 31.64 MiB per frame/view and cluster/list
storage is about 1.08 MiB. This is additional to the existing shared surfaces and
histories. Material textures are still evaluated during final composition: this
change moves local-light evaluation, not all geometry/material work, into compute.

## Qualification

Optimized RelWithDebInfo builds on gpu-worker Windows, EVGA RTX 2060 SUPER
GPU_1_UUID, LUID 000000000000be8e.
Controller source baselines: engine 5f643a8, game 11dad60, ModelSupport da6a969.
The controller performed orchestration/review only.

- 36 native clustered/shadow cases across both APIs: lists, forward comparison,
  consumer isolation, FiniteLine/LTC, metals, masking, unsupported classes,
  disabled environment, resize, orthographic projection and moving camera/light;
  empty/blocked/PCF/border/self/range-edge/clear shadows.
- Six additional native runs load the game's actual composed material profile.
  Built-in and composed consumer isolation images match exactly on both APIs.
  Clustered versus forward maximum error is 1/255; mean error is 0.01875/255
  on D3D12 and 0.015573/255 on Vulkan.
- Existing direct-component/finite fixtures, rectangle/local-shadow CPU contracts
  and ten mapped/material/masked/unsupported/extent surface cases pass.
- Twelve runtime/editor wet, dry, camera-orbit and battle scenarios pass on
  D3D12/Vulkan with no validation errors. The r8 composition optimization matches
  eleven r7 scene images exactly; Vulkan dry daylight differs by at most 1/255 per channel (mean 0.000344/255). Existing D3D12 warning 1328 remains a warning.
- Eleven final registered tests pass: three CPU contracts and eight native
  forward/consumer-reference checks, including the actual Vulkan indexed-material
  path. Six additional Low/High/Ultra runs verify the same clustered stages and
  retained reflection tier behavior. Medium is covered by the full matrix.
- Relative to committed forward lighting, compact evaluated-surface precision
  produces small differences. D3D12 wet mean absolute channel error is 0.024836/255;
  maximum 90 occurs at isolated pixels. Only 0.0162% of wet pixels differ by more
  than 1/255 and 0.000386% by more than 32/255. Scene/difference images are retained;
  do not describe this as bit-identical to the original forward renderer.

Qualification caught two integration defects before acceptance: Vulkan initially
selected an intermediate moment atlas, and the game's composed PBR templates
initially retained the forward loop despite dispatching compute. Both were fixed.
A permanent consumer-image fixture prevents an unused lighting pass from passing
as a connected implementation. Failed/stopped runs remain in task evidence.

A reused harness also initially staged into the prior task's mutable candidate
cache. That run was excluded; previous reports/images/hash manifests were
preserved, the misplaced cache was retired, and the exact committed baseline was
rebuilt in isolated worktrees for comparison. Baseline reconstruction is recorded
separately from original binary provenance.

## Remaining boundary

Broader source feature/channel records, specialized-material compute variants,
source-fixture mapping, full material-composition reuse and Linux qualification
remain open. C8 placed-probe scheduling and static/dynamic shadow refresh policy
are separate unfinished work. Do not label this complete engine parity.

Evidence: WORKER_EVIDENCE_ROOT/phlosion-clustered-dispatch-20261027 on gpu-worker, with reviewed
source delta and evidence returned to
PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/clustered-dispatch-20261027 on controller.

## Measured cost and acceptance

The first connected revision regressed D3D12 wet-night GPU median from about
31.9 to 35.1 ms. Sharing the bounce traversal reduced that to 33.16 ms, still
slower. Those revisions were rejected. r8 uses a dedicated opaque composition
variant instead of carrying the exhaustive standard fallback into that draw.
The smaller shader and lossless empty-list exit preserve the checked images.
This is renderer work organization, not a quality, resolution or rain reduction.

Matched optimized native-1920x1080 Medium runs use the same identified EVGA card.
Wet runs are baseline/candidate/candidate/baseline; dry has one pair. Statistics
cover 140 frames, frames 40-179, with validation and diagnostic readbacks disabled
during that interval. Each run begins at or below 45 C. Existing telemetry
flagged Vulkan shutdown after resources were released; the old r6/r7 trials stay
excluded. New runs use completion of a frame-185 screenshot as an explicit
conservative end marker after the measured interval. Every temperature/clock/
reason sample up to that marker must be free of software/hardware thermal
slowdown. Post-marker flags are retained separately, not silently discarded. A separate audit of all telemetry samples in all twelve accepted r8 runs confirms no thermal flags anywhere in those runs, including shutdown (thermal-audit-final.json).

The baseline was rebuilt from the exact committed engine/game revisions, in
isolated worktrees with the same configuration/dependencies. Both API baseline
validation images match the previous committed reference exactly. No worker
source change was copied to controller before qualification.

| API / scene / version / repeat | GPU mean | Median | p95 | p99 |
| --- | ---: | ---: | ---: | ---: |
| d3d12 / wet / baseline / 1 | 31.925 | 31.993 | 32.559 | 32.690 |
| d3d12 / wet / candidate / 1 | 28.145 | 28.425 | 29.430 | 29.578 |
| d3d12 / wet / candidate / 2 | 28.482 | 28.507 | 29.255 | 29.468 |
| d3d12 / wet / baseline / 2 | 31.612 | 31.892 | 32.670 | 32.961 |
| d3d12 / dry / baseline / 1 | 22.863 | 24.396 | 24.900 | 25.023 |
| d3d12 / dry / candidate / 1 | 19.899 | 20.909 | 21.318 | 21.497 |
| vulkan / wet / baseline / 1 | 33.369 | 33.567 | 34.151 | 34.279 |
| vulkan / wet / candidate / 1 | 30.894 | 30.945 | 31.386 | 31.535 |
| vulkan / wet / candidate / 2 | 30.444 | 30.938 | 31.487 | 31.641 |
| vulkan / wet / baseline / 2 | 33.444 | 33.678 | 34.321 | 34.491 |
| vulkan / dry / baseline / 1 | 26.020 | 26.154 | 26.484 | 26.609 |
| vulkan / dry / candidate / 1 | 23.130 | 23.144 | 23.481 | 23.528 |

All values are milliseconds. CPU frame/present distributions:

| Run | CPU mean / median / p95 / p99 | Present mean / median / p95 / p99 |
| --- | --- | --- |
| d3d12 / wet / baseline / 1 | 32.629 / 31.813 / 46.369 / 51.799 | 0.191 / 0.171 / 0.245 / 0.795 |
| d3d12 / wet / candidate / 1 | 29.770 / 28.525 / 43.358 / 46.328 | 0.204 / 0.194 / 0.290 / 0.380 |
| d3d12 / wet / candidate / 2 | 30.652 / 28.658 / 43.642 / 48.705 | 0.175 / 0.168 / 0.236 / 0.292 |
| d3d12 / wet / baseline / 2 | 32.889 / 31.470 / 49.540 / 56.362 | 0.175 / 0.169 / 0.224 / 0.253 |
| d3d12 / dry / baseline / 1 | 23.727 / 23.455 / 37.094 / 44.488 | 0.182 / 0.169 / 0.251 / 0.293 |
| d3d12 / dry / candidate / 1 | 20.561 / 18.900 / 32.264 / 37.931 | 0.199 / 0.179 / 0.266 / 0.335 |
| vulkan / wet / baseline / 1 | 35.040 / 33.272 / 49.806 / 55.334 | 0.053 / 0.049 / 0.076 / 0.092 |
| vulkan / wet / candidate / 1 | 30.763 / 30.742 / 31.957 / 32.293 | 0.072 / 0.067 / 0.106 / 0.122 |
| vulkan / wet / candidate / 2 | 30.425 / 30.686 / 31.907 / 32.323 | 0.073 / 0.068 / 0.103 / 0.139 |
| vulkan / wet / baseline / 2 | 35.032 / 33.606 / 49.446 / 55.746 | 0.133 / 0.049 / 0.081 / 0.111 |
| vulkan / dry / baseline / 1 | 25.864 / 25.935 / 27.126 / 32.941 | 0.064 / 0.062 / 0.100 / 0.125 |
| vulkan / dry / candidate / 1 | 23.164 / 22.975 / 30.401 / 35.639 | 0.067 / 0.063 / 0.093 / 0.125 |


See timing-r8.json for individual phase costs, temperature/clock telemetry and
post-marker flags. These are short matched worker samples, not retail-game FPS
parity or a claim for every scene. The native-1080p Medium 16.67 ms target remains
open. Dry daylight retains the previously documented distribution variability.

Next is C8: placed-probe selection/blending and scheduled refresh, plus correct
static/dynamic shadow invalidation and refresh budgets. Preserve response to
moving actors/lights, time of day, rain/wetness and field/battle view changes.
Full material-evaluation reuse, source feature/channel coverage and Linux remain
separate unfinished gates.
