# Reflected surface lighting - 2026-10-28

The normal Windows D3D12/Vulkan field and battle reflection passes now capture
secondary-view surfaces, resolve clustered/material-classified lighting and
compose eligible standard opaque/masked pixels. This removes their forward
material/light replay. Specialized characters/foliage and transparent geometry
retain their required draws. The primary surfaces cannot represent unseen
reflected geometry, so the reflection has its own depth and compact planes.

No quality tier, resolution, refresh budget, light-response coefficient, rain
density, material-specific impact or puddle-ring setting changed. No new user
switch or engine/plugin ABI was introduced. This is incremental C7 integration;
C7/C8, Linux qualification and the 16.67 ms target remain open.

## Correctness and scope

- Capture requires the reflected matrix and extent. Primary/reflected resources
  have separate keys; depth restore checks the view and capture domain.
- Reflected capture keeps plane clipping and front-face conventions. Fullscreen
  composition bypasses geometry's reflected-matrix substitution.
- Ending the reflection invalidates its temporary surfaces before the main view.
  A local identity capture supports shadow derivative boundaries without
  advancing the primary camera/object history.
- Vulkan now clears previous-view SSR source pointers at scene begin. A new
  secondary-before-primary-resize regression reproduced 105 validation errors
  before the fix (destroyed sampler), and zero after it.
- Reconstructing a position from depth can put a physical light face just in
  front of its own analytic source plane. Clustered area evaluation now rejects
  coplanar receivers inside a conservative floating-point uncertainty envelope,
  including uploaded inverse error. This removes bright striped self-lighting
  on physical emitters. It is an independent coordinate-precision adapter;
  the finite-source BRDF remains unchanged.

The last correction also removes pre-existing primary-view emitter speckling,
so full-scene pixels are deliberately not identical. Diagnostic isolation of
probes, shadows, haze, emission, material formats and geometry replay did not
remove the defect; disabling direct-light radiance did. Inspecting reflected
radiance exposed the self-lit emitter faces. The new coplanar native fixture is
a control, not a claimed red/green reproduction: its unguarded version also
passed. The game captures establish the visible correction.

## Qualification

All work ran on gpu-worker Windows, EVGA RTX 2060 SUPER index 1, LUID
000000000000be8e, UUID GPU_1_UUID.
MSVC RelWithDebInfo and shader optimization level 3; no local GPU/build work.

- 96 native GPU checks passed: 22 final secondary composition/planar checks,
  52 primary/legacy-planar/temporal-secondary regressions, and 22 clustered
  lighting controls (culling, forward/consumer, LTC, metal, mask, unsupported,
  disabled, extent, orthographic and moving cases). No API validation errors.
- Three CPU checks passed: renderer policy, planar projection and motion history.
- Twelve scene pairs passed review: each API's runtime wet/dry/orbit/battle and
  editor wet/battle. Profiles confirm secondary capture, cluster cull/classify/
  shade, composition and depth reuse, with no redundant screen-reflection draw.
- Rain-state snapshots and paired performance settings match. Wet runtime
  correctness images differ by at most one display code value; orbit/battle/
  editor views include the intentional emitter correction and nearby bloom/
  reflected changes. See visual-review.json and final-review-summary.json.
- Existing native planar references remain within one code value.

## Matched performance

Immutable prior accepted payload versus qualified payload, native 1920x1080,
Medium, fixed night/overcast, same review state. Per API: wet ABBA, then dry AB;
220 frames/run, analyze frames 40-179 (280 wet frames/variant, 140 dry).
Validation off for timings, profiling on. GPU cooled to <=45 C between runs;
no builds or processing during measurements. All 12 runs accepted, no thermal
flags, maximum observed temperature 65 C. These are short-run GPU timings,
not a guaranteed sustained FPS measurement.

| Mean GPU cost (ms) | D3D12 before | D3D12 after | Vulkan before | Vulkan after |
| --- | ---: | ---: | ---: | ---: |
| Whole frame, wet | 20.852 | 19.253 | 21.984 | 21.018 |
| Planar reflection, inclusive | 3.794 | 2.082 | 3.416 | 2.260 |
| Whole frame, dry | 13.209 | 13.573 | 15.179 | 15.248 |
| Wet minus dry | 7.643 | 5.680 | 6.805 | 5.770 |

Whole wet GPU savings are 1.599 ms (7.7%) / 0.966 ms (4.4%).
Reflection savings are 1.712 ms (45.1%) / 1.157 ms (33.9%).
Wet GPU p95 improves 22.423 -> 20.807 ms / 23.427 -> 22.236 ms.
The dry control is one pair and slightly slower; do not claim zero dry cost.
The reconstruction guard is shared with primary clustered shading.

CPU costs remain material. Whole wet CPU-frame mean changes
21.613 -> 20.864 ms / 22.531 -> 22.764 ms; p95 remains about 33 / 36 ms.
Reflection CPU submission rises 2.457 -> 4.700 ms / 2.202 -> 4.646 ms.
Begin-frame waits fall 5.204 -> 3.614 ms / 7.719 -> 4.475 ms, so improved GPU
time does not establish an equal delivered-frame gain. Captured geometry and
identity preparation plus the specialized submission traversal still cost CPU.

Peak process GPU memory: D3D12 wet 1542 -> 1657 MiB; Vulkan wet 1845-1856 ->
1962-1963 MiB. Dry stays about 1515 / 1811 MiB. Secondary surfaces/depth/lighting
alone require at least 48 bytes per reflected pixel, about 47.5 MiB at
960x540 with two frames in flight; target-slot lifetime and allocation overhead
make the observed process increase larger. Long-session/many-view memory
behavior is not established by these short runs.

## Next work

Remove repeated CPU geometry/material preparation and identity bookkeeping
across capture and specialized submission while keeping explicit participation
and offscreen coverage. The current reflected capture alone costs roughly
3.3-3.5 ms CPU, and its final static traversal still costs 0.55-0.83 ms despite
eligible GPU draws being suppressed. This is the immediate dependency for
turning the GPU saving into consistent frame delivery.

Then address measured rain/haze costs without reducing the qualified effect:
primary rain costs 2.02 / 2.37 ms GPU, primary haze 1.79 / 2.61 ms and reflected
haze 0.74 / 0.96 ms. Keep shader work, scheduling and memory accounting separate;
do not call an asset or quality reduction a complete-technique implementation.

## Evidence and provenance

Worker: WORKER_EVIDENCE_ROOT/phlosion-reflected-lighting-20261028.
Controller summaries: artifacts/lighting-foundation/reflected-lighting-20261028.
Baseline engine 9be688271154a914bfe1f817a404cb48a7b351fc and game
0b48aded16abffc4b6abaee4cab3a1605f67c15b. Immutable baseline payload:
WORKER_EVIDENCE_ROOT/phlosion-planar-capture-20261028/qualified.

Receipts include qualification-final.json, regressions-final.json,
cluster-controls.json, scene-gates-final.json, visual-review.json,
analysis-matched.json, timing-summary.json, payload-comparison-final.json and
the export manifest. Raw profiles, GPU CSVs, images and rejected diagnostics
remain on the worker. No supplied source dump was copied or used in this change;
there is no new retail parity percentage or complete-technique claim.
