# Reflection cost investigation — 2026-10-25

## Qualified change

PokemonCyberpunk now selects prepared local radiance in field and battle.
PhlosionEngine supplies the reusable capture/filter/sampling policy. Direct3D
also specializes ordinary PBR source-colour capture to remove unused screen
tracing code. The 128-step geometry search and 64-sample GGX convolution no
longer run at each prepared receiver.

All implementation, builds, GPU tests and image processing ran on gpu-worker.
These 1920x1080 results are GPU medians from 200 valid samples, not presented
FPS or a comparison against retail Cyberpunk. All cases retained their weather
and gameplay state exactly. Appearance changes are intentional and localized to
the prepared fallback. No rain preset, fixture, resolution or light count was cut.

| API / case | Baseline GPU ms | Prepared GPU ms | Reduction | Thermal samples (before/after) |
| --- | ---: | ---: | ---: | --- |
| vulkan wet | 124.060 | 69.901 | 43.7% | 0/0 |
| vulkan dry | 102.354 | 55.870 | 45.4% | 0/0 |
| vulkan orbit | 142.788 | 80.091 | 43.9% | 0/0 |
| vulkan battle | 117.614 | 67.668 | 42.5% | 0/0 |
| d3d12 wet | 116.981 | 53.148 | 54.6% | 0/0 |
| d3d12 dry | 51.849 | 24.631 | 52.5% | 0/0 |
| d3d12 orbit | 134.074 | 65.513 | 51.1% | 0/0 |
| d3d12 battle | 109.659 | 55.301 | 49.6% | 1/0 |

Direct3D baseline full cases reuse the already recorded controls from this same
task. Rows with thermal flags are observations, not clean code-only speedup
claims. Wet-scene short paired controls independently reproduced the savings:
Vulkan 123.412 -> 70.231 ms; Direct3D 115.300 -> 52.482 ms, without thermal flags.
The short Direct3D prepared run without source specialization took 57.526 ms;
specialization is useful alongside the prepared-map change and preserves pixels.

Five CPU contracts, 17 Vulkan native cases and 17 final Direct3D native cases
passed. Twelve prepared native scenes match across APIs within one colour step.
Dry/wet/orbit/battle runtime and both Windows editor backends passed; the editor
Direct3D debug check retains only the pre-existing warning 1328. Shader validation
and visual oracle failures from earlier attempts remain recorded below.

The reflection atlas allocation is unchanged. Direct3D's temporary lighting
packet budget increases from 64 to 96 to accommodate the 60 producer filter
draws plus contact/screen/final views: +557,056 bytes (544 KiB) across two frames.
The original full-scene candidate hit the old budget and failed closed; the final
runtime/editor checks exercise the corrected capacity.

Cold shader compilation remains expensive. The first new Vulkan run spent
304.6 seconds total including compilation, loading and 100 frames; the first
Direct3D attempt spent 454.8 seconds before the packet-limit failure. These are
not matched cold-start comparisons. Warmed rendering gains do not solve startup
or first-use hitches. Cache/prewarm/offline shader work remains on the roadmap.

The 16.67 ms working frame budget is still unmet. Prepared probes remain a
raster-filtered, per-view camera capture rather than placed/cached/blended
production probes. Box projection cannot retain the old geometry trace's
off-screen occlusion and arbitrary-surface parallax. Existing planar/screen
reflection hits still take priority. Keep the retained reference flag for review.

Evidence: WORKER_EVIDENCE_ROOT/phlosion-reflection-architecture-20261025 on gpu-worker,
prepared-r3-source-manifest.json, prepared-r3-build.json, prepared-r2-suite.json
(Vulkan controls), prepared-r3-suite.json (Direct3D controls),
prepared-qualified-{suite,editor,qualification}.json, prepared-controls.json
and prepared-r3-native-cross-api.json. Native prepared-r3 is the final Direct3D
suite; Vulkan's passed cases are in native-prepared, whose later failed D3D
prototype is explicitly superseded. All 57 frozen shader payloads match r2/r3.

## Final build and shared-consumer verification

The final integrated-source candidate is prepared-r4 (19 tracked files plus this
report and the game roadmap). All 57 generated shader payloads match prepared-r3.
The final wet controls reproduced Vulkan 70.161 ms and Direct3D 52.796 ms over
70 valid samples; changes versus the reviewed prepared build were at most one
colour step with identical weather/gameplay state. Both final Cyberpunk editor
images match the reviewed prepared editor images exactly, with identical state.
These shorter controls confirm the full-case measurements above; they do not
replace their 200-sample results.

The game environment and outdoor-profile CPU tests also pass. The environment
test now explicitly covers both the six-face prepared game policy and twelve-face
reference policy. All 36 native screen-reflection checks pass across the two APIs,
including thin geometry and source/contact-depth reuse comparisons.

An unchanged Autochess checkout at 672f09b281a3738e4ce869f9ae9af3588aaaff58 was
built against this engine. Its two native material cohorts per API and all
17 editor material cases per API pass: four native and 34 editor captures, with
explicit ASUS device selection in all 38 logs. Representative editor captures
were inspected. This is focused material/shared-renderer qualification, not a
complete Autochess release test: the worker fixture contains the selected models
and scene/UI content, not the entire optional preload/audio catalog.

That qualification exposed a pre-existing Direct3D debug-drawing bug: the 8-bit
render-target pipeline was also used while the HDR scene target was active.
The final build selects a matching HDR pipeline. The four native before/after
images are pixel-identical, while 332 format-mismatch validation errors fall to
zero. All three edited legacy debug implementation files matched the original
controller baseline before this correction. The extra pipeline's driver memory
was not quantified; the 544 KiB figure above describes only the upload-ring delta.

The first Autochess cold run exceeded the old 180-second capture timeout. With
a 1200-second startup allowance it completed all four native captures in
1007.9 seconds; the final warm run took 62.4 seconds. This reinforces the remaining
shader-startup problem, but is not a matched old/new cold-start regression
measurement. Task harness timeout extensions did not change image/content gates.
Editor fixture junctions were replaced with real in-project files after path
containment correctly rejected the external target. The final Cyberpunk retry
supplied its missing task project file. Failed-attempt records are retained.

Final evidence includes prepared-r4-{source-manifest,build,primary,qualification}.json,
prepared-final2-editor.json, final-regression-report.json, extra-regressions.json,
debug-hdr-{before,validation}.json, autochess-qualification.json, and the native/editor
reports in autochess-native-r4 and autochess-editor-r4. Windows Vulkan and Direct3D
were qualified; Linux was not exercised during this Windows worker session.
Only reviewed source and compact evidence return to controller. No local build,
GPU test, runtime binary replacement, commit or push is part of this handoff.

## Remaining diagnostic cost

These effects-off runs are diagnosis only; production keeps both effects enabled.
Their differences interact and must not be added as independent pass costs.

| Prepared Vulkan control | GPU median ms |
| --- | ---: |
| local-off | 65.856 |
| screen-off | 30.214 |
| both-off | 26.126 |

Screen reflections, redundant source rendering, and the remaining lighting/
haze/rain cost require further work. No retail installation or FPS measurement is
available on this worker. Source architecture alone cannot prove performance
parity with the retail game.


## Initial diagnostics before the prepared-map replacement

This extends the rejected rough-probe prefilter trial. It does not establish
retail Cyberpunk performance parity. The accepted runtime baseline is engine
db3c232434c6d61887f9db71f8a6d9ffc2c0bb5f / game
1c717aac9d985cc7c979d22fa2f2f6abe1d722b6.

## Conditions and evidence

All editing, builds and GPU work ran on gpu-worker, Windows 11, ASUS RTX 2060 SUPER,
LUID 000000000000ad83, driver 610.88. Resolution 1920x1080, fixed wet-night camera,
accepted Hybrid rain/material settings, 100 frames per case, median of 70 valid
GPU samples (frames 30–99). These are diagnostic GPU timings, not presented FPS.
Median busy clocks were 1920–1935 MHz, with no thermal-slowdown flags. No cooling,
driver, power, clock, quality, sample-count or game-default changes were made.

Worker evidence root: WORKER_EVIDENCE_ROOT/phlosion-reflection-architecture-20261025.
Manifests retain hashes, settings, logs, images, weather state and telemetry.
The source baseline includes all 1,489 controller task files and prior untracked
documents. Raw third-party source is not part of any patch or runtime dependency.

## What dominates this view

Uninstrumented Vulkan controls (effects disabled only for diagnosis):

| Control | GPU median ms |
| --- | ---: |
| Accepted renderer | 123.478 |
| Local reflections disabled | 66.194 |
| Screen reflections disabled | 64.287 |
| Both disabled | 26.259 |
| Accepted renderer, repeat | 123.786 |

Disabling an effect changes downstream work; these differences are not independent
costs that can be added together. They establish reflections as the first target.
Even with both disabled, the scene exceeds the 16.67 ms working budget.

A separate temporary profiler isolated the local visibility search, local GGX
filter and screen trace without removing their source captures:

| Diagnostic | GPU median ms |
| --- | ---: |
| Instrumented normal / repeat | 127.036 / 127.270 |
| Local visibility search skipped | 87.647 |
| Local 64-tap filter skipped | 109.474 |
| Both local receiver calculations skipped | 70.732 |
| Screen ray search skipped; capture retained | 98.100 |

The local visibility search is a larger target than the local filter alone.
Both source-colour rendering and final rendering pay these local calculations.
The instrumented normal source interval was 35.659 ms and final-scene interval
72.143 ms; skipping both local receiver calculations reduced them to 9.482 and
46.784 ms. Phase medians are independent and do not sum to a precise total.
The diagnostic instrumented code was removed after retaining its source/evidence.

## Source architecture implications

The inspected research snapshot prepares rough probe radiance before receiver
shading, then uses box projection and filtered texture lookups. Its inspected
probe fallback has no equivalent of Phlosion's per-receiver 128-step geometry
search. Its screen-reflection path reuses previous-frame scene colour, rather
than redrawing the opaque scene for source colour every frame.

These are mechanism observations, not proof that a copied algorithm will retain
our accepted occlusion, parallax and moving-camera behavior. The snapshot's guide
is dated June 2021 and differs from retained retail captures. Primary source
paths/hashes are recorded in Cyberpunk2077Research's
lighting/reflections/SOURCE_ARCHITECTURE_REVIEW.md.

The architectural priorities are therefore:
1. Avoid fallback evaluation when a stronger reflection source fully replaces it.
2. Avoid repeated same-frame local reflection work in source and final rendering.
3. Move rough-lobe integration out of material shading, with a dedicated kernel
   and complete footprint coverage; qualify visibility and source-width behavior.
4. Introduce previous-frame colour reuse only with motion, disocclusion, exposure,
   camera-cut and scene-reset contracts. A still image cannot qualify that path.
5. Measure and address the remaining direct-lighting, haze and rain budget.

## Rejected empty-region traversal

An experimental conservative 8x8 occupancy hierarchy reused unused alpha in the
existing probe colour pyramid. It skipped only original ray samples inside an
empty region; it did not change sample placement in occupied regions.

| Path | Vulkan ms | D3D12 ms |
| --- | ---: | ---: |
| Accepted | 122.941 | 115.710 |
| Prototype, skipping disabled | 129.140 | 121.624 |
| Prototype, skipping enabled | 142.737 | 133.382 |

Both APIs regressed, so the experiment is rejected. It passed the unchanged
one-colour-step full-scene bound and exact weather-state comparison; 24 native
executions across both APIs produced identical paired images. Core/synchronization
and D3D12 debug checks were clean. A separate randomized geometric check covered
480,000 rays and 7,060,845 skipped sample positions without escaping the intended
face/tile. Correctness alone does not make this worthwhile.

The first full-scene prototype had a startup access violation. Restoring profiler
headers with old modification times left stale objects in an incremental build;
touching restored inputs and rebuilding resolved it. The failed run and Windows
application event remain in evidence. No display-driver reset was recorded.
The corrected runs above are in tiles-r2-suite.json. The rejected patch is retained
at the evidence root as rejected-empty-tiles.patch; it is not production source.

## Subsequent experiments and revised target

The demand-driven ordering prototype was rejected: Vulkan 128.598 ms versus
122.941 ms; Direct3D 115.289 versus 115.710 ms was effectively flat. Same-frame
colour reuse was also rejected: it borrowed wrong colour at the front concrete
ledge (11 pixels over the one-step bound, max 22) and slowed Vulkan to about
140 ms. Tightening the depth agreement did not fix it. Neither is production code.

Source-pass specialization improved Direct3D but regressed Vulkan. Only Direct3D
ordinary PBR mode 2 was retained for further qualification. Full controls and
candidate medians were wet 116.981 -> 97.325 ms, dry 51.849 -> 50.453, and
orbit 134.074 -> 113.362, with no thermal flags. Battle had one thermal sample
in each run and did not establish a gain (109.659 -> 116.259); do not advertise
a universal speedup. All four images stayed within one colour step, with exact
weather-state equality. The corrected editor fixture gave identical paired
images and state. Native specialized/unspecialized tests passed.

On 2026-10-25 the user explicitly authorized architectural replacements with
intentional appearance changes where closer source behavior improves cost.
Retail is absent on gpu-worker; they prefer source-guided implementation, not
compiling the research game or requiring a retail benchmark. The old pixel bound
still applies to equivalent changes; new algorithms get independent visual
contracts and explicit limitations.

## Prepared-radiance candidate

The candidate captures six colour faces, omits the six geometry faces, prepares
GGX roughness levels once, and performs box-projected filtered lookups at the
receiver. It removes the old 128-step visibility trace and 64-sample convolution
from prepared receivers. The same 4*resolution square atlas is reused. Direct3D
adds 544 KiB to its bounded upload ring as described above. Geometry-traced mode remains the default engine/reference policy;
PokemonCyberpunk explicitly selects prepared radiance in field and battle.
PHLOSION_DEPTH_TRACED_LOCAL_REFLECTIONS=1 restores its old sampling policy.

This is not the complete inspected production architecture: filtering uses
raster draws/copies, the capture follows the camera and updates each view, and
the game has one projection volume. Prepared fallback has approximate parallax
and no geometry visibility test. Existing planar/screen reflections still take
priority. Placement/blending, caching, dedicated filtering and temporal SSR
remain separate work.

The first Direct3D candidate overwrote filter output with normals in its final
pixel-output wrapper; the roughness oracle caught it. The corrected guards keep
filter colours. Vulkan's 17 native cases and the corrected Direct3D 17 cases
pass, including the original traced occlusion/handoff checks. Twelve prepared
native scenes agree across APIs within one colour step. Five CPU contracts pass.

The initial editor harness failures were fixture path errors (old task root,
absolute project/material paths and unresolved vertex-adapter paths), not game
renderer failures. Retained failed manifests document them. The portable fixture
now uses the common task parent as its project root; no project validation was
weakened. Native/image gates were not loosened to accept the Direct3D filter bug.

Full-scene qualification completed with prepared-r3; final prepared-r4 runtime,
editor and shared-consumer checks are recorded at the top of this report.
The reviewed source delta is ready for controller integration.
The 1080p/60 FPS working budget is not yet achieved. Source architecture cannot
by itself establish an FPS comparison to retail, and the artistic 90% checkpoint
is not a rendering-performance score.
