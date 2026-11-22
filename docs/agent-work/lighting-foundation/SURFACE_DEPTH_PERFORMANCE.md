# Primary surface depth reuse - 2026-10-28

## Change

The normal field and battle renderer already captures opaque/masked depth for
shared surfaces, motion, clustered lighting and reflections. Contact/filter
scratch work clears the scene depth again before final forward composition.
The final geometry pass therefore began without that coverage in its depth
attachment. Standard shared-material composition already rejected occluded
samples inside the pixel shader, but still paid geometry/coverage and some
fragment work. Restoring depth permits earlier rejection; it does not remove
geometry replay or newly eliminate all hidden-surface lighting.

D3D12 now restores that captured depth after scratch work and sky, before final
geometry, using the existing depth-only fullscreen publication. The engine
exposes a backend scheduling preference; the game follows it in field and battle.
Vulkan retains its previous normal path: testing a depth copy found no reliable
saving. Explicit Vulkan restoration is implemented and qualified for callers
that need its depth contract, but the normal game does not request it.
The explicit restore implementations preserve color, sampled shared-surface
data and the previous scratch descriptor. Matching view/extent, active scene and completed surface state are
required. Active shadow, haze, probe and reflection captures are rejected.
This is a current-view caller contract, not a new probe-selection API.

No new depth allocation or shader variant is introduced. Geometry submission,
vertex processing and the separate planar reflection pass remain. This reduces
hidden-fragment work; it does not complete fullscreen opaque composition or C7/C8.
The planar pass still provides visible puddle reflections and is retained.

Lighting, contact-search quality, temporal reconstruction, probe refresh cadence,
rain density, surface-specific Hybrid impacts and puddle rings are unchanged.
There is no user opt-in or quality-budget reduction.

The connected depth/surface -> lighting/reflections -> forward composition order
is consistent with the existing SOURCE_RENDER_CALL_PATH trace referenced by
COMPLETE_TECHNIQUE.md. This is an independent Phlosion resource-lifetime adapter,
not a claim of identical retail Cyberpunk 2077 behavior or performance.

## Verification

gpu-worker Windows, optimized RelWithDebInfo, EVGA RTX 2060 SUPER,
LUID 000000000000be8e. Controller controller only orchestrated, reviewed and integrated.

All 16 native cases pass with D3D12/Vulkan validation: opaque, cutout, transparent,
enrolled forward-only, moving camera/object, odd resize, occlusion and cleared
state. The 14 ordinary before/after image pairs match exactly. Final native checks also
assert the selected backend scheduling preference. The two deliberate
occlusion cases prove hidden surfaces are rejected while uncovered pixels and
existing scene color survive. Wrong camera/extent, active capture and prior/cleared
scene depth are rejected. No comparison tolerance was loosened.

The initial fixture failure was in the reference setup: its solid background
wrote depth. Clearing that scratch depth before both variants corrected the
fixture; the renderer was unchanged. That failed attempt and correction remain
in the evidence. Four CPU CTest entries pass: engine, temporal scene, contact
visibility and game exploration contracts.

Twelve final validation-enabled scenes pass: wet/dry/orbit/battle runtime and
wet/battle editor on both APIs. Expected battle state, surface/motion classes,
temporal reflection coverage and clustered/probe work are present. D3D12
publishes primary depth; Vulkan does not. No renderer validation errors.

Eight final scene images match the accepted references exactly. Three differ by
at most one byte; the D3D12 daylight image differs by at most three bytes, with
141 pixels exceeding one byte in a 1920x1080 image. Later frame-185 final timing
images are not universally exact either: D3D12 wet/dry maxima are 6/3 bytes
(7/0 pixels over four), while both Vulkan images stay within one byte. Mean
differences are below 0.008 byte/channel. Full-image pairs and magnified worst
crops were reviewed; no broad lighting, rain, geometry or silhouette change was
visible. The cause of every sparse outlying pixel was not isolated. These
qualification limits accompany the preserved-quality claim; no native threshold
was relaxed.

## Matched warm performance

Baseline engine 292ca96ff48e670b04c3463d57e9d9956c6fc6d4 and game
6cc0b768173b5b6e90b0d99cb2c23af2a6c84b23. Frozen payload and tested-source hashes
are retained. Native 1920x1080 Medium, wet night/Hybrid heavy rain and dry clear
noon. Wet order is baseline/candidate/candidate/baseline per API, 280 measured
frames per version. Dry pairs supply 140 each. Frames 40-179 of 220 are measured;
validation/readbacks are disabled, profiling enabled. Startup, compilation and
screenshot work are excluded.

Initial matched campaign, baseline -> depth reuse on both APIs. Vulkan candidate
results below were rejected for the normal game path. All times are milliseconds.

| API / scene | GPU median | GPU mean | GPU p95 | GPU p99 |
| --- | --- | --- | --- | --- |
| d3d12 / wet | 23.128 -> 22.928 | 23.190 -> 22.870 | 24.749 -> 24.496 | 25.088 -> 24.782 |
| vulkan / wet | 24.979 -> 25.117 | 25.156 -> 25.335 | 26.386 -> 26.519 | 26.535 -> 26.698 |
| d3d12 / dry | 15.117 -> 14.970 | 15.200 -> 15.332 | 16.871 -> 16.663 | 17.014 -> 16.773 |
| vulkan / dry | 17.773 -> 17.792 | 17.823 -> 17.846 | 18.814 -> 18.848 | 18.936 -> 18.909 |

Mean primary geometry and depth-publication costs in the wet scene:

| API / scope | Baseline | Candidate |
| --- | --- | --- |
| d3d12 / geometry | 4.450 | 4.048 |
| d3d12 / surface-depth-reuse | 0.000 | 0.055 |
| vulkan / geometry | 4.666 | 4.770 |
| vulkan / surface-depth-reuse | 0.000 | 0.039 |

The selected GPU reached 65 C. All runs began at or below
45 C. No thermal flags occurred through frame-185 screenshot completion.
No invalid/dropped profile frames or renderer errors. Every measured frame
retains three probe relights, no material recapture and no removed screen-source
redraw. D3D12 candidates have one primary depth publication; final Vulkan has none.
CPU/present distributions and complete per-pass statistics remain in analysis.json.

Final backend-policy control runs (same 140 measured frames each):

| API / scene | GPU median | GPU mean | GPU p95 | GPU p99 |
| --- | --- | --- | --- | --- |
| d3d12 / dry | 14.896 | 14.894 | 16.703 | 16.845 |
| d3d12 / wet | 22.929 | 22.993 | 24.585 | 24.895 |
| vulkan / dry | 17.764 | 17.832 | 18.834 | 18.970 |
| vulkan / wet | 25.021 | 25.132 | 26.514 | 26.612 |

The repeated D3D12 campaign establishes a modest gain. Its final policy control
exercises the same depth publication. Vulkan copy timings did not establish a
benefit, so the final policy preserves its previous frame path. The final controls
are verification runs, not a second repeated baseline/candidate campaign.
The dry D3D12 initial pair has a lower median but a higher mean; do not describe
that pair as an unqualified throughput improvement.

These are instrumented warm GPU timings, not shipping FPS or matched retail
Cyberpunk 2077 benchmarks. Linux qualification, cold shader preparation, complete
C7/C8 scheduling/composition and the wet-scene 16.67 ms target remain open.
Technique-coverage percentages do not increase merely from this optimization.
The next substantial geometry target is classified fullscreen composition from
the existing surface buffers, retaining specialized/transparent forward paths.
The separate planar draw must retain its visible puddle contribution until an
alternative passes visual qualification. This result does not eliminate either
cost, and Vulkan's neutral result is evidence against treating depth publication
alone as the general solution.

## Evidence

Controller: artifacts/lighting-foundation/geometry-depth-20261028 (ignored).
Worker: WORKER_EVIDENCE_ROOT/phlosion-geometry-depth-20261028. Retained scripts, source and
payload manifests, validation logs, profiles/telemetry, images and review panels
identify the tested implementation. No local builds, GPU tests or inference.
