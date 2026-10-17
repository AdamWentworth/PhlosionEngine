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
response. Small water normal perturbations disturb the projected image. Five
filtered samples approximate roughness blur; this is not GGX prefiltering.
The response replaces the corresponding environment-specular contribution and
uses the existing GGX split-sum approximation, dielectric reflectance and
specular scale. It does not add an unweighted colored image to the ground.

Capture emission is omitted because the analytic local-light BRDF already
supplies source reflections. This avoids counting each canopy source twice while
adding reflected lit geometry. A consequence is that emissive signage without
a matching analytic source is not represented in this first scene-reflection
layer. Rain, droplets and other transparent effects are also excluded.

The reflected scene can contain objects outside the main camera. The reflection
view boundary blends back to existing ambient/neutral environment lighting;
that fallback is not a local reflection probe. The method supports one flat
receiver plane, not arbitrary curved/vertical reflectors, multiple water heights,
or a general SSR/probe/RT system. It is an engine choice suited to this location,
not a recovery of Cyberpunk 2077's SSR implementation.

## Verification

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
