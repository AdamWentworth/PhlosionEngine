# Lighting foundation checkpoint

2026-10-17. Fixture emission, local shadows, HDR, static shadow culling and colored
diffuse bounce are functionally verified, including native scene, shared-consumer
and normal-launch checks. Performance limitations remain below. Earlier phase
notes are historical. Checkpoint commits were authorized on 2026-10-17. Measured
component agreement below is not a whole-lighting-engine parity percentage.

## Current follow-up: recovered shadow moment filter

2026-10-18: The independent engine-owned distance-moment kernel passes the
captured first-frame comparison. Horizontal, vertical and chained outputs each
match all 2,097,152 RG16_UNORM components bit-exactly, including a control that
clears outputs before independent dispatch. Untouched layers and the restored
original shadow array are exact; later composition remains within the retained
two-ULP replay envelope. See [SHADOW_MOMENTS.md](SHADOW_MOMENTS.md) for coverage,
private reproducible commands and the addressing precision limitation.

Thirty-two public synthetic filtering/visibility cases pass on GL/Vulkan/D3D12;
the existing finite-light cases also pass using the shared native fixture runner.
The normal game remains on the accepted PCF shadow path. No plugin ABI, source
intensity, weather, scene or production rendering behavior changed in this step.
Next: live moment targets and filtering with radial receivers, API addressing
qualification, blocker controls and the established scene/shared-consumer checks.
This is measured filter agreement, not complete shadow-system or engine parity.

## Previous follow-up: live finite-source response

2026-10-17: The shared surface shaders now support an explicit finite-source
response on physical rectangle lights. PokemonCyberpunk selects it for the six
canopy fixtures; the engine default remains LTC and the game has an LTC review
override. The adapter uses rectangle area times radiance for normal intensity,
an inscribed line/sphere, one-sided angular emission and authored material F0.
Geometry, source colors, weather and normal launch cycles remain unchanged.
Model/editor ABI is now 18/38; all consumers require rebuilding and staging.

See [LIVE_FINITE_LIGHTS.md](LIVE_FINITE_LIGHTS.md) for the full contract and
acceptance record. The production guard replay retains the measured component
accuracy (0.000065974% diffuse / 0.000029806% specular aggregate L1), including
the same four/two shadow-edge outliers. All three native numerical/domain fixtures,
129 affected engine checks and six game CPU contracts pass. The six station,
six battle, seven day/dry/off/oblique, seven scene-reflection and four isolated
source captures pass their existing guards. All three scene-switch probes and
the normal project launch pass. Autochess's six runtime and six editor material
views pass with the accepted baseline. Its first PowerShell 7 verifier failure
and Vulkan cold-start timeout are retained; Windows PowerShell and a longer
artifact-only startup allowance resolve those harness limits without changing
image gates. Cold startup remains slow. Existing shadow filtering, bounce, haze
and reflection transport are not recovered Cyberpunk implementations.

2026-10-18 acceptance: paired 1080p wet-night GPU medians (LTC / finite) are
GL 46.147 / 42.882 ms, Vulkan 22.999 / 21.144 ms and D3D12 23.094 / 20.736 ms,
200 warm samples each. Recorded scene state differs only by the source-response
tags. These are full-scene observations, not isolated response timing or 60 FPS
qualification; Vulkan host median remains 54.717 ms. Live integration is accepted
with the declared startup/performance and captured shadow-edge limits. Next
measured parity target: shadow-map generation, projection and filtering.

## Previous follow-up: independent GPU local-light response

2026-10-17: `world_finite_light.glsl` independently implements the measured
finite-source distance, falloff, diffuse/specular response and moment visibility.
It is exercised by fixtures only; production material shaders do not include it.
No ABI, game source, staged runtime/editor binary, scene or launch default changed.

The private native replay runs the original retail PSO and the port against the
same captured inputs with directional RGB zeroed in both, then restores the
original constants and output resources before the captured frame continues.
Across all 873,728 active pixels, 99.98556% of the 5,242,368 RGB output channels
are identical. Aggregate relative L1 errors are 0.00006486% diffuse and
0.00002898% specular. Four diffuse pixels and two of the same specular pixels
exceed 2% relative error, with maximum 28.69% / 36.58%; all lie at shadow edges.
They remain an explicit limit, not removed from the aggregate or acceptance.
See [GPU_LOCAL_RESPONSE.md](GPU_LOCAL_RESPONSE.md) for coverage and interpretation.

Original local-light outputs are restored bit-exactly. Later composition differs
by at most two half-float ULPs, also observed in an ordinary no-port replay and
independent PIX export. The original strict preservation result remains
`requires-review`; a separate qualification records this measured replay
variation. GPU allocation addresses and tile order vary, while all 58,880 ordered
cluster light lists, dispatched tile sets and other inputs match the control.
All 122 original exported source files retain their recorded hashes.

Native OpenGL, Vulkan and D3D12 synthetic fixtures pass against independent
float64 reference values, 16 cases / 128 channels per API. They cover point,
finite line and sphere response, roughness, metallic response, back-facing
surfaces, radius-zero specular gating, falloff/cap/cutoff, moment visibility,
rotation and endpoint reversal. Maximum absolute errors: GL 1.15973e-6,
Vulkan/D3D12 7.47621e-7. Build warnings are errors. No performance measurement or
runtime/editor/shared-consumer qualification is implied by these isolated tests.

Private final case: `evidence/lighting-matched/gpu/local-o3-final-04/`, including
`case.json`, `qualification-v2.json` and source/binary hashes. The precision
experiment and initial strict failures remain retained. Engine evidence:
`artifacts/lighting-foundation/captured-local-gpu/`. Next: generic finite-source
contract, explicit canopy mapping and live material binding; preserve separate
LTC rectangles and require the existing scene/API/editor/shared-consumer checks.

## Previous follow-up: captured local-light CPU recovery

2026-10-17: User explicitly targets the actual light-to-surface calculation,
independent of station asset differences. Root-only cloud work continues; no
local model or delegated agent. Engine/game runtime source and launch defaults
are unchanged by this research milestone.

An owned hidden D3D12 replay now extracts the actual inputs and both HDR outputs
immediately around indirect dispatch parent 7084 (PSO 20727), with audited resource
transitions restored after readback. The original capture/export remains read-only.
The three previously unresolved CBVs were heap-relative addresses: b0 is resource
974, b12 is 956, b13 is 958; b6 is a 256-byte slice of upload resource 739.
GPU-written tile/cluster lists, 128-byte light records, G-buffer, shadow moments,
exposure and output textures are retained in the private research depot.

Current-GPU independent PIX composition export versus native replay differs in
246 of 3,686,400 half-float channels: 240 differ by one half-float ULP and six by
two. Maximum absolute difference is 0.000244141; aggregate relative L1 is
2.05148e-9. It is not bit-exact. The older GTX 1070 export has larger differences
and remains retained separately; do not silently substitute its qualification.

The readable CPU local-light model was compared at all 3,413 dispatched tile
centers, covering 53 light records and 13,478 evaluations (10,869 finite-size BRDF
evaluations, 566 with the shadow-enabled flag). With diagnostic captured surface
decoding and directional response retained, aggregate relative RGB L1 error
against the two captured GPU lobes is 0.04134%, maximum absolute HDR error
0.001953125. The independent local contributions versus diagnostic decoded
contributions differ by 0.003778% aggregate L1. These are defined component
measurements, not whole-engine parity or a production GPU port. Profile-texture
branches are present but no sampled light has a nonzero profile index.

A second deterministic sample uses a different pixel offset within each tile:
3,413 pixels, 54 light records, 13,543 evaluations, including 113 evaluations
actually attenuated by shadows. Aggregate RGB L1 error against GPU output is
0.044383%, maximum absolute HDR error 0.00390625; isolated local contributions
versus decoded execution differ by 0.001208% aggregate L1. No color/intensity
fitting was performed. CPU float32 interpretation, GPU optimization, half-float
storage and texture-filter precision remain numerical qualifications. Only this
captured shader permutation/frame is covered; the profile-texture path and other
lighting passes are not established by these results.

Evidence: private `evidence/lighting-matched/readbacks/baseline-01/` contains
`capture.json`, `current-gpu-qualification.json`, `local-model-comparison.json`
and the readbacks. `tools/lighting_matched/prepare.py`, `run_capture.py`,
`inspect_dispatch.py`, `light_model.py` and `compare_model.py` reproduce the work.
The fixed-seed offset validation and detailed handoff are recorded alongside it.
The GPU fixture described above completes the next recovery step. Production
integration and its runtime/editor/API/shared-consumer checks remain next.

## Previous follow-up: visible local-light scattering (accepted)

The repeated "75%" lighting estimate is withdrawn. It had no defined denominator
or measured image comparison. Historical percentages below are superseded, not
acceptance criteria. The last two passes improved frame cost without changing
the images; they must not be reported as lighting fidelity progress.

This pass targets the missing colored illumination of air around the canopy.
The retained retail night screenshot shows luminous haze around local sources;
Phlosion's matched baseline has colored surfaces but almost unlit air. The
retail screenshot and the earlier canopy concept establish the visual direction,
not a pixel-match target: geometry, weather and exposure differ. The retained
diagnostic composition PNGs use a non-retail transform and cannot establish color
parity. Capture shader types include `SVolFogConstants`; no exact Cyberpunk fog
algorithm or parameter match is claimed.

Acceptance: matched before/on/off native images, visible colored air with bloom
disabled, opaque foreground and source occlusion, complete rectangle extent,
RGB addition, zero-density/source-off preservation, view/sky continuity, and
all three APIs in runtime/editor. Keep source geometry, direct light strengths,
weather clocks and rain effects unchanged. Independent numerical area/volume
integration checks the new shader in a public fixture. Record the actual visual
result and cost after validation; do not replace the withdrawn number.

Implemented an opt-in rectangular-source integral in the shared height medium,
with source visibility from the local shadow atlas. The station uses scattering
albedo 0.55 and anisotropy 0.25. Ordinary materials retain extinction and defer
scattering to a quarter-width/quarter-height HDR pass. Depth-aware composition
protects foreground silhouettes; reflected and main views have separate view
integrals. Model plugin ABI is 17 and editor project ABI is 37; consumers require
rebuilt DLLs. The authored scene and source intensities are unchanged.

The isolated air captures disable falling rain and bloom. The haze-off image is
pixel-identical to the prior checkpoint. Open air under the canopy gains blue;
air outside the fascia gains pink, while the foreground remains sharp. Across
all APIs, the reduced pass differs from the retained full-resolution reference
by approximately 0.10/255 mean absolute error, with 99th percentile 1/255. This
compares two Phlosion implementations, not Phlosion against Cyberpunk.

The first full-resolution implementation was rejected for cost: D3D12 added
46.699 ms at 1080p. The first reduced pass still added 12.529 ms because the
compiler evaluated its expensive fallback. An explicit branch preserves the
image exactly and skips that work for depth-matched pixels. Paired heavy-wet-night
timings use 200 warm GPU samples on an RTX 2060 SUPER, with CPU builds and other
GPU jobs stopped. Final reduced-pass medians (off/on) are 20.578/22.563 ms on
D3D12, 20.353/24.688 ms on Vulkan and 39.661/47.006 ms on OpenGL. These are added
feature costs, not speedups or 60 FPS qualification. Host tails remain material.
Vulkan's earlier synchronization pair was 20.555/24.678 ms; the final repeat also
includes the rain-only specialization guard. Final D3D12/Vulkan/OpenGL GPU p99 values are
25.450/28.429/86.688 ms, and host medians are 23.413/59.932/49.369 ms. Ordinary
desktop applications remained open; this is a paired development measurement.
Cold compilation is also substantial: the first shared Autochess D3D12 capture
process took about six minutes, dominated by building previously uncached material
variants. This is not an isolated compiler benchmark or a warm launch measurement.
Completed source/entry-point/target/compiler-flag-keyed DXBC blobs were reused for
the editor checks after checking their header length and SHA-256 copy equality;
no existing cache entries were overwritten. See `shared-shader-cache-reuse.json`.
The first shared Vulkan asset-preview check exceeded its unchanged 240-second
limit during cold material preparation. Preserve that failure under Autochess
`editor-cold-timeout`. A separate shader-preparation diagnostic completed and
produced the native preview in 72.3 seconds; all six original editor checks then
passed with their timeout and image/content thresholds unchanged. This does not
qualify cold-start performance.
The raw rejected trials remain in `cost` and `reduced-cost`. `branch-cost` keeps
the initial optimized runs; `pre-volume-guard-cost` retains the prior Vulkan pair.
`final-cost` retains the final Vulkan repeat and exact
copies of the unchanged D3D12/OpenGL runs, with provenance recorded alongside them.

Completed checks: 127 engine contracts/native probes pass, including all 76 haze
checks and the existing planar/HDR/material/ownership checks. The independent
emissive-reflection oracle passes. Five game environment/rain/weather contracts
pass. All 36 station/air/battle/control/signage captures pass their existing
expected-content and API/host thresholds. All twelve shared character images
(six runtime images and six editor model crops) exactly preserve the previous
checkpoint, with no Autochess or PokemonModelSupport source changes. Both editor
build directories and the staged game/model plugins have been rebuilt.
The final Vulkan rain-only specialization excludes the haze dispatcher, allowing
unused integration code to be discarded. All 40 affected Vulkan haze, planar and
HDR probes pass again. Eight further shared runtime/editor captures (GL/Vulkan)
pass unchanged thresholds and exactly preserve their baseline images; D3D12 is
unchanged by this last shader condition. Six sky-only API/host captures pass.
Five same-session station/reference visits pass on each API, preserving resource
stability and clearing station lighting in the reference scene. The 120-second
day and 180-second Hybrid weather defaults remain intact. The final five Vulkan
field/battle/timing images exactly preserve their earlier counterparts. The normal
D3D12 project-launch probe passes with Hybrid rain and the CP audio selection.
The visual change is accepted with the performance and cold-start limits above.

Limits: bounded single scattering; emitter-centre phase/source attenuation;
the existing shadow proxy instead of source-sized volumetric penumbrae; no
scattering from point/sign lights, temporal volume history or multiple scattering.
The separate rain billow/drop simulation is unchanged. General reflections,
dynamic indirect light and softer area-light shadows remain visual gaps.

Evidence roots are `artifacts/lighting-foundation/local-haze` in engine, game and
Autochess. Game `accepted` contains the reduced-pass native matrix; `final` retains
the earlier full-resolution reference. Reproduce with `Verify-Local-Haze.ps1`,
then `verify_local_haze.py accepted --baseline before --reference-root final
--cost-root final-cost` using full paths beneath that evidence root. Follow with
the existing station rectangles, battle, canopy and signage validators. Engine
`PhlosionLocalHaze*` probes compare independent numerical area/view quadrature,
foreground depth, source occlusion and target reuse across all APIs. Exact model
and editor compatibility proof is retained in Autochess `paired-build-current`.

## Previous follow-up: Vulkan submission (performance only)

Starting from engine ebe4d1b / game b23efa5, profile current-thread CPU time and
repeated Vulkan submission work. Material lookup was the main measured cost;
locale-heavy string-stream texture-key construction is replaced with `to_chars`
and direct string appends. Cache identities, lifetimes, resource scopes, GPU
commands, lighting and scene content remain unchanged. Temporary profiling is
removed from the production code.

Matched 1080p heavy-wet-night host median falls 73.569 to 49.943ms (32.1%) on
Vulkan. A diagnostic repeat reduces mean current-thread CPU time from 50.078 to
30.156ms. Timing images and scene state remain pixel-identical on all APIs, with
unchanged warm resource totals. GL/D3D12 keep their existing implementation and
show only small run variation; the default D3D12 launch gains no new speedup.
Vulkan host p99 remains 76.150ms, so 60 FPS is still unqualified. GPU duration also
falls, but shader work is unchanged and clock coverage is incomplete.

56 engine checks and five game contracts pass. All 36 public reflection images,
27 station/battle/control/signage images and twelve shared character images are
unchanged. Twelve native shadow probes, all API/host content/parity checks and
normal D3D12 launch pass. Both engine editor builds and staged game/model plugins
are current. Blender/GLB hashes and 120s daylight / 180s Hybrid rain defaults are
unchanged; ModelSupport and Autochess source remain untouched. The optimization
is accepted for the next engine/game checkpoint. See [SUBMISSION.md](SUBMISSION.md) and each repository's
`artifacts/lighting-foundation/shadow-submission` for the exact method, diagnostics,
temporary profiler, matched controls and verification evidence.

## Previous follow-up: frame cost

Starting from engine fa7ae72 / game 45f42ca, the lighting shader checks local
shadow visibility before LTC integration on Vulkan/D3D12 and skips only exactly
zero contributions. OpenGL retains its previous ordering after an inconsistent
trial. Light strengths, shadow quality, scene content and material ABI are unchanged.
Game tracing now separates field/battle reflection host work; it remains opt-in.

Matched heavy-wet-night 1080p images are pixel-identical on all three APIs. Final
GPU control/candidate medians are 38.301/37.909ms GL, 24.154/22.264ms Vulkan and
20.257/19.893ms D3D12. GL's small difference is variation, not an optimization
claim. Earlier Vulkan savings were smaller; D3D12's final saving is .364ms (1.8%).
Host tails and repeated shadow/submission costs remain substantial. This is not
60 FPS qualification or a new numerical Cyberpunk parity claim.

55 engine checks, five game contracts and all 36 public reflection-image equality
checks pass. All 27 field/battle/control/signage images exactly preserve the prior
checkpoint, and all twelve native shadow probes pass. Normal launch and shared
runtime/editor material checks pass; all twelve shared images preserve the prior
checkpoint exactly. Both editor build directories and staged game/model plugins
are current. Source Blender/GLB hashes and 120s daylight / 180s Hybrid weather
defaults are unchanged; ModelSupport and Autochess source remain untouched.
The small optimization is accepted and saved as the next engine/game checkpoint.
See [FRAME_COST.md](FRAME_COST.md)
and each repository's `artifacts/lighting-foundation/frame-cost` for the exact
inputs, rejected/outlier evidence, raw timing and final verification status.

## Previous follow-up: reflected emission and roughness

The `reflection-quality` follow-up retains sign/screen emission in planar captures,
with game-owned capture overrides suppressing only the six analytic canopy
emitters. A normalized aspect-correct nine-tap Gaussian replaces the five-tap
cross. The existing shader emission hook is retained for composed-material
compatibility. No light strengths, authored assets, weather or provider ABI change.

The final 49 engine checks and five game contracts pass. All twelve shared
Autochess runtime/editor images and all 36 public reflection images preserve
their respective baselines exactly. Station, battle and oblique signage pass
all three APIs in runtime/editor at unchanged thresholds. The bloom-disabled
sign-emission control changes 691 pixels in an independently projected authored
puddle, with other lighting held constant. Same-session editor switching passes
five station/reference visits on each API, with stable resources and no stale
station light/bounce state in the Colosseum reference.
The normal D3D12 editor launch also passes with Hybrid rain and CP audio;
the 120s day / 180s rain defaults and source Blender/GLB hashes are unchanged.

Functional verification passes; performance remains flagged. Final 1080p heavy-rain
GPU off/on medians are 32.889/37.988ms GL, 21.274/24.588ms Vulkan, and
16.982/20.166ms D3D12. Every API exceeds the existing incremental review guides;
this is not sustained-60-FPS qualification. See REFLECTIONS.md and the engine/game/
Autochess `artifacts/lighting-foundation/reflection-quality` roots for controls,
tails and cold-start limitations. Broader reflection coverage, proper roughness
prefiltering and submission cost remain open. The approximately 75% foundation
estimate remains qualitative; it is not measured Cyberpunk parity.

## Earlier foundation and reflection checkpoints (historical)

The authorized foundation checkpoint is committed: engine `ef447b4` / `47a2cab`,
PokemonModelSupport `e4039c9`, Autochess `eef446d1`, and PokemonCyberpunk
`6fcfbb2` / `3dfe5a7`. Game dependency pins match those shared-source revisions.
No pushes were requested or performed.

The first reflection pass added live scene reflections on flat wet forecourt receivers.
It is functionally verified in all three APIs and both hosts. Its matched heavy-rain
GPU cost is +4.68/+3.58/+3.02ms (GL/Vulkan/D3D12), above the existing guide;
performance optimization remains required. The redundant initial sky draw is
removed, with all twelve final field/battle images preserved pixel-for-pixel.
See [REFLECTIONS.md](REFLECTIONS.md) for its contract, native acceptance and cost.
It is saved as the next authorized engine/game checkpoint; it does not change
PokemonModelSupport, Autochess source, authored assets, rain or the six light
strengths. Broader SSR/probe/RT coverage and emissive-sign reflections were still open
at that checkpoint; the current follow-up above addresses the latter.

The subsequent trial targeted reflected-scene submission cost before expanding coverage.
Preserve the current appearance, compare matched on/off timing, and retain the
existing performance guides. The roughly 75% foundation estimate discussed with
the user is qualitative feature coverage, not measured Cyberpunk image parity.
The below-plane static-culling trial was visually exact but did not establish a
useful speedup, so its runtime changes were removed. REFLECTIONS.md retains the
measurements and rejected-candidate evidence; the frame-cost flags remain open.

## Colored diffuse bounce — previous verified pass

The engine bakes one diffuse bounce from static glTF geometry into 32 directional
irradiance probes beneath the canopy. Six independent unit-source bases are
relit from live pink/blue fixture radiance; source-off gives exactly zero bounce.
Characters and shaded surfaces receive the result. Generic bake, math and all
three backend bindings belong to the engine; volume/source selection belongs to
PokemonCyberpunk. No new fill lights or authored asset changes. Moving objects
do not update transport, and this is not full GI or scene reflections.

Four engine CPU contracts, five game contracts, nine native HDR tests and 64
public/station/wet/battle/control captures pass with unchanged limits. Twelve
Autochess runtime/editor material captures pass; five editor images match the
earlier HDR baseline exactly, and the sixth differs by one channel byte. The
standalone Autochess executable was rebuilt explicitly; an earlier stale-binary
attempt was rejected. Scene switching passes on all APIs, five visits each,
including clearing bounce in the Colosseum reference. The final normal D3D12
launch passes and retains the 120s day / 180s Hybrid rain cycles and CP audio.

Fresh 1080p clear-night timing on RTX 2060 SUPER, Cyberpunk closed, 200 warm
samples per control, all shadows/HDR/bloom retained:

| API | GPU bounce off/on ms | Host off/on ms |
| --- | ---: | ---: |
| OpenGL | 26.336 / 27.602 | 28.636 / 29.841 |
| Vulkan | 14.666 / 14.970 | 49.956 / 48.739 |
| D3D12 | 10.363 / 11.931 | 10.689 / 11.939 |

D3D12 reverse-order repeat: GPU 10.474 / 11.683ms, host 10.773 / 11.962ms.
Its added GPU cost spans 1.21-1.57ms, with the first result slightly above the
existing +1.5ms review guide; both results remain recorded. No threshold change.
These medians do not establish sustained 60 FPS: host p99 exceeds 16.667ms on
every API, with GL throughput and Vulkan CPU-side cost still needing work. The
toggle measures current-shader execution, not total cost versus a pre-change
binary. The machine changed from GTX 1070 to two RTX 2060 SUPER cards; do not
compare those generations as an optimization result. Only GPU 0 rendered.

Vulkan now reuses and persists a bounded, device/driver-validated pipeline cache.
CPU corruption/compatibility contracts and native load/store checks pass. Warm
normal editor launches were about 34s in retained logs; first material compiles
can still take minutes. This pass used cloud execution only, without local AI
or delegation. See [BOUNCE.md](BOUNCE.md) for architecture, exact limitations,
startup evidence and game `artifacts/lighting-foundation/bounce/rtx2060` reports.

The preservation snapshot has 422 files: 387 unchanged, 35 intentional engine/game
overlaps and 14 new files. Model-support and Autochess source are unchanged.
Canonical Blender and private station.glb hashes remain unchanged. Binaries are
rebuilt/staged and this work is checkpointed at the user's request. At that
checkpoint the wet highlights represented only light sources. The subsequent
scene-reflection work is recorded above and in REFLECTIONS.md.

## Static shadow culling — previous accepted pass

Generic glTF import now computes primitive bounds from decoded positions. The
draw adapter optionally rejects static PBR primitives wholly outside the supplied
pass frustum, before material/backend submission. Field and battle supply each
actual shadow matrix for the environment only. Skinning, custom vertex materials,
prewarm and absent/invalid bounds retain their existing paths. Shaders, light
settings, source geometry, rain and camera behavior are unchanged. No new workers;
root continues under the user's recorded implementation fallback approval.

Four engine CPU tests (including the expanded import/draw contract) and five game
environment contracts pass. All 19 station/battle/control images are pixel-identical
to the accepted HDR baseline; six low-sun API/host captures and the culling-off
control pass. Existing source/packet, battle, wet reflection and bloom validators
pass. Scene switching passes on all three APIs (five visits each); the final normal
D3D12 launch passes and restores the station, 120s day cycle and 180s Hybrid weather
cycle. Engine `build-presentation`, game runtime/editor and model provider targets
were rebuilt and staged together. No shader/backend change required repeating the
previous Autochess material matrix; its separate paired binaries were not replaced.

Matched clear-night 1080p tests retain HDR, bloom and all shadows, 200 warm samples:

| API | GPU off/on culling ms | Host frame off/on culling ms |
| --- | ---: | ---: |
| OpenGL | 41.115 / 36.573 | 42.801 / 38.233 |
| Vulkan | 23.194 / 22.778 | 63.291 / 48.456 |
| D3D12 | 21.721 / 12.701 | 21.982 / 12.943 |

Reverse-order D3D12 repeats at GPU 21.632 / 13.280 and host 21.939 / 13.872 ms.
GL/Vulkan performance images are identical; D3D12 has maximum one-byte difference.
These are medians, not sustained 60 FPS or rain/battle guarantees. Frame spikes and
Vulkan host cost remain. Host timings include waits. See SHADOW_CULLING.md and game
`artifacts/lighting-foundation/shadow-culling` for exact evidence and limitations.

Before-state snapshot includes 117 files across four repositories (dirty work and
affected clean inputs). 106 are byte-identical; the eleven intentional changes are
five engine glTF/test files, game field/battle presentation, its capture helper and
test plan, and this checkpoint/PLAN. Earlier lighting/material/model edits remain
intact. New files are the culling design and two game verification tools. All
changes remain uncommitted. Next visual work is colored indirect light and actual
scene reflections; further Vulkan performance investigation remains worthwhile.

## Current changes

PokemonCyberpunk `tools/outskirt_station_lighting.py` rotates pink physical
diffusers/channels/clips to emit outward and 60 degrees down, below the fascia.
All six source colors/radiance/emission/condition settings are preserved, with
no added lights. Blue and non-canopy fixtures, point lights, shelters and puddles
match the retained baseline. The normal private station export was regenerated;
canonical Blender source hash is unchanged, 196 meshes / four pumps / four columns.
`verify_station_rect_sources.py` now checks visible normals, corner joins,
18 independent interior area-integral samples at two heights, exterior receiver
orientations and a zero-direct-light back-facing control. New
`Verify-Canopy-Overlap.ps1` captures four isolated source modes under the canopy.
Game HANDOFF, TEST_PLAN and EMISSION_PASS document this step.

No engine rendering code or ABI was changed in this phase. All 52 saved pre-existing
modified/untracked files across the four repositories still matched their baseline
copies at review. The earlier material ownership cleanup remains uncommitted.

## Verification complete

- Exported source geometry/normals/extent/corner oracle passed (maximum centreline
  corner error 0.000001404 m). All 18 interior and external orientation checks pass.
- OutdoorProfile and WorldEnvironment CTests pass, two of two.
- All six wet-night station runtime/editor captures passed the existing source
  packet, fixture, allocation and parity oracle. Maximum API MAE 1.36518/255
  (limit 1.5); maximum host MAE 0.00000343/255.
- All seven D3D12 controls completed. Existing analytic reflection, puddle/dry
  state and localized-bloom checks passed at front and oblique camera angles.
- All six rainy battle runtime/editor captures passed the existing geometry,
  fixture, weather, shelter, allocation and backend parity checks.
- All four under-canopy source controls passed matching player/camera/import
  state, dry surfaces, zero point radiance and expected rectangle counts. The
  reviewed character interior has 1,303 pixels responding to each source group
  independently out of 2,408 sampled pixels. The close camera retracts through
  the established collision behavior; no new camera behavior is claimed.
- Visually reviewed source-off, pink-only, blue-only, both, dry/oblique and battle
  views. Final total: 23 native captures and two CPU contracts, all passed.
  No performance qualification or new generic renderer feature is claimed.

Evidence is in game `artifacts/lighting-foundation/emission`; stage logs are
`artifacts/lighting-foundation-emission-{matrix,controls,battle,overlap}.log`.
Native exec sessions 33367 (export), 68476 (station matrix) and 9420 (remaining
captures) exited successfully. `overlap/comparison.png` and `verification.json`
retain the source-isolation evidence. Changes are left uncommitted.

## Routing and remaining renderer work

The installed doctor saw root gpt-6-astra and pinned astra_flash_builder. Actual
worker `/root/canopy_emission` failed immediately with HTTP402 insufficient credit
and was interrupted. Root implemented the fixture bundle under the user's existing
approval to continue with the current model. No worker implementation was accepted.

At phase 1 closure, local shadows, HDR, colored indirect light and scene reflections
were unimplemented. That review identified RGBA8 world targets, per-material
mapping and format-bound PSOs as constraints. The following shadow/HDR sections
record their implementation and compatibility checks. Colored indirect light and
scene-object reflections remain later work. No new Cyberpunk capture or hardware
RT was required for the fixture step.

## Local shadows (2026-10-16)

User authorized proceeding toward the approximately 75% conventional lighting
milestone. LOCAL_SHADOWS.md specifies the first renderer bundle. Current work is
uncommitted and functionally verified, with performance limitations below. Pre-existing 60 files across four repositories
were copied with hashes to artifacts/lighting-foundation/pre-local-shadows.

Implemented optional IWorldLocalShadow, shared atlas/projection policy, 4096 D32
atlas (2048 sun plus eight 1024 local tiles), three backends, shader visibility
for rectangular diffuse/specular contributions, field/battle/calibration routing.
Added bright public `RectLightCalibration shadow` plus independent ray/box oracle.
Corrected foliage CPU culling to use each actual light matrix (including sun).
At this stage there were no HDR/bloom or indirect/reflection changes. Root performs work under
prior approval after the already documented DeepSeek 402 failure; no new worker.

Initial and final engine/game builds passed. PhlosionLocalShadows,
PhlosionRectLights, CyberpunkOutdoorProfile and CyberpunkWorldEnvironment pass.
All twelve public probe on/off captures and all policy controls pass the
independent ray/box oracle. The skinned control was corrected to compare against
its matching moved-pose off capture (not the stationary cube); no image threshold
was relaxed. D3D12 has 294 shadow samples with median reduction 64/255 and 1,416
clear samples with median change zero. Twelve dry station on/off captures also
pass API/host parity, maximum API MAE 0.60834/255, with no allocation failures.

OpenGL's first local-shadow run exposed a major cost regression: roughly 212 ms
versus 58 ms off at 832x468. Temporary opaque-only, receiver-disabled and foliage-
disabled probes isolated the dominant problem to the vertex material dispatcher
for repeated wind-deformed shadow geometry. Specializing BOTH vertex and fragment
material selectors preserves the same shader/discard/skinning path and produces
an identical station image (MAE 0, max 0), at approximately 60 ms with shadows.
Fragment-only specialization did not solve it. The temporary opaque program,
receiver bypass and palette-unroll experiments were removed. The accepted approach
caches generic per-material shadow programs and prepares them through the existing
material prewarm path. It contains no station coordinates or foliage mode numbers.
All GL public probe/policy controls were rerun after this change and pass.

The user closed Cyberpunk 2077. Clean paired 1080p tests supersede all earlier
provisional timings: game `artifacts/lighting-foundation/local-shadows/clean-gpu/
performance-summary.json`. Clear night, bloom off, 200 warm samples [30,230):

| Backend | GPU off/on ms | CPU host off/on ms |
| --- | ---: | ---: |
| OpenGL | 18.622 / 36.435 | 20.266 / 38.137 |
| Vulkan | 15.212 / 21.205 | 28.619 / 72.622 |
| D3D12 | 8.544 / 19.962 | 9.478 / 20.272 |

These shadow-only measurements predate HDR. They establish substantial GPU and
CPU cost, not 60 FPS qualification. Vulkan local submission alone is 36.708 ms.
Retain the original failed/experimental logs. Wet station (six), rainy battle
(six), and D3D12 off/dry/day/oblique controls (seven) completed and passed existing
validators. Wet maximum API MAE is 1.31/255 at the unchanged 1.5 limit.

## HDR scene acceptance

RGBA16F world targets and compatible direct/scene pipelines are implemented on
all three APIs and editor surfaces. Optional IWorldSceneRadiance resets per view;
the game opts outdoor views in. Shared bloom receives unclipped radiance, then
ACES, sRGB and display dithering run once. Sky remains unclipped. Feedback maps
into its existing RGB8 history before authored feedback. Other consumers retain
their mapping convention (float intermediate storage can change quantization).

All nine native intensity/blend/bloom/feedback tests and three legacy feedback
tests pass. Eight engine CPU/shader architecture checks and five game environment
contracts pass. Original failures are retained: SDL_setenv did not set the CRT
capture environment; the feedback test then exposed a real OpenGL first-draw
buffer initialization bug, now fixed. Sky clamping found during review is fixed.

Engine build-presentation editor, game runtime/plugin and model providers are
rebuilt and staged. Autochess engine-build editor/plugin paired proof and separate
runtime rebuild pass. The first paired build was correctly rejected because
source changed during compilation; the stable second build passes.

Final HDR wet station (six), rainy battle (six), and D3D12 controls (seven) pass
under game `artifacts/lighting-foundation/hdr/native`, with unchanged validators.
Maximum API MAE is 1.30917/255; maximum runtime/editor MAE is 0.0000155/255.
The independent front/oblique emitter projection oracle, puddle/dry response and
localized halo checks pass. Visually reviewed night, lights-off, daylight and
battle images. All nine native radiance cases pass again after strengthening the
test to delete stale screenshots and reject exposure above the supported range.

Final clean 1080p timing with HDR enabled, bloom off, 200 warm samples [30,230),
all GPU runs sequential and Cyberpunk closed:

| Backend | GPU shadows off/on ms | CPU host off/on ms |
| --- | ---: | ---: |
| OpenGL | 22.529 / 39.054 | 24.163 / 40.777 |
| Vulkan | 15.316 / 20.984 | 26.206 / 64.490 |
| D3D12 | 14.852 / 20.214 | 14.919 / 20.422 |

D3D12 with normal bloom enabled: GPU 21.536 ms, CPU host 21.828 ms. These are
clear-night station measurements, not rain/battle/whole-game frame guarantees.
Do not infer isolated HDR overhead from separate earlier runs. This is not a
60 FPS setting across APIs. Vulkan's local-shadow CPU submission is 29.850 ms;
repeated geometry submission and GL's shadow cost remain optimization targets.
Full raw samples and matching-camera/import checks are in game
`artifacts/lighting-foundation/hdr/final-performance/performance-summary.json`.
RGBA16F adds four bytes per world-target pixel (~7.91 MiB at 1080p); opting into
the 4096 D32 shadow atlas adds 48 MiB over the original 2048 map. No allocation
failures occurred. The settings allocate targets on preparation/resize, not per
frame.

Preservation audit: 53 of 60 pre-existing dirty files remain byte-identical. The
seven intentional overlaps are three generic material HDR gates, engine CMake,
PLAN/CHECKPOINT, and game TEST_PLAN. The three material files differ from their
pre-HDR copies by one radiance-mode gate each; prior ownership cleanup is intact.
All other pre-existing model/Autochess edits remain unchanged.

All three API scene-switch checks pass (five station/reference visits each).
The final D3D12 normal launch passes and restores Hybrid rain / day-night defaults.
Game logs are `artifacts/lighting-foundation/hdr/scene-switch-*.log` and
`normal-launch.log`. Both engine editor build directories and staged game/model
plugins are current; no visible editor was left running.

Autochess native checks passed: six runtime cohort images and six editor body/eyes
and stage images, including comparison against the material-ownership baseline.
Existing content, API parity and baseline limits all pass. Visually reviewed a
runtime cohort and the editor model view. Final staged Cyberpunk/model DLL hashes
match their rebuilt artifacts, and engine/game/Autochess diff checks pass.
The first artifact-only retry helper failed because its new results directory did
not exist (the historical helper assumed an earlier capture run had created it).
The PNG and error are preserved; explicit directory creation fixed log capture.
The production capture helper and image limits remain unchanged. Runtime startup
is allowed 450 seconds for cold shader variants, as in the earlier qualification.
No engine/game source change was required for this harness issue. Final summaries
are in Autochess `artifacts/lighting-foundation/hdr/runtime/captures/matrix-report.json`
and `editor/report.json`. All native sessions have completed and no game/editor
process was left running. The next engineering priority is reducing repeated
shadow submission cost before increasing lighting complexity.
No colored indirect bounce or scene-object reflections are implemented. Do not
claim the approximately 75% target as measured parity. All work is uncommitted.
