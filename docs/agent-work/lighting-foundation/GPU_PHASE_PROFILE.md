# GPU phase profile — 2026-10-25

## Preserved baseline

Engine 6ddc5e3d8626e498f6f02f566ebcc4b7b51fed02 and game
d46396a59eabc9a2d7947dffce837cc7d588cc7e contain the accepted prepared local
reflection replacement. The controller recorded 1,490 baseline file hashes;
the Windows worker matched those bytes before editing. This pass adds
measurement and an explicit diagnostic, not a default quality reduction.

## Recording

Set PHLOSION_RENDER_PROFILE_PATH to a writable JSON filename before launching
the runtime or editor. Optional PHLOSION_RENDER_PROFILE_TOTALS_ONLY=1 records
whole frames without phase queries. Normal launches do not set either variable.
The recorder supports Vulkan and D3D12; it is independent of the old OpenGL path.

Each frame allows 128 nested scopes, with a bounded 2,048-frame capture.
GPU timestamps are read only after the renderer's existing frame fence.
No additional GPU wait is inserted. Pending frames are drained after the
ordinary shutdown GPU wait. JSON is written at shutdown, not during timing.
Invalid results and capacity overflow are recorded and rejected by the summary
tool. Stop a capture normally to obtain its file; crashes do not flush it.

Example after copying the capture to a machine permitted to process it:

    python tools/render/summarize_frame_profile.py capture.json --view field-view --warmup 30 --samples 200 --output summary.json --csv phases.csv

Use battle-view to discard the exploration/transition portion before battle
warmup. Omit --view for totals-only captures. The tool reports median, mean,
p95, p99 and maximum. Infrequent phases contribute zero on inactive frames;
active_frames records how often they ran.

All intervals are inclusive. A local-reflection-filter interval is INSIDE
local-reflection-capture; static-geometry, foliage and actors are inside their
parent draw. Do not add nested durations or add CPU and GPU time together.
cpu_frame_ms is renderer begin/end wall time, not host-frame time or execution
time on a CPU core. begin_wait_ms includes initial setup and GPU/acquire waits;
present_ms is the measured present call. Submission scopes can include waits.
Whole GPU time includes GPU idle gaps within the submitted command stream.
The scopes attribute command-stream intervals; pipelined GPU work can overlap.

PHLOSION_DIAG_SKIP_SCREEN_TRACE=1 is diagnostic only. It retains source rendering,
depth-tile preparation and depth reuse, then makes receivers use their existing
fallback. It deliberately changes the image and must never become a game default.
D3D12's source-stage duration changes under this diagnostic, so whole-frame
differences are not an additive attribution of pure raymarch cost.

## Machine, build and fixture

All implementation, optimized builds, tests and processing ran on gpu-worker.
Windows, ASUS RTX 2060 SUPER (single 8 GB card), UUID
GPU_0_UUID, LUID 000000000000ad83,
driver 610.88; the EVGA card was not used. No local inference or controller GPU
work. Tests were serial, with cooldown to 55 C and an 88 C stop guard.

CMake RelWithDebInfo uses /O2, /Ob1, symbols and release CRT. The project removes
NDEBUG for this configuration: assertions ARE enabled. It is optimized
Development, not Debug and not a separately qualified pure Release build.
An earlier conversational claim that assertions were disabled was corrected.

1920x1080, accepted materials, heavy Hybrid rain and wet night, fixed replay and
weather seed. Per-frame weather console trace is disabled in phase measurements.
Deferred performance trace is retained. GPU validation is enabled only for
correctness controls, not performance rows. The usual sample window contains
200 frames after 30 warm frames; battle selection is after the transition.

## Measurements

| Case | D3D12 GPU median / p95 ms | Vulkan GPU median / p95 ms |
| --- | ---: | ---: |
| wet | 53.25 / 56.67 | 68.14 / 71.77 |
| dry | 24.60 / 46.43 | 52.90 / 57.94 |
| orbit | 65.78 / 72.77 | 80.47 / 91.34 |
| battle | 59.27 / 102.81 | 67.01 / 123.84 |

The heavy-wet final geometry stage takes 32.59 ms D3D12 / 39.68 ms Vulkan;
static geometry accounts for 32.16 / 39.21 ms. Skipping only receiver screen
reflection sampling brings geometry to 9.39 / 9.22 ms. Its source pass remains:
3.90 / 9.55 ms normally, 9.30 / 9.79 ms in the diagnostic.
Whole diagnostic frames are 36.73 / 39.35 ms. This is a bottleneck experiment,
not an acceptable shipped configuration or a promised optimization saving.

The first broad profile measured roughly 3–4 ms local capture/filter, 3 ms planar
reflections, 4–5 ms contact depth/filter, 1–3 ms haze, and 1–2 ms rain GPU work.
Rain build/submission also costs about 7–8 ms of CPU wall time. Use the retained
per-case JSON for precise intervals and tails rather than adding these estimates.
The 16.67 ms target is not achieved; even a free screen-reflection search/source
would leave substantial work. Battle p95 spikes remain unresolved.

## Source-guided direction

The private read-only research reference remains
Cyberpunk2077Research/lighting/reflections/SOURCE_ARCHITECTURE_REVIEW.md.
The inspected June 2021 paths use previous-frame scene-colour feedback, prepared
depth and bounded quality-dependent ray budgets. Our current path draws a
separate source scene and may traverse up to 4,096 pixel steps inside material
shading. Those architectural differences are stronger leads than raising GPU
clocks or blindly lowering rain density.

The inspected roughness cutoffs also vary by quality (ordinary high fades .5–.7,
ultra .6–.8). Phlosion's .45–.75 is not evidence of a universally wrong cutoff;
do not claim that rough sand can simply be discarded in every reference mode.
Temporal feedback needs camera/velocity, disocclusion, exposure and cut-reset
contracts before removing the current source pass.

## Verification and limits

Both APIs have wet, dry, orbit and battle profiles; 200 selected frames per case,
zero invalid/dropped frames and spans, and no sampled thermal-slowdown flags.
Profiler-disabled, totals-only and scoped controls retain the accepted image
and have no established timing regression beyond observed run variation.
GPU validation controls pass; known D3D12 warning 1328 remains allowed by the
existing game harness, with no new errors. Validation timing is not benchmark data.

The dry D3D12 image is not bitwise repeatable: the unchanged accepted binary
differs from itself by a maximum of 3 colour steps in 9 pixels. The profiled
candidate matches the repeated accepted control within 1; all attempts remain
in dry-repeat.json. This caveat is recorded, not silently converted into a
looser general image gate. Other paired runtime cases satisfy the existing
one-step/state-equality checks.

The new recorder CPU contract and five existing renderer contracts pass.
The summary utility has real-capture and malformed-input checks. Windows is
qualified here; Linux was not rebooted/retested in this pass.

Worker evidence: WORKER_EVIDENCE_ROOT/phlosion-frame-cost-20261025. Key records:
controller-baseline.json, phase-r3-source-manifest.json, refined-suite.json,
dry-repeat.json, summary-checks.json, phase-r3-profile-test.log, per-case
results/*/{manifest.json,phases.json,gpu.csv,runtime.json,runtime.png,runtime.log}.
Earlier failed fixture comparisons remain available: a 240-frame battle image
was mistakenly compared with a 340-frame reference; the corrected matched
fixture passes. Validation-on and validation-off images are compared separately.

## Optimization experiment

Rejected, not installed in the final renderer. The prototype aggregates existing
8x8 conservative depth bounds into 64x64 bounds and attempts larger proven-empty
jumps before the fine search. This is an independent experiment, not a claim that
the inspected reference uses this exact hierarchy. Six native mirror/thin-source
checks pass with GPU validation, including 1921x1081.

| API | Experimental shader, hierarchy off | Hierarchy on |
| --- | ---: | ---: |
| vulkan | 71.400 ms | 74.628 ms |
| d3d12 | not completed | not completed |

The extra hierarchy preserves the compared Vulkan wet-night images within the existing
one-colour-step gate, but its additional work does not pay for itself. The
original profiled Vulkan shader measured 68.137 ms; even the experimental shader
with the extra level off measured 71.400 ms. That comparison alone cannot isolate
shader complexity from run variation, but enabling the level is itself slower.
The worker also spent several minutes compiling the new shader on first use;
those startup minutes are excluded from warmed timing. The remaining Direct3D
scene comparison was deliberately stopped after over seven minutes of first-use
compilation because the Vulkan regression had already rejected this prototype.
No Direct3D scene timing or full-scene image result is claimed for the experiment;
its three native Direct3D checks did finish and pass. The intentional cancellation
is recorded in hierarchy-d3d-cancelled.json.

The exact pre-experiment implementation was restored and its source/shader
hashes verified. The unapplied patch remains at
experiments/screen-hierarchy-20261025.patch. Keep hierarchy-experiment.json,
hierarchy-controls.json and native-hierarchy-smoke/manifest.json with the failed
optimization; do not interpret successful visual checks as a performance win.

## Priorities after this pass

1. Replace the inline, long screen-reflection search with a separately budgeted
   reflection pass. Preserve thin-source, offscreen fallback, mirror, roughness,
   occluder and moving-camera contracts; compare changed approximation behavior
   openly instead of silently cutting a loop bound.
2. Reuse scene colour/depth instead of the extra source draw, with explicit
   camera-cut, exposure, moving-object and disocclusion handling. The source
   review supports this architecture; it is not a one-line texture substitution.
3. Use the new profiles to address contact preparation, repeated planar/probe
   captures and rain CPU submission. Measure frame tails as well as averages.

These are ranked targets, not additive promised savings. Rain density, fixtures,
material values, output resolution and normal weather cycles were not reduced.


Final restoration checks: six engine CPU contracts, two game environment
contracts, both validation-enabled editor hosts and the summary utility pass.
Editor screenshots match the accepted host references within one colour step,
with matching scene state. The 57 composed shader/SPIR-V payloads match the
pre-experiment profiler exactly. No experimental hierarchy is active.
Source integration copies the reviewed worker bytes; it does not replace local
editor binaries or claim that a controller-side build/test was run.


## Source-call-path correction - 2026-10-26

The priorities above are historical. The user redirected this work from isolated
performance trials to reproducing the source's connected rendering architecture.
The follow-up trace is in Cyberpunk2077Research,
lighting/reflections/SOURCE_RENDER_CALL_PATH.md (24 hashed source files and 51
verified function/call anchors).

That trace follows static-world and entity lights through registration, collection,
packing, clustered culling, material-classified compute lighting, reflection
composition, temporal AA and the next frame's feedback producer. The inspected
ordinary Ultra SSR path uses four 16-step rays, quarter-size depth, half-size
mip-filtered colour feedback, hit-point motion reprojection and temporal AA.
These are connected dependencies, not permission to cap our existing loop at 16.

The geometry-redraw resolve and deferred candidate-filter prototypes were not
accepted. The former retained the expensive search and duplicated material work;
the latter regressed the Vulkan wet control. Neither is active. No new speedup
or visual-parity percentage is claimed.

Next implementation is the surface/motion/temporal foundation followed by
dedicated stochastic reflections and removal of the fresh source-colour draw.
Clustered/material-classified direct lighting shares those surface inputs.
Do not restart loop-cap, tile-size or filter-order trials before that migration.
Source-contract and temporal checks must qualify intentional approximation
changes; retain old reference tests instead of silently weakening their gates.

Restoration verified on gpu-worker: 1,495 source files match the accepted controller
baseline; PhlosionEditor, CyberpunkRuntime and CyberpunkEditorPlugin rebuild in
RelWithDebInfo; five engine CPU lighting/renderer contracts pass; all 57 composed
shader/SPIR-V payloads match the accepted phase-r3 package. No new GPU timing
or runtime visual test was run after this restoration, and no new renderer
architecture has been promoted. Controller binaries were not replaced.

Worker receipts: WORKER_EVIDENCE_ROOT/phlosion-bounded-reflections-20261026/
baseline-verified.json, restored-build-shaders.json and source-evidence/.
Build/test logs: WORKER_EVIDENCE_ROOT/phlosion-reflection-architecture-20261025/
source-trace-restored-{engine,game,cpu}.log.

### First implementation dependency: material preparation

Native and composed Vulkan/D3D12 material paths now call prepareWorldMaterial,
which returns WorldSurfaceMaterial before lighting or reflection evaluation.
The record contains the evaluated normal, albedo, ORM sample, metallic,
roughness, occlusion and dielectric reflectance, including the existing wet
material response. Forward lighting consumes it immediately, so this introduces
no additional render pass, render target or history allocation.

This is a material-evaluation boundary, not a completed G-buffer. It does not
yet cache visible surfaces on the GPU, supply object motion/TAA, replace SSR,
remove the source-colour redraw or change the accepted timing figures.
The next dependency is the shared surface attachments and stable object/motion
inputs. Do not treat this refactoring as a performance optimization.

Qualification on gpu-worker: optimized engine/editor and game runtime/plugin builds,
five engine CPU contracts, and 28 native GPU cases passed. All 80 native
reflection comparisons were pixel-identical. Paired 1080p heavy-rain scene
captures at frame 48 also matched pixel-for-pixel on Vulkan and D3D12.
Native fixtures preceded a comment-only edit; final rebuilt binaries were used
for the full-scene checks. No new frame-time benchmark is claimed.

The accepted D3D12 full-scene baseline emits 942 severity-2 ID-1328 buffer initial
state warnings. The prepared-material build emits the same messages/count and
no additional validation messages; this is not a warning-free full-scene result.
The first broad scanner stopped on those baseline warnings; its failed receipt
is retained alongside the reviewed continuation, not rewritten as a clean pass.
Evidence: Cyberpunk2077Research/data/evidence/lighting-source-call-path-20261026/
surface-qualification.json; full worker evidence remains in
WORKER_EVIDENCE_ROOT/phlosion-bounded-reflections-20261026.


### Surface export shader contract - 2026-10-26

Native and composed Vulkan/D3D12 now have a separate surface-output shader
variant. It calls the same material preparation, including mapped normals and
wet-material adjustments, then exports linear albedo/AO, encoded world
normal/roughness, metal/dielectric reflectance/validity, and unexposed HDR
emission. Lighting and reflection evaluation are outside that material branch.
The direct Vulkan artifact is world_surface.frag.spv; D3D12 uses mainSurface.
The material profile has an optional eighth Vulkan artifact; legacy profiles
still load, and malformed or escaping optional artifacts fail explicitly.
The game adds this artifact through its own CMake composition, without changing
PokemonModelSupport source or runtime quality defaults.

At this shader-only checkpoint, this was the producer/data contract, not an
activated G-buffer. Attachment allocation followed in the shared-pass stage below. Four RGBA16F planes are
the initial qualification layout (32 bytes/pixel), not the intended shipping
bandwidth budget. Unsupported, transparent, sky and special-effect paths are
excluded; material response hooks such as character eye/inking behavior still
need a forward fallback or explicit deferred classification. Capture outputs
must not be treated as a complete deferred replacement for every project material.

PHLOSION_VALIDATE_SURFACE_SHADER=1 compiles the actual native/composed D3D12
mainSurface entry for standard PBR modes 2 and 254 during renderer initialization, without selecting it for
drawing. Vulkan native/project variants build with four output locations.
The new GPU fixture executes packing/unpacking numerically on both APIs,
including HDR emission, normal normalization, roughness, AO and dielectric
reflectance. It does not yet test four simultaneous raster attachments, masked
capture coverage, half-float quantization or a deferred lighting consumer.

Next: allocate and bind the shared attachments with correct frame/view/scope/
resize lifetimes, qualify actual rasterized material values and coverage, then
add motion/history consumers and remove the redundant source-colour draw.
Do not insert another permanent geometry redraw, relabel this as a speedup,
or lower ray counts before temporal reconstruction is ready. Medium/High
remain the intended primary quality budgets.


The first D3D12 full-game qualification was stopped during a long cold start
with the unspecialized surface check enabled. The log did not identify the
individual compile invocation; its failed receipt is retained. The
qualification now uses the renderer's established constant-material approach
for PBR modes 2 and 254; it does not validate every project-specific material.
This changes only the opt-in compile check, not default rendering.


Qualification: optimized engine/editor and game runtime/plugin builds passed.
Five engine CPU contracts pass after correcting an existing composition check
that still expected direct reflection calls (its failure was reproduced on HEAD).
Both native/project Vulkan surface artifacts expose locations 0 through 3.
GPU packing tests pass 32 numeric cases per API; ten native reflection cases
produce 34 pixel-identical comparisons. The final 1080p heavy-rain frame-48
controls also match exactly on both APIs. D3D12 retains the same 942 severity-2
ID-1328 warnings as the accepted baseline; Vulkan reports none. No new GPU
frame-time benchmark was taken.

Native comparison fixtures preceded the opt-in D3D12 validation-selector change;
the final D3D12 scene used the specialized checker. Existing Vulkan SPIR-V payloads
are byte-identical (41/41). Tests do not activate the new surface render targets.
Worker evidence: WORKER_EVIDENCE_ROOT/phlosion-shared-surface-20261026/qualification.json.
Controller copy: Cyberpunk2077Research/data/evidence/lighting-shared-surface-20261026.
Controller source integration does not replace locally installed editor binaries.


### Shared raster surfaces and first consumer - 2026-10-26

Both native APIs now allocate four RGBA16F attachments, execute the surface
export variant during a real geometry pass, retain depth, and make the results
readable by later passes. This is no longer a compile/packing-only fixture.
IWorldSurfaceBuffer exposes capture, invalidation, diagnostic plane display,
and contact-visibility resolve from the captured depth and mapped normals.
The resolve skips the old contact geometry pass and its normal snapshot copy;
Vulkan also consumes the capture depth directly. It does not add a second
geometry callback on top of the old contact prepass.

The game enables this experimental replacement with
PHLOSION_SHARED_SURFACE_CONTACT=1 in field and battle presentation. Default
launch remains on the accepted forward/contact path. Completed reflection
probes may coexist with a surface capture; nested active captures are rejected.
The Vulkan indirect submission declines this pass and uses its existing direct
fallback. Transparent/sky/special effects and materials without surface exports
remain excluded. This is not yet complete deferred material classification.

Capture slots belong to an in-flight frame and are reused only after its fence.
Each capture in a frame has a distinct slot (maximum 16), so recaptures/views
cannot overwrite a prior draw's sampled textures. Resizing replaces that slot's
images rather than accumulating size-keyed allocations. D3D12 retains the SRV
indices across swapchain resizing and releases the slots at shutdown; Vulkan
frees descriptors with their images. Starting/ending a scene or explicitly
clearing invalidates capture handles. Contact history rejects switching between
geometric and mapped-normal inputs as well as its existing view/scope checks.

The four 16-bit planes are still a 32-byte/pixel qualification format. The
normal-mapped contact result is intentionally not asserted pixel-identical to
the geometric-normal path. The native flat-surface comparison is limited to
1/255; the existing direct/emission/reset exact-image checks were not weakened.
The game flag remains opt-in pending broader material/motion/battle review.

Outstanding P2: rigid/skinned/wind motion, view/exposure/history invalidation,
temporal reflection reconstruction, separate reflection/lighting consumers and
removing the source-colour redraw and per-material long SSR search. Only then
should Medium/High reflection budgets be qualified with matched timings.
No frame-time improvement, 60-FPS acceptance, or new parity percentage is
claimed by this shared-buffer stage.


Final qualification on gpu-worker (Windows, ASUS RTX 2060 SUPER, optimized
RelWithDebInfo): 26 native GPU cases, seven engine CPU contracts, four game
CPU contracts, four runtime comparisons and four editor comparisons passed.
Both APIs' default 1080p heavy-rain/night images match their accepted baselines
exactly, including editor views. The shared path changes mapped-normal contact
shading: runtime maximum differences are 21/255 Vulkan and 19/255 D3D12;
editor maxima are 20/255 and 5/255. These are reported differences, not a
lossless-equivalence claim. The flat native contact comparison is exact on
D3D12 and within 1/255 on Vulkan. Both shared-path battle smoke tests reach an
encounter and complete without new validation errors; full battle effects and
moving-camera acceptance remain outstanding.

Vulkan validation is clean. Wet-night D3D12 retains the baseline 942 severity-2
ID-1328 buffer initial-state warnings; the battle smoke has 974 of that same warning. This is not a warning-free qualification.
The surface pipeline initializes lazily so default Vulkan startup does not
compile the unused MRT variant. No clean performance benchmark was run, and
the local editor binaries were not replaced by this source integration.

Worker receipt: WORKER_EVIDENCE_ROOT/phlosion-surface-pass-20261026/qualification.json.
Controller evidence: Cyberpunk2077Research/data/evidence/lighting-surface-pass-20261026.


### Separate surface-reflection consumer - 2026-10-26

PHLOSION_SHARED_SURFACE_REFLECTIONS=1 opts field/battle into the shared surface
capture/contact path and a separate full-screen reflection resolve. The new
consumer reads captured world normals, roughness, validity and depth, plus the
completed current-frame colour/depth source. Eligible visible opaque/masked
receivers reuse its HDR radiance/confidence before their normal material/DFG
response. Transparent materials explicitly retain the original search, including
near-coplanar overlays; invalid or depth-mismatched pixels also fall back.

This establishes a reflection consumer of the real surface buffers without
another geometry callback. It intentionally retains the existing exhaustive
reference traversal and rough filtering. The reference material search is kept
unchanged below the prepared-result branch so unsupported receivers and default
launches keep their previous behavior. This is not the traced stochastic SSR,
temporal reconstruction, or removal of the current-frame source-colour redraw.

The resolve requires an exact matching camera/extent and completed source; a
missing, stale or already-resolved source is rejected. The result is an immutable
frame/capture-slot HDR snapshot. D3D12 and Vulkan reuse the contact filter's
otherwise-unused history binding during final material shading; the current
contact visibility binding and its persistent history resource stay intact.
Vulkan creates its new pipeline lazily. Neither path enables the experiment by
default. The four surface planes remain the larger qualification format.

Motion for rigid objects, skinning and wind, view/exposure history invalidation,
disocclusion rejection, and temporal reconstruction remain the prerequisites
for replacing the reference traversal with Medium/High ray budgets and removing
the fresh source draw. Do not claim those costs have already been eliminated.
Startup shader compilation, GPU frame time and visual differences are separate
measurements; shader compilation is not a frame-time sample.

Evidence is retained under Cyberpunk2077Research/data/evidence/
lighting-reflection-consumer-20261026; the worker task is
WORKER_EVIDENCE_ROOT/phlosion-reflection-consumer-20261026.


### Reflection consumer qualification - 2026-10-26

Disposition: retain as an opt-in architecture milestone, not a performance
optimization or the completed P2 path. All implementation, optimized
RelWithDebInfo builds, CPU checks and GPU runs used gpu-worker; controller work
was limited to orchestration, review and exact-delta integration.

- 30 native reflection cases plus two surface-authority cases passed across
  D3D12/Vulkan. The authority case deliberately changes captured roughness while
  leaving final material roughness unchanged, proving the shared data controls
  the reflection result. Thin geometry, masking, near-coplanar transparency,
  camera/extent changes and invalidation are covered.
- Seven engine and four game CPU tests passed. Four runtime and four editor
  views passed their fixed-view checks; two battle smoke runs reached battle.
  Vulkan validation stayed clean. D3D12 retained the same known ID-1328
  severity-2 warnings as its controls (942 field/editor; 974 battle).
- Default fixed 48/100-frame views were byte-identical. Against the prior
  shared-contact path, experimental runtime images changed by mean 0.00436/255
  or less, maximum 68/255; editor mean <=0.000793/255, maximum 20/255. The largest
  differences are isolated reflective glints; these are not identical images
  or complete moving-gameplay acceptance.
- The additional frame-240 exact-image gate failed by 1/255 in 150 pixels.
  Repeating the unchanged accepted D3D12 binary also differed by 1/255 in 144
  pixels. The failed receipt remains in the evidence; no threshold was loosened.
  Vulkan accepted/candidate comparison differed by at most 1/255 in 203 pixels.

Timing: 1920x1080, fixed heavy rain/night, ASUS RTX 2060 SUPER
(GPU_0_UUID), validation disabled, total-only GPU
timestamps, frames 61 through 240. Per API, run order was inline / prepared /
prepared / inline. Both controls used shared surface/contact data, and the same
frozen production binary and settings. The table averages each pair of run
medians; it is not a comparison against a retail Cyberpunk 2077 run.

| API | Inline search (ms) | Separate consumer (ms) | Added cost |
| --- | ---: | ---: | ---: |
| D3D12 | 51.4599 | 51.9885 | +0.5286 ms (+1.03%) |
| Vulkan | 66.5181 | 68.1039 | +1.5858 ms (+2.38%) |

Recorded temperatures peaked at 73-75 C, median active clocks were
1927.5-1935 MHz, and no thermal-slowdown flags were reported. These runs do not
meet the 16.67 ms goal.

A separate D3D12 full-phase pair moved about 7.32 ms out of final geometry
shading into a 7.92 ms surface-reflection resolve. The parent source phase
includes that resolve; do not add inclusive parent and child times together.
The reference traversal is still expensive. This is evidence of relocated work,
not eliminated work.

Default-off spot controls also require follow-up: prior D3D12 medians were
52.3179/52.0278 ms and candidate medians were 52.9208/52.9376 ms, a roughly
0.76 ms higher average. Vulkan's single pair went from 69.5105 to 67.5831 ms.
This is not enough to attribute a cause or promise unchanged default performance.
The opt-in switch prevents the additional surface/resolve passes by default;
it does not restore the previous compiled shader. Investigate default shader
cost alongside the next consumer work before enabling or declaring a shipping
performance win.

Next dependency: motion and history for rigid, skinned and wind-driven geometry,
camera cuts, exposure and disocclusion, followed by temporal reconstruction and
bounded Medium/High searches. Removing the current-frame source redraw remains
outstanding. Keep the exhaustive path as the correctness reference.


### Bounded reflection default - 2026-10-26

The user authorized fidelity tradeoffs for demonstrated performance. A bounded
single-frame Medium reflection setting now replaces exhaustive inline searching
by default in PokemonCyberpunk. Matched native-1080p wet-night ABBA runs on the
ASUS RTX 2060 SUPER reduced mean-of-run-median GPU time from 52.906 to 40.405 ms
on D3D12 (-23.6%) and 67.319 to 45.245 ms on Vulkan (-32.8%). This is an intentional
quality change: thin reflective details can be missed and rough materials use
local reflections earlier. Rain and authored light state remain unchanged.

See [SSR_QUALITY_PERFORMANCE.md](SSR_QUALITY_PERFORMANCE.md) for the broader view
controls, all qualification limits and evidence. The earlier opt-in shared
reflection consumer stays off by default. Temporal reconstruction and removal of
the source draw remain outstanding; the 16.67 ms target is not yet met.
