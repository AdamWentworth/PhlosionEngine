# Prepared contact depth performance - 2026-10-28

## Change and evidence

The shared contact/depth group cost about 5 ms on both Windows backends.
New scopes isolate preparation, horizon search, spatial filtering, temporal
filtering and publication. The horizon search alone averaged 1.94 ms on D3D12
and 1.91 ms on Vulkan in the instrumentation-only baseline; the complete filter
averaged 2.32 and 2.29 ms.

Previously each horizon sample selected the farthest of four full-resolution
depths again. A half-size preparation pass now makes that selection once.
The raw search and spatial filter reuse its depth and selected pixel. Row-major
tie selection, conventional depth, odd-extent clamping and normal lookup are
preserved. The source-traced preparation/search/filter sequence is documented in
Cyberpunk2077Research's lighting/reflections/VISIBILITY_PRODUCER.md; this is an
independent Phlosion storage adapter, not a claim of identical retail packing.

The existing RGBA16F scratch/copy infrastructure stores all 32 depth bits as
11/11/10-bit integer chunks, with the selected 2x2 pixel offset in alpha.
All chunk integers are exactly representable in half precision and are fetched
without interpolation. No depth precision is discarded. Preparation uses the
history descriptor slot only during raw/spatial stages; temporal filtering keeps
its previous visibility/depth bindings. Full-resolution reconstruction retains
the original selection path.

The change adds one half-size draw/copy and a reusable snapshot per in-flight
frame. At 1920x1080, the existing power-of-two snapshot allocator uses two
1024x1024 RGBA16F images: 16 MiB of additional image storage, excluding driver
allocation overhead. No new full-resolution target or persistent history is added.

Search resolution, 15 directions x 10 samples, radius, thickness, spatial/temporal
filters, exposure, reconstruction, and quality settings remain unchanged.
No rain, lighting, reflection, probe-update or shadow budget is reduced.
Hybrid sand/hard-surface splashes and puddle rings are unchanged.

## Qualification

Execution used gpu-worker Windows, optimized RelWithDebInfo, EVGA RTX 2060 SUPER,
LUID 000000000000be8e, UUID GPU_1_UUID.
controller only orchestrated, reviewed and integrated. Engine editor/native tests
and game runtime/plugins built successfully.

Four CPU CTest entries pass: engine contracts, contact contracts (including all
2048 exactly stored half-precision chunk values), temporal-scene contracts and
game exploration. Native validation passes 26 before/after contact cases across
D3D12/Vulkan, plus four odd-extent candidate cases. All 52 paired off/on images
are within the existing one-byte comparison limit. Original fixture assertions
are unchanged: direct/emission/reset/cut outputs retain their exact requirements;
masking, disocclusion, specular direction, motion and shared-surface behavior pass.
Shared-surface and 641x481 contact tests are now registered in CTest.

Twelve validation-enabled scene runs pass: wet/dry/orbit/battle runtime and
wet/battle editor on both APIs. Expected battle state, motion/surface coverage,
temporal reflection output and clustered/probe work are present; no renderer
validation errors. Eight comparison images stay within one byte of the accepted
references. The four daylight/orbit images differ by at most 3-4 bytes, with
10-143 pixels exceeding one byte in each 1920x1080 image.

Later frame-185 timing images are also not universally pixel-identical.
Vulkan wet/dry maxima are 1/5 bytes (0/6 pixels over four); D3D12 wet/dry maxima
are 7/14 bytes (5/21 pixels over four). Mean differences are below 0.0069 byte per
channel. Whole-image pairs and magnified worst-difference crops were reviewed:
no broad change in lighting, rain, geometry or silhouettes was visible.
Differences concentrate around fixture highlights and edges. The cause of
every outlying pixel was not isolated; no original native threshold was relaxed.
These limits accompany the preserved-quality claim.

## Matched warm performance

Baseline controller engine 39c8c0be294fd8418680c84c53e3cac82fd428b1 and game
144f279793b183ade2c8e182c8ce1052f963921a. The frozen profiled baseline adds only
the timing scopes; its rendering is unchanged. Source and payload hashes are
retained. Native 1920x1080 Medium: wet is night/Hybrid heavy rain, dry is clear noon.
Both APIs use baseline/candidate/candidate/baseline wet order, 280 measured
frames per version. Each dry pair supplies 140 frames per version. Frames
40-179 of 220 are measured, with validation and diagnostic readbacks disabled
and profiling enabled. Compilation, startup and screenshot readback are excluded.
Every run starts at or below 45 C; thermal flags are checked through frame-185
screenshot completion.

All times in milliseconds, baseline -> candidate.

| API / scene | GPU median | GPU mean | GPU p95 | GPU p99 |
| --- | --- | --- | --- | --- |
| d3d12 / wet | 23.820 -> 23.225 | 23.889 -> 23.376 | 25.300 -> 24.899 | 25.722 -> 25.128 |
| vulkan / wet | 25.950 -> 25.310 | 26.018 -> 25.397 | 27.204 -> 26.530 | 27.272 -> 26.720 |
| d3d12 / dry | 15.710 -> 15.162 | 15.891 -> 15.295 | 17.566 -> 16.951 | 17.736 -> 17.168 |
| vulkan / dry | 18.514 -> 17.814 | 18.476 -> 17.853 | 19.504 -> 18.843 | 19.606 -> 18.942 |

Wet-scene mean contact costs, including the new preparation and publication:

| API / scope | Baseline | Candidate |
| --- | --- | --- |
| d3d12 / contact-filter | 2.326 | 1.735 |
| d3d12 / contact-prepare | 0.000 | 0.023 |
| d3d12 / contact-horizon | 1.948 | 1.369 |
| d3d12 / contact-spatial | 0.232 | 0.164 |
| d3d12 / contact-temporal | 0.060 | 0.066 |
| d3d12 / contact-publish | 0.085 | 0.112 |
| vulkan / contact-filter | 2.298 | 1.765 |
| vulkan / contact-prepare | 0.000 | 0.023 |
| vulkan / contact-horizon | 1.916 | 1.324 |
| vulkan / contact-spatial | 0.227 | 0.226 |
| vulkan / contact-temporal | 0.051 | 0.057 |
| vulkan / contact-publish | 0.103 | 0.136 |

The selected GPU reached 66 C. No thermal flags occurred
through the completion marker, with no invalid/dropped profile frames or renderer
errors. All measured frames retain three probe-relight spans, no static material
recapture and no removed screen-source redraw. CPU/present distributions remain
in analysis.json. The isolated first-trial result is retained separately; these
repeated wet runs govern the reported saving.

These are instrumented warm timings, not shipping FPS or a matched retail
Cyberpunk 2077 benchmark. Cold shader preparation still takes minutes after
a shader-family change and is not solved by this pass. Linux remains an explicit
release gate; Windows qualification does not cover it.

## Remaining work

The wet-scene 16.67 ms target remains unmet. Keep the connected technique and
quality budgets while addressing remaining contact-search cost, repeated opaque
geometry/coverage and the extra planar-reflection pass, then C8 probe filtering/
publication and shadow refresh scheduling. Complete moving actor/light/weather
freshness must survive any scheduling change. Technique-coverage percentages
are unchanged by this cost optimization.

## Evidence

Worker: WORKER_EVIDENCE_ROOT/phlosion-contact-cost-20261028.
Controller: artifacts/lighting-foundation/contact-cost-20261028.
Retained evidence includes build-prepared.json/logs, cpu-checks.json,
native-baseline.json, prepared-checks.json, scenes-final.json, final-checks.json,
analysis.json, visual-review.json, tested-source.json/.patch, frozen payload
hashes, native/scene images, phase profiles and GPU telemetry.

Two harness setup mistakes are retained: a nonexistent build target was corrected
to PhlosionContactVisibilityTests, and an invalid native mode 'corner' was
corrected to 'contact'. The useful baseline timings were retained; no failed
rendering assertion or comparison threshold was removed or weakened.
