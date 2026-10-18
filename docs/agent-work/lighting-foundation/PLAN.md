# Outskirt lighting foundation

Authorized 2026-10-15 after the retained Cyberpunk capture sufficiency review.
Objective: coherent continuous pink/blue canopy lighting, believable overlap on
surfaces and characters, and a sound path for shadows, indirect light, HDR glow
and wet reflections on OpenGL, Vulkan and D3D12 in runtime and editor.

## Evidence and existing work

### Active target: recover the captured light response

The user's 2026-10-17 clarification makes the immediate target the actual
Cyberpunk light-to-surface calculation with matched inputs, independent of scene
asset differences. Further independently chosen rendering features or station
screenshots do not establish this match. The accepted haze remains an existing
Phlosion feature, not an exact recovered Cyberpunk algorithm.

Private research implementation and reproducible commands now live in
`D:/Projects/Research/CyberpunkRainResearch/tools/lighting_matched`; draw-time
inputs, original outputs and measurements live in `evidence/lighting-matched` in
that depot. PSO 20727 / indirect parent 7084 / dispatch 7085 is the first target.
Keep captured binaries, raw resources and disassembly outside source repositories.
The work includes a diagnostic DXIL interpreter and a separate, readable CPU
reconstruction of local-light response. Replaying or interpreting retail bytecode
alone is not an independent implementation. Neither tool changes game lighting.

The isolated GPU fixture is qualified with explicit numerical limits; see
[GPU_LOCAL_RESPONSE.md](GPU_LOCAL_RESPONSE.md). The same response core passes
synthetic native compute fixtures on OpenGL, Vulkan and D3D12. The live adapter
is documented in [LIVE_FINITE_LIGHTS.md](LIVE_FINITE_LIGHTS.md): the existing
physical rectangle has an explicit direct-response selection, area/radiance to
intensity mapping, authored material F0 and singular-input handling. Canopy
fixtures use the finite response, with an LTC comparison override. Require the
established runtime/editor and shared-consumer checks before accepting the live
change. The capture exercises finite-length lights and two-moment shadow
visibility; live depth-PCF local shadows remain a different algorithm.
Check the artifact's branch coverage before extending claims to other shapes,
profiles, materials, shader permutations, frames, indirect light or reflections.
The next filter component is now independently qualified against the capture:
[SHADOW_MOMENTS.md](SHADOW_MOMENTS.md) records bit-exact horizontal, vertical and
chained distance-moment outputs, seeded-output controls and native math fixtures.
It is not yet a live shadow path. The next implementation is engine-owned moment
targets, ordered filtering passes and matching radial receiver/addressing adapters,
followed by existing scene/shared-consumer acceptance. No additional user capture
is currently required for that integration.

The user's engine-parity target means matching the observable lighting subsystem
in the captured rendering mode. Track direct response, source shape/units,
shadow generation/filtering, material decoding, reflections, indirect light,
volume scattering and display response separately. A component's numerical
agreement is not a percentage for the entire renderer. Do not substitute a new
feature or a performance improvement for measured behavior agreement. The current
retail evidence is RT-disabled; it cannot establish parity with ray/path tracing.

The material-ownership cleanup is verified in engine, game,
PokemonModelSupport and Autochess and included in the authorized 2026-10-17
checkpoint commits. Baseline patches, edited/new files
and the current private station outputs are in ignored
`artifacts/lighting-foundation/baseline`. Original engine HEAD was 69a2fa2 and game HEAD 668c3a4.
The authored Blender source must remain unchanged; regenerate the derived export.
All general rendering belongs to the engine; scene placement belongs to the game.

The retained retail 2.31 night capture has RT disabled and SSR High. Its local
light, probe, shadow, reflection and HDR glow passes are documented under
`D:/Projects/Research/CyberpunkRainResearch/evidence/lighting-study`.
`capture-sufficiency-review.json` corrects the earlier shadow-pass count: 45 draws
and two dispatches, not 47 draws. No new capture is needed for foundation work.
The current pink rectangles face outward and illuminate no interior canopy
receiver directly. Blue faces downward. RGB accumulation already combines them.
The accepted HDR pass now keeps radiance through bloom before the display transform;
direct-source highlights are not scene reflections. Those earlier foundation
passes did not recover Cyberpunk's shader calculations; the later matched local
response above provides the first independent numerical component comparison.

## Phases

Phase 1 is accepted; see CHECKPOINT.md and the game's EMISSION_PASS.md.
The local-occlusion portion of phase 2 is implemented and functionally verified
under LOCAL_SHADOWS.md, with substantial measured cost still requiring review.
Phase 3 is implemented under HDR.md; CHECKPOINT.md records native acceptance.
The follow-up static shadow submission optimization is specified and measured
in SHADOW_CULLING.md; it preserves the accepted appearance before further visual work.
Static, live-relit diffuse bounce is functionally verified in BOUNCE.md, with
measured cost and cold-start limitations retained for follow-up.
Flat wet-surface scene reflections are implemented under REFLECTIONS.md. This
captures live reflected geometry, including off-screen objects; it does not yet
provide general SSR, local reflection probes or ray tracing.

1. Fixture emission: retain four continuous pink perimeter runs and two blue
   ceiling runs, with uniform healthy colors and steady intensity. Make the pink
   luminous planes face outward and 60 degrees downward, matched by their physical
   diffuser/channel geometry. Prefer six sources over adding four more draws and
   area-light integrations. Preserve radiance, weather, layout and source assets.
   Validate exported normals/planes and neutral receiver overlap, then native
   station and battle captures across all APIs/hosts plus off/dry/day/oblique
   controls. Compare character front/back/side response, not just sand brightness.
2. Occlusion and indirect lighting: inspect existing shadow submission and material
   interfaces, then specify a generic local-light shadow contract and a deliberate
   indirect solution. Avoid per-camera fill, hardcoded station blockers in generic
   shaders, or claiming proxy shadows are full scene shadows. Confirm cost and
   compatibility before implementation; disabled inputs preserve old consumers.
3. HDR composition: retain radiance through opaque/transparent world composition
   and bloom with one final display transform. Preserve editor/UI output and
   non-world model inspection. All three APIs and model paths must agree.
4. Material-aware reflections: choose a scene reflection path with off-screen
   fallback based on the available render interfaces; distinguish rough sand,
   wet asphalt and shallow water. Verify camera motion, occlusion and source-off.

Phases 2-4 depend on architecture findings and get concrete contracts before
implementation. Do not fake missing layers with higher direct-light intensity.
Checkpoint commits were authorized on 2026-10-17, followed by scene-reflection
work. Pushes, game restart, rain redesign, source asset overwrite, dependency
upgrades and generic rendering changes in PokemonModelSupport are not included.

## Execution and acceptance

The former approximately 75% estimate is withdrawn: it was not a measured
lighting comparison. Subsequent work must report a specific visible change and
matched evidence. Submission optimizations are performance work, not additional
lighting fidelity. The current visual pass adds rectangular-light scattering in
the existing height haze; it does not claim Cyberpunk's exact implementation.

The user explicitly requested cloud Astra throughout this lighting work on
2026-10-16, excluding the newly installed local model. Root implements and reviews;
do not start local AI or delegate this pass. This supersedes the earlier Flash
worker plan and fallback.
Keep independent GPU jobs sequential and do not stop the user's Cyberpunk game.
The editor was not running at orientation; request closure only if a later build
is actually blocked by a newly opened editor.

Acceptance requires actual generated geometry/source agreement; meaningful CPU
contracts; native runtime/editor content and backend parity at existing thresholds;
source-off, daylight and camera controls; and recorded GPU cost for renderer changes.
No visual baselines or thresholds are relaxed to hide regressions. Shared shader
changes also require the affected Autochess character/editor smoke coverage.
Keep exact commands, artifacts and limitations in CHECKPOINT.md at phase boundaries.

## Local illuminated haze

Opt-in `WorldEnvironmentLighting::localHazeAlbedo` and `localHazeAnisotropy`
occupy the two reserved weather-material uniform components, independently of
surface wetness. Default albedo zero preserves other consumers. The game binds
0.55 / 0.25 for the station only, with `PHLOSION_DISABLE_LOCAL_HAZE` as a review
control. Existing density, source radiance and weather cycles are preserved.

The shared shader integrates single scattering along the camera-to-surface ray,
or through the local light ranges for sky pixels. It clips to each source sphere
and emitting half-space, integrates the entire rectangle's solid angle, uses
16 equiangular samples, and attenuates both view and source paths in exponential
height density. Two-by-two depth comparisons use the existing local shadow atlas;
volume samples deliberately omit surface-normal bias and receiver derivatives.
Sky submission follows shadow preparation. Reflected rays integrate only their
above-water leg; the water receiver handles the real viewer leg.

The station sets `localHazeDeferred`: ordinary sky/material draws retain existing
extinction but omit the local in-scattering integral. `drawWorldLocalHaze` captures
opaque depth, integrates into a private RGBA16F target at one quarter of each view
dimension, then adds the result to scene radiance before transparent rain and
bloom. The alpha channel carries linear distance, avoiding half-float device-depth
precision loss. Depth-aware bilinear composition rejects taps across silhouettes;
unrepresented thin foreground pixels use the analytic full-resolution integral.
Reflected and main views each run this pass. Their targets are independent of the
planar reflection image. HDR-disabled review mode also disables local scattering.
An explicit branch skips the analytic fallback when depth-matched samples exist;
the earlier conditional-expression version evaluated the expensive path on D3D12.
Vulkan orders prior sampling before reusing a scratch target in the same frame.
Its rain-only specialization explicitly excludes the haze dispatcher so that
the specialized volume shader can discard the unused integration code.
The C++ environment layout changes the model plugin ABI to 17 and editor project
ABI to 37; every staged model/game plugin must be rebuilt with these headers.

This follows the [volume transfer equation](https://pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/The_Equation_of_Transfer)
and [Henyey-Greenstein phase model](https://pbr-book.org/4ed/Volume_Scattering/Phase_Functions).
It is a bounded single-scattering approximation: phase and source-path attenuation
use the emitter centre; the current single-apex shadow map does not supply
source-sized volumetric penumbrae. It has no multiple scattering, point/sign
light scattering, froxel history or illumination of the separate billow/drop
simulation. It adds no ray tracing, rain redesign or global brightness increase.

`PhlosionLocalHazeTests` checks against independent fine area/view quadrature,
including sky, source-off, density-zero, reversed source, shadow blocker, nearby
opaque receiver, sharp foreground edge, odd viewport sizes, and red/blue addition
on all APIs, for both direct integration and the reduced-resolution pass. A second
composition in one frame checks scratch-target reuse. The game uses
`Verify-Local-Haze.ps1` and `verify_local_haze.py`, plus the established station,
battle, reflection and shared-model guards. Cost is measured after correctness;
acceptance is a visible improvement with declared limits, not a parity percentage.

## Reflected emission and wet-surface filter follow-up

Authorized after checkpoint engine 1130af5 / game 533eb44. Include emissive
geometry in the generic capture; the game's existing per-material overrides
suppress only the canopy emitters already represented by rectangle specular.
Signs, screens and unrelated emissive materials retain their authored emission
and fixture gain. Do not raise source intensity or change source assets.

Replace the five-sample cross with a normalized nine-sample 2D Gaussian,
corrected for viewport aspect and padded targets. This remains an image-space
roughness approximation, not GGX prefiltering, SSR or a Cyberpunk shader port.
Keep dry/rough-sand gates and the existing DFG energy response. Verify reflected
emissive texture shape with illumination absent, emission-off, roughness edge
spread and unchanged main-view signs, then the station/battle API-host matrix,
wet/dry/day/source-off/oblique controls, shared character checks and GPU timings.
Evidence goes under `artifacts/lighting-foundation/reflection-quality` in each
affected repository; the earlier reflection captures remain the baseline.

## Frame-cost follow-up

Authorized after engine fa7ae72 / game 45f42ca. Measure matched feature-off
controls and host scopes before choosing an optimization. Preserve source assets,
light strengths, weather, shadows, bounce, reflections and main camera. The first
candidate checks local-shadow visibility before expensive area-light integration,
skipping only contributions with exactly zero visibility. Retain it only after
native equality/content checks and repeated warm timing establish a benefit.
All three APIs and shared material consumers remain required; no threshold changes.
Evidence: engine/game `artifacts/lighting-foundation/frame-cost`.
The retained candidate enables the early check on Vulkan/D3D12 and preserves
OpenGL's original ordering after an inconsistent GL trial. `FRAME_COST.md` records
both the slow trial and repeat, matched images, cost breakdown and acceptance.

## Shadow submission follow-up

Authorized after engine ebe4d1b / game b23efa5. Separate current-thread CPU time
from wall time, then profile repeated Vulkan geometry, material and state work.
The measured first target is texture identity formatting inside material lookup:
replace repeated string-stream setup with integer `to_chars` and direct appends,
preserving cache-key precedence, anonymous identity, sampling fields and scope.
Keep cache lifetime, allocations, geometry validation, animation, shaders and
all scene settings unchanged. Verify legacy key compatibility and native images,
repeat matched timing without temporary profiling, and cover runtime/editor on
all APIs plus the shared character consumers. Evidence goes under
`artifacts/lighting-foundation/shadow-submission` in the affected repositories.
Accepted measurements, native checks and remaining targets are recorded in
[SUBMISSION.md](SUBMISSION.md).
