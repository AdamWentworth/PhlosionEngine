# Opaque material reuse and probe-filter cost - 2026-10-28

## Change and contract boundary

Standard opaque composition now reads the evaluated albedo, mapped normal,
metallic/F0/AO/roughness and emission captured earlier in the frame. It no longer
prepares those same material values again for its final lighting draw. D3D12 and
Vulkan use the existing compact surface planes; no extra full-resolution target
is allocated. Both built-in and game-composed material shaders use the consumer.
Specialized character/foliage and transparent participation remain on their
required paths. Mask/coverage and geometry replay still exist, as can base-color
sampling before material evaluation. This is not fullscreen deferred composition.

Probe roughness filtering still makes the same 60 small draws/copies at the
same resolution and sample counts. It now overwrites each filter rectangle
without depth testing, preserves scratch scene attachments between intermediate
copies, and clears the full scene once after the final copy instead of 60 times.
Prepared and legacy filter callers preserve their final-clear behavior. Placed
probes retain the same complete-generation publication and quality face budgets.
This removes redundant work; it does not implement asynchronous/staggered filter
scheduling or close C8.

Native qualification also exposed a Vulkan indexed-material binding error:
binding 5 used VK_WHOLE_SIZE with a nonzero dynamic offset. The binding now stays
at offset zero; transient draw-state records start at a whole-record boundary
and the push draw index addresses them. This preserves per-draw data without
mutating a descriptor in use. Indexed forward/clustered/consumer tests pass
validation after the correction.

Source mapping: C1 evaluated surface data -> C7 lighting/composition consumers,
and C8 prepared filtered radiance. See COMPLETE_TECHNIQUE.md and the existing
research SOURCE_RENDER_CALL_PATH.md. These are independent Phlosion changes,
not a claim to reproduce all source material variants or the complete engine.

## Verification

All execution used gpu-worker Windows, optimized RelWithDebInfo. Engine editor,
native fixtures, game runtime and game plugin built successfully. The two CPU
CTest entries (engine contracts and probe surface cache) and 50 native GPU runs
passed. Those runs cover both APIs, built-in and actual composed materials,
forward/clustered consumers, unsupported/masked coverage, resize, orthographic,
indexed submission, placed-probe publication and retained-versus-fresh relighting
with wetness, emission and haze.

A permanent consumer test deliberately changes final-draw color, roughness,
metalness and emission after capturing the surface. The stored-material result
must remain pixel-exact against its unmodified reference. Both APIs and shader
families pass. Existing probe-filter reference images remain pixel-exact; no
reference-image threshold was relaxed.

Twelve actual game scene runs pass: wet/dry/orbit/battle runtime and wet/battle
editor on D3D12 and Vulkan, including expected battle state, surface/motion
coverage, reflection output and clustered/probe stages. No validation errors.
The obsolete editor fixture was corrected before its test could launch; that
failed harness attempt is retained separately.

Compact decoding and stored-surface coverage replace freshly evaluated values
in final shading. Full-game images are not bit-identical to the earlier renderer.
Runtime mean absolute RGB error is 0.066..0.096 byte/channel (out of 255) for
wet/orbit/battle and about 0.080 for dry. Wet maximum is 28; battle 14..15.
The dry maximum of 120 occurs in isolated tiny ground/edge details: 36 pixels
on D3D12 and 31 on Vulkan exceed 32/255, below 0.002% of either image.
Before/after, magnified differences and a crop of the maximum were reviewed.
Lighting, vegetation, scene silhouettes, puddles and rain remain visually
consistent in the checked views. Do not describe this as exact old/new pixels
or as proof for every view. All thresholds in the native tests are unchanged.

No rain, light, reflection sample count, render resolution or quality budget
was reduced. Hybrid's distinct surface splashes and puddle rings are unchanged
in source. Cold shader preparation remains costly: the first full-game D3D12
launch in this task took roughly nine minutes preparing shaders. Warm GPU
timings below exclude startup; this change does not solve cold-start hitches.

## Matched warm measurements

Baseline engine 1a2785614ec5262ff13d6a7857223c93e2751764 and game
fa3f4c1e67937d7b4ca0165c09c0177179e460ef are the committed starting point.
Those commits only add documentation to the preceding qualified binaries
(engine 1c793ade / game beffc83a); the frozen baseline payload hashes are retained.
Candidate is the exact source delta in tested-source.json plus the report
documentation. Game runtime source and content are unchanged.

Windows 10.0.26300.0 on gpu-worker, EVGA RTX 2060 SUPER, LUID 000000000000be8e,
UUID GPU_1_UUID. Native 1920x1080, Medium.
Wet uses Hybrid heavy rain at night; dry uses noon. Wet run order per API is
baseline/candidate/candidate/baseline. Dry has one supporting pair, not a repeated
dry acceptance campaign. Each run starts at or below 45 C.

Each optimized run records 220 frames; frames 40..179 supply 140 samples.
The tables pool the two wet repeats (280 frames/version) and use 140 dry frames.
Validation and motion/temporal diagnostic readbacks are off during timing;
phase profiling stays on. Frame-185 screenshot completion marks the end of the
thermal check, after the measured interval. No missing/invalid/dropped profile
frames or renderer errors. These are instrumented warm timings, not shipping
FPS, cold-start performance or a same-hardware retail Cyberpunk benchmark.

Four validation controls reproduce each version's own accepted image within
the unchanged one-byte tolerance; these are not old/new equivalence tests.
Candidate measured frames retain three probe-relight spans, with no static
material recapture. Neither version draws the removed screen-reflection source.

All table times are milliseconds; arrows mean committed baseline -> candidate.

| API | Scene | GPU median | GPU mean | GPU p95 | GPU p99 |
| --- | --- | --- | --- | --- | --- |
| D3D12 | wet | 27.117 -> 23.636 | 27.243 -> 23.736 | 28.900 -> 25.270 | 29.325 -> 25.451 |
| D3D12 | dry | 18.934 -> 15.549 | 18.189 -> 15.472 | 21.042 -> 17.490 | 21.211 -> 17.615 |
| VULKAN | wet | 28.790 -> 26.542 | 28.971 -> 26.711 | 31.220 -> 28.029 | 31.443 -> 28.243 |
| VULKAN | dry | 20.677 -> 19.201 | 21.168 -> 19.234 | 23.269 -> 20.243 | 23.520 -> 20.318 |

CPU frame and present statistics, mean / median / p95 / p99:

| API / scene / version | CPU frame | Present |
| --- | --- | --- |
| d3d12 / wet / baseline | 28.729 / 26.588 / 43.291 / 50.921 | 0.186 / 0.175 / 0.237 / 0.306 |
| d3d12 / wet / candidate | 25.754 / 23.773 / 38.484 / 41.249 | 0.196 / 0.180 / 0.278 / 0.793 |
| d3d12 / dry / baseline | 18.669 / 17.748 / 30.998 / 34.990 | 0.190 / 0.170 / 0.254 / 0.588 |
| d3d12 / dry / candidate | 16.169 / 14.950 / 26.459 / 28.739 | 0.172 / 0.157 / 0.234 / 0.266 |
| vulkan / wet / baseline | 28.799 / 27.396 / 33.546 / 34.240 | 0.071 / 0.068 / 0.097 / 0.104 |
| vulkan / wet / candidate | 26.523 / 25.952 / 30.210 / 30.883 | 0.070 / 0.066 / 0.095 / 0.108 |
| vulkan / dry / baseline | 20.982 / 20.198 / 25.257 / 25.758 | 0.069 / 0.064 / 0.094 / 0.122 |
| vulkan / dry / candidate | 19.055 / 18.638 / 22.253 / 23.191 | 0.071 / 0.064 / 0.094 / 0.159 |

Wet-frame median GPU reductions: D3D12 12.8%, VULKAN 7.8%.
Selected-card peak temperatures were 61..65 C. No thermal slowdown occurred through the completion marker. 1 post-marker flagged telemetry samples are retained.
The wet-night 16.67 ms target remains unmet. A dry median below that target
does not establish sustained 60 FPS; tail frames and CPU pacing still matter.

## Measured work and next targets

Mean scope time across wet frames (nested scopes must not be added to parents):

| Scope | D3D12 old -> new | Vulkan old -> new |
| --- | --- | --- |
| Final geometry/material shading | 7.530 -> 4.303 | 6.609 -> 4.739 |
| Combined contact/depth/setup | 5.142 -> 5.083 | 5.008 -> 4.989 |
| Separate planar reflection | 3.936 -> 3.909 | 3.722 -> 3.732 |
| Probe filter | 0.864 -> 0.738 | 1.150 -> 0.713 |
| Probe face relighting | 0.264 -> 0.262 | 0.250 -> 0.256 |
| Clustered lighting (inside contact/depth) | 1.253 -> 1.249 | 1.249 -> 1.244 |
| Temporal reflection | 0.459 -> 0.459 | 0.564 -> 0.562 |
| Rain | 2.063 -> 2.053 | 2.176 -> 2.213 |
| Haze | 1.780 -> 1.769 | 3.033 -> 3.040 |

The matched comparison changes material reuse and filter scratch handling
together. Scope costs locate the saving; they are not isolated per-instruction
benchmarks. The separate planar reflection remains a full scene pass. Ordinary
opaque geometry/coverage replay also remains.

Next: split residual contact/depth/reconstruction setup into useful GPU scopes
and reduce redundant work based on those measurements; investigate remaining
opaque composition and separate planar-reflection cost. Continue C8 filter/job
budgeting, probe residency/priorities and shadow refresh scheduling. Complete
generations must remain atomic and moving actors/lights/weather must stay fresh.
Do not replace these stages with arbitrary rain/resolution/sample reductions.
Linux Vulkan, broader specialized material coverage and cold shader preparation
remain open. No category percentage or retail-parity score is raised here.

## Evidence

Worker: WORKER_EVIDENCE_ROOT/phlosion-opaque-reuse-20261028.
Controller: artifacts/lighting-foundation/opaque-reuse-20261028.
Key records: tested-source.json/.patch, build-final-r2.json and build logs,
native-final-r2/checks.json and logs, scenes-final.json, timing.json, analysis.json,
publish-cost.json, per-run manifests/profiles/telemetry/images, payload hashes,
comparison sheets and scripts. Runtime/editor material profiles and binary hashes
are checked by the harness. The final code is unchanged after those tests.

Retained failures: initial test fixture field-name compile error; early native
indexed Vulkan descriptor validation failure, corrected in this change; editor
harness's stale plugin path, corrected without changing the product. These
attempts are not counted as passing runs or performance samples. One Vulkan
candidate timing repeat reported a software thermal flag before the conservative
frame-185 completion marker. It was excluded and rerun with the unchanged
cooldown and thermal gates; timing-first-attempt.json and its raw run remain.
