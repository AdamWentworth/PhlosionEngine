# Flat wet-surface scene reflections

Work started after the 2026-10-17 foundation commits (engine 47a2cab,
PokemonCyberpunk 3dfe5a7). Generic rendering stays in the engine; the game selects
the forecourt receiver plane and submits the same scene/actor poses in field and
battle. No source assets, light strengths, rain style or weather clocks change.
The pass requires HDR scene radiance; the game's legacy-mapping diagnostic
(`PHLOSION_DISABLE_SCENE_RADIANCE`) disables this dependent reflection pass too.

## Contract

`IWorldPlanarReflection` captures a live, half-resolution reflected scene into
RGBA16F, after shadow maps and before the ordinary view. Both views reuse the
same shadow maps and diffuse transport. The reflected camera and VP mirror Y
about the selected plane; fragments below it are clipped, and reflected winding
is corrected before evaluating normals. Backend view overrides also cover native
model-provider draws. No precipitation, recursive water, battle trails or screen
effects are included in the reflected view.

The completed HDR capture is copied to a separate sampled image. Color and depth
are cleared before the main view. Vulkan/D3D12 targets are bounded power-of-two
buckets per in-flight frame; immutable sampler descriptors avoid rewriting a set
already used by a recorded draw. Vulkan suspends its render pass for the copy.
OpenGL uses a separate texture and never samples its current framebuffer.
Every world-view boundary resets reflection availability; disabled consumers
retain their previous rendering. Editor surfaces use the same path as runtime.

Only wet, approximately horizontal receivers near the plane use the capture.
Roughness fades it out between 0.25 and 0.55; rough sand keeps its existing
response. Small water normal perturbations disturb the projected image. Nine
bilinear samples form a normalized 2D Gaussian with an aspect-correct footprint.
This approximates roughness blur; it is not GGX prefiltering.
The response replaces the corresponding environment-specular contribution and
uses the existing GGX split-sum approximation, dielectric reflectance and
specular scale. It does not add an unweighted colored image to the ground.

Capture includes emissive geometry. PokemonCyberpunk suppresses only the six
canopy fixture material emissions during capture because their analytic area
sources already supply their highlights. Signs, screens and unrelated emission
remain, with live fixture gain. The main-view factors and reported fixture state
are kept separate from capture overrides. The bar's grouped point lights remain
art-directed proxies; this is not an exact energy-matched source decomposition.
Rain, droplets and other transparent effects are still excluded.

The reflected scene can contain objects outside the main camera. The reflection
view boundary blends back to existing ambient/neutral environment lighting;
that fallback is not a local reflection probe. The method supports one flat
receiver plane, not arbitrary curved/vertical reflectors, multiple water heights,
or a general SSR/probe/RT system. It is an engine choice suited to this location,
not a recovery of Cyberpunk 2077's SSR implementation.

## Verification of initial reflection checkpoint

Public native tests exercise an independently projected colored object, a fully
off-screen object, reversed winding, an opaque blocker, below-plane clipping,
dry/off controls, view reset and capture-extent changes. Existing HDR/bloom/blend
tests remain required. Native station exploration and battle must agree across
all three APIs and both hosts, with dry, source-off, oblique and reflection-off
controls. Review CPU/GPU frame cost against an otherwise identical off control;
no sustained-60-FPS or numerical Cyberpunk-parity claim follows from this pass.

Development evidence: engine and game `artifacts/lighting-foundation/reflections`.

### Native reflection and station checks

The projection CPU contract and all 27 native reflection cases pass. Eighteen
cross-API image comparisons pass, with maximum mean RGB error 0.015271/255.
All nine HDR/intensity/blend/bloom/feedback tests, the shared material-composition
contract, and five affected game environment/weather contracts pass. The final
Vulkan descriptor-binding change was followed by all nine Vulkan reflection cases
and material composition again.

Six wet station runtime/editor views pass the existing source/packet/content and
image limits. Maximum API MAE is 1.329873/255 (limit 1.5); maximum host MAE is
0.0000172/255. Six rainy battle views pass, with maximum API MAE 0.626537/255.
The seven existing source-off/puddle-off/bloom-off/dry/noon/oblique controls pass,
including independently projected light footprints and localized bloom. Seven new
controls isolate the reflected scene, with a precipitation-free pair to inspect
the character and geometry. They check matching scene/environment state, reflected
actor position, no change above the receiver horizon, and exact dry equality.
Front and oblique views change 32,126 and 48,024 pixels by more than three channel
values; dry and legacy-HDR-off control pairs are pixel-identical. No allocation
failures were reported.

Reproduce game checks with `Verify-Canopy-Neon.ps1 -Stage
matrix|battle|controls|reflections` and the station, battle, neon and
`verify_planar_reflections.py` validators listed in the game's TEST_PLAN.md.
Public tests are CTest `^PhlosionPlanarReflection` in engine `build-presentation`.
The final game-only change additionally disables reflections when the legacy
HDR-off diagnostic is selected; the normal HDR-enabled path is unchanged.

### Shared consumers and preserved inputs

Autochess's separate runtime rebuild and paired editor/plugin build pass, with a
source-bound paired-build verification after implementation. Six runtime cohort
captures and six editor body/eyes/stage captures pass the unchanged expected-content
and API limits. All twelve images match the prior diffuse-bounce checkpoint
pixel-for-pixel. The runtime SHA256 is
`101f24c05f60f8912441180d0004cfbf2a24d56be3153da7123c701ca97c2bf7`.
The first attempt failed in PowerShell 7's System.Drawing helper before capturing;
the unchanged repository tools passed under Windows PowerShell. That infrastructure
failure is retained separately. Runtime cold-compile timeout was 450 seconds,
without changing visual thresholds. First new Vulkan/D3D12 shader compiles still
took minutes; this pass does not resolve cold-start latency.

PokemonModelSupport and Autochess source remain clean at their checkpoint heads.
The canonical Blender SHA256 remains
`6015f9de3eb4228c1310f86462095051e9ebdc1d5236cfe8e1ae4df7b0d71918`;
the station GLB remains
`6a1c0e493007eced0bef5058dae033b06fd55ba05fd450f48294145630e5a007`.
The game runtime/plugin are rebuilt and all three staged game/model DLLs match
their build outputs. Shared evidence is under Autochess
`artifacts/lighting-foundation/reflections`; preservation and staging hashes are
under the matching game evidence root.

All three native editor scene-switch probes pass: five station/reference visits
per API, retaining the reference's independent content and clearing station light
and bounce data. The normal D3D12 launcher probe passes with the existing 120-second
day cycle, 180-second Hybrid rain sweep and CP audio selection. Probes are hidden
and finite; no interactive editor is left running by this work.

### Frame-cost review

Sequential matched 1920x1080 heavy-rain front views, with all shadows, HDR, bloom
and diffuse bounce retained, use 200 warm GPU and host samples (frames 30–229).
Hardware is RTX 2060 SUPER / driver 617.42; GPU 0 renders while GPU 1 remains idle.
Cyberpunk and other game/editor checks are closed during timing. This compares
enabled/disabled execution of the same current shader, not shader compilation
cost against an older binary. Do not compare these rainy front views with the
earlier clear under-canopy timings as if the reflection pass caused that difference.

The first implementation measured GPU off/on 31.964/36.726ms (GL),
19.352/23.938ms (Vulkan), and 17.560/20.762ms (D3D12). It redundantly drew the main
sky before clearing it for capture. The follow-up removes that draw in field and
battle, retaining one reflected sky and one main sky. All twelve final station
and battle viewports are pixel-identical to the first implementation across
APIs/hosts, and the five game contracts pass after rebuilding. Final appearance
and timing evidence is under `final/`.

| API | GPU off/on ms | Host off/on ms | Added GPU ms |
| --- | ---: | ---: | ---: |
| OpenGL | 31.881 / 36.565 | 34.316 / 38.917 | 4.684 |
| Vulkan | 20.844 / 24.421 | 66.737 / 74.446 | 3.577 |
| D3D12 | 17.489 / 20.512 | 18.165 / 20.435 | 3.023 |

All three exceed the unchanged +1.5ms GPU / +2ms host review guides. This is a
functionally verified visual pass, not a performance-budget or sustained-60-FPS
qualification. With reflections, GPU p99 is 40.496/29.060/21.996ms and host p99
is 53.935/106.361/40.948ms (GL/Vulkan/D3D12). Both timing sets remain recorded;
do not attribute every cross-run difference to removing the sky draw. The extra
scene submission remains the substantial cost. Reflection culling/batching and
receiver coverage are optimization targets before expanding the feature; Vulkan
uses ordinary draws during capture and retains its existing main-view path.

The final normal D3D12 launch passes, staged DLL hashes match, and all native
sessions have exited. GPU monitoring recorded GPU 0 reaching 99% / 1965MHz and
GPU 1 remaining idle. Source assets remain unchanged. The reflection work is
saved as the next authorized engine/game checkpoint after the foundation commits.

## Below-plane submission culling trial — not retained

The reflection checkpoint is engine `91f135a` and game `7aebbb7`. A subsequent
trial added an optional world-space half-space to the generic glTF draw adapter,
rejecting whole static PBR bounds below the reflected water plane. Intersections,
contact, numerical slack, missing/invalid bounds, skinning, custom vertex materials
and prewarm retained their prior paths. Invalid caller planes failed preflight.

The engine contracts and five game environment/weather contracts passed. All twelve
station/battle API/host viewports plus dry/oblique controls were pixel-identical
to the checkpoint, with unchanged scene state and existing content/parity limits.
Six matched 1080p timing images were also identical. No shader or source asset
changed. However, only one of the station GLB's 196 primitives was entirely below
the selected water height: the compacted forecourt.

Both controls below retain reflections, shadows, HDR, bloom, bounce and heavy
rain. They use 200 warm samples, frames 30-229, on the same RTX 2060 SUPER and
driver as the checkpoint. Runs were sequential with Cyberpunk closed. A timing
snapshot records GPU 0 at 1935MHz with no video decoding and GPU 1 idle.

| API | GPU culling off/on ms | Host culling off/on ms |
| --- | ---: | ---: |
| OpenGL | 36.700 / 37.082 | 38.790 / 39.235 |
| Vulkan | 24.043 / 23.762 | 75.222 / 74.312 |
| D3D12 | 20.412 / 20.386 | 20.188 / 20.241 |

The 0.026ms D3D12 difference and mixed API results do not establish a useful
speedup. The trial and its diagnostic toggle were removed; runtime sources return
to the reflection checkpoint. Candidate patches, CPU/native checks, exact image
comparisons and raw timing remain under engine/game
`artifacts/lighting-foundation/reflection-culling`. Do not claim this as an accepted
optimization or a higher Cyberpunk parity score. Profile the remaining capture
work before choosing a larger optimization. Emissive-sign coverage and roughness
filtering remain the next visual gaps; the existing frame-cost flags still apply.

## Reflected signage and roughness filtering — 2026-10-17 follow-up

This follow-up starts from engine 1130af5 / game 533eb44. The engine now captures
emissive radiance in both default and composed PBR materials. The game selects
the six analytic rectangle emitters for capture-only suppression through its
existing glTF emission overrides; visible factors and fixture telemetry remain
unchanged. Non-rectangle signs/screens and unrelated material emission survive.
There is no new provider ABI, scene asset, light intensity or weather setting.

The old five-tap cross is replaced by a normalized nine-bilinear-tap 2D Gaussian.
Radius grows with roughness squared; its horizontal scale accounts for capture
aspect, including D3D12/Vulkan padded buckets. Clear puddles stay sharp, rougher
water softens, and the existing rough-sand/dry rejection and DFG response remain.
This is a small spatial-filter improvement. It does not add mip prefiltering,
depth-dependent GGX convolution, temporal reconstruction, general SSR or probes.

The 49 selected engine tests pass: projection, 36 native reflection cases, nine
HDR/bloom/feedback cases, engine contracts, semantic boundary and material
composition. The native emissive test uses a textured E with all illumination
zero. Its reflection preserves the holes, disappears when emission is zero,
and leaves the visible sign identical between smooth/rough receivers. Independent
edge analysis measures 10–90% widths of 2.06–2.09px at roughness .06 and
2.57–2.60px at .4; maximum API MAE is .035525/255. Run
`tools/verify_planar_quality.py build-presentation/planar-reflection-tests
--output <report.json>` after CTest. Five game environment/weather tests and five
affected Autochess renderer/material CPU contracts also pass.

Evidence for this follow-up is under `artifacts/lighting-foundation/reflection-quality`
in engine, game and Autochess. Removing the emission helper initially broke
default shaders and an existing composed character material package; the latter
repeated compilation each frame and reached the 450-second capture timeout.
The final implementation retains `worldReflectionEmission` as an identity hook,
preserving both current and already-composed shader callers. This supersedes the
intermediate direct-return edits; default/material shader call sites remain
unchanged. The full 49 engine tests, native emission/filter oracle and five game
contracts pass again after rebuilding all hosts/providers. An Autochess link failed on its
OpenGLRenderBackend object (LNK1163); deleting that build object and rebuilding
resolved it without a source change. Initial failure logs are retained.

The six station and six battle API/host captures pass the existing content and
parity checks: maximum station API MAE 1.329876/255, host MAE .0000172/255,
and battle MAE .626544/255. All seven lighting controls and seven reflection
controls pass, including the independently projected actor, unchanged sky,
localized bloom and exact dry/legacy-HDR equality. Nineteen matched station,
battle and lighting-control images differ from the prior checkpoint by at most
.000607/255 mean error; dry and noon are pixel-identical. The front view remains
effectively unchanged because it mainly shows analytic canopy highlights and
clear puddles. This is a targeted coverage/filtering improvement, not a broad
lighting change or a new numerical Cyberpunk parity score.

Final shared-consumer verification passes after restoring the compatibility hook:
six Autochess runtime cohort images and six editor body/eyes/stage images all
match the preceding reflection checkpoint pixel-for-pixel. Expected-content and
API/host thresholds remain unchanged. The separate runtime and paired editor/
plugin builds are source-bound; PokemonModelSupport and Autochess source remain
clean. All 36 public reflection images also match the intermediate implementation
exactly after the hook restoration. These preservation reports live in engine
`public-compat-preservation.json` and Autochess `final/*-preservation.json` within
the follow-up evidence root. Cold shader startup remains slow (the first D3D12
cohort took several minutes); it is separate from warm-frame GPU measurements.

The final six station and six battle captures are pixel-identical to the
pre-compatibility follow-up captures. A separate oblique view uses an existing
forecourt puddle to show the bar lettering at a grazing angle; it does not alter
the main camera, puddle placement or source intensities. All six API/host signage
views pass (maximum MAE .782897/255). A D3D12 emission-on/off pair disables bloom
and keeps the point/rectangle illumination, fixture gains and weather identical:
691 pixels inside the independently projected authored puddle change by more
than two channel values. This isolates reflected glyph emission from the visible
sign's halo. A preliminary steep doorway-puddle view was faint; the final oblique
view and isolation evidence live under game `reflection-quality/final`.

Final sequential 1920x1080 heavy-wet-night controls on RTX 2060 SUPER, driver
617.42, retain shadows, HDR, bloom, bounce and rain. Each result uses 200 warm
frames (30-229), with the complete planar path toggled off/on:

| API | GPU off/on ms | Host off/on ms | Added GPU ms |
| --- | ---: | ---: | ---: |
| OpenGL | 32.889 / 37.988 | 35.232 / 40.113 | 5.099 |
| Vulkan | 21.274 / 24.588 | 66.423 / 77.518 | 3.314 |
| D3D12 | 16.982 / 20.166 | 17.023 / 20.175 | 3.184 |

All three exceed the unchanged +1.5ms GPU / +2ms host review guides. D3D12's
reflection-on GPU p99 is 21.155ms and host p99 40.583ms; this does not qualify
sustained 60 FPS. These toggles measure the whole reflection path, not the extra
four filter samples alone. Differences from the earlier checkpoint's separate
timing run do not establish a speedup. `final/reflection-performance.json` retains
the medians, tails and flags. The concurrent GPU monitor records no video decode,
GPU 0 reaching 97% / 1980MHz, and GPU 1 idle at 1MiB. Cyberpunk and other game/editor
processes were absent before these sequential timing runs.

Final editor scene switching passes on OpenGL, Vulkan and D3D12: five alternating
station/reference visits each, with stable resource counts and cleared station
fixtures/bounce in the Colosseum reference. The Blender source and derived station
GLB retain their recorded SHA-256 hashes. All three staged game/model DLLs match
the rebuilt providers. Scene-switch traces and staged-binary proof are in game
`reflection-quality/final`.

The final normal D3D12 editor launcher probe passes with the station and Hybrid
rain/CP audio selected. Default 120-second daylight and 180-second rain periods
are unchanged. All hidden verification processes exited. The new code is ready
for normal project launch; the performance flags above remain open.
