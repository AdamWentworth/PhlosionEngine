# Outskirt lighting foundation

Authorized 2026-10-15 after the retained Cyberpunk capture sufficiency review.
Objective: coherent continuous pink/blue canopy lighting, believable overlap on
surfaces and characters, and a sound path for shadows, indirect light, HDR glow
and wet reflections on OpenGL, Vulkan and D3D12 in runtime and editor.

## Evidence and existing work

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
direct-source highlights are not scene reflections. No exact Cyberpunk shader port
or numerical parity is claimed.

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
