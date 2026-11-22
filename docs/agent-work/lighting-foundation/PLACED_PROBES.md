# Placed probes and complete relighting inputs

2026-10-28. Builds and execution use Windows gpu-worker. Canonical integration and
commits remain on controller. Baseline: engine 7b12c3d, game 7d7d365.

## Connected path

PokemonCyberpunk's ordinary field and battle renderer now uses two game-owned,
fixed probes: under the canopy and outside the bar. These replace the six
camera-following radiance geometry views. There is no new opt-in or player
feature switch. Low/Medium/High/Ultra keep this same technique.

The game captures static dry material, normal, depth, occlusion, reflectance
and base emission once per scene/asset/pose/exclusion/placement revision.
Shadow-cache diagnostic policy cannot change that revision. Capture stores
authored wet-material parameters and an emission channel in the otherwise unused
static-probe motion plane. GltfSceneDrawOptions::probeEmissionChannels carries
the channel through materialTimeSec only during this capture; ordinary motion,
animation and material draws retain their existing interpretation.

WorldPlacedProbes relights those retained faces with:
- The current AtmosphereSky, clouds, sun/moon disc and background coverage.
- Directional illumination/shadows and current point/finite-area/LTC sources.
- Material normal/roughness/metal/F0/AO, existing diffuse-probe bounce, ambient,
  multiple scattering, reflection response and completed neighboring radiance.
- Current wetness, shelter and puddle material parameters, plus live fixture
  emission gains. Rectangle emission remains excluded where its analytic light
  already supplies the reflected source, preserving the prior policy.
- Current atmospheric haze and local scattering. Probe sky and surface shading
  resolve haze directly rather than inheriting the main view's deferred marker.

Only static field geometry enters these material probes. Actors, skinned
characters and foliage keep their normal fresh rendering and temporal/SSR
participation; this does not add offscreen dynamic-object capture. Hybrid rain,
surface-specific impacts and puddle rings are unchanged.

## Publication, blending and budgets

Each probe keeps two radiance banks. An update writes all six faces, builds the
roughness pyramid and GGX convolution, then publishes the completed bank.
Incomplete or superseded updates cannot replace the previous completed result.
The scheduler snapshots environment and emission state for each generation.
Shadow maps remain the renderer's current resources; exact snapshot-owned
shadow jobs are a remaining scheduling boundary.

The receiver selects only its logical scene's probes, applies individual box
projection and smooth influence weights, normalizes overlaps, and retains the
existing ambient fallback outside coverage. Relighting excludes its own bank
and never borrows probes from a different field/battle scene.

The initial material capture prewarms six radiance faces. Later updates process
one probe at a time: Low 1, Medium/High 3, Ultra 6 faces per frame. With two
resident probes, that completes both probes every 12/4/2 update frames.
All tiers use 128-square faces; these are Phlosion's explicit budgets, not a
claim that the inspected source uses these exact preset values.
Changing lights, weather or the viewer does not recapture static geometry.
Resize/resource recreation invalidates residency and triggers recapture.

The renderer reserves four probe identities for field/battle and two banks
per identity. Material storage uses the existing 32-byte-per-texel shared-surface
representation: 24 MiB for all four double-buffered probes at 128 square.
The persistent 2048-square RGBA16F radiance atlas adds 32 MiB. These figures omit
allocator padding, transient targets and other rendering resources.

## Source mapping and boundaries

Research: Cyberpunk2077Research/lighting/reflections/SOURCE_RENDER_CALL_PATH.md.
In the inspected snapshot, CEnvProbeRelighter::RelightProbe and
ProcessSingleStage_Relight consume retained material faces, collect current
lights, batch face updates, then filter/publish. The PROBE_RELIT shader combines
stored materials with current directional/local illumination, sky/ambient,
neighboring probes, emission and fog. This implementation connects those stages
using independently written Phlosion code and the existing qualified light math.

Key implementation:
- WorldPlacedProbes.h: scene-owned revision and face-update schedule.
- WorldPlacedProbeCache.h: complete generations, atlas banks and descriptors.
- world_probe_relight.glsl: material reconstruction and current lighting.
- world_local_reflection.glsl: placed-probe influence/box projection/blending.
- D3D12/Vulkan scene-color paths: retained resources, relight binding, filtering.
- PokemonCyberpunk OutskirtReflections/OutskirtLighting and field/battle
  presentation: positions, geometry policy, emission bindings and quality budget.

This replaces camera-following capture on the development path. It does not
close C8 or shipping acceptance: source packed eight-byte materials, generalized
streaming/priority/eviction, exact source weighting and refresh policies,
specialized materials and shadow scheduling still differ. Existing ambient,
weather, haze and indirect-lighting systems are reused, not promoted to full
source-system parity. Linux native qualification and the 16.67 ms performance
target remain release gates. The subsequent [warm performance qualification](PLACED_PROBE_PERFORMANCE.md)
records measured improvements and remaining costs; it does not establish a new
parity percentage or 60-FPS claim.

## Qualification

Final qualification is recorded with exact source and binary hashes in
WORKER_EVIDENCE_ROOT/phlosion-placed-probes-20261027 on gpu-worker. The controller receives
the reviewed delta and its evidence before integration.

The native checks compare retained and freshly captured materials under changed
sun/local lights, wetness, emission and haze. Separate placed-probe checks prove
spatial blending, invisibility of a partial update, publication of a complete
update, and absence of unchanged-geometry recapture. Game checks cover both
Windows APIs in runtime/editor, field/battle, wet/dry and camera movement.
Accelerated day/weather cycles exercise Low and Ultra scheduling budgets.


Final results: optimized engine/editor and game/runtime/plugin builds passed.
Six CPU suites, 22 native GPU cases, twelve final game/editor scenes and four
accelerated Low/Ultra cycle runs passed on the selected EVGA RTX 2060 SUPER.
No GPU validation errors were detected. All eight retained/fresh relighting
image pairs and both partial-generation publication comparisons were pixel-exact.
Profiles record two initial material captures in the field and two more on
entering battle, with continued relighting and no screen-source geometry redraw.
The cycle runs span almost 24 hours, cloud coverage 0..0.93 and rain intensity
0..1; they retain exactly 1/6 steady relit faces on Low/Ultra respectively.
Empty shutdown frames are excluded from that budget count, not rendered frames.

The final full-scene screenshots were reviewed in both APIs/hosts. They are
intentionally different from camera-following probes; existing native reference
thresholds were not widened to approve the new algorithm. An earlier legacy
Vulkan retained-material comparison differed by one component at one pixel;
an unchanged strict repeat passed. That observation and all failed builds remain
in the worker record. Cold startup is still costly and clean frame-time/60-FPS
acceptance was not established by these validation-enabled runs.

Evidence: final-build-r3.json, native-final/checks.json, scenes-final.json,
quality-final.json, qualified-source.json, source-delta.json, candidate/files.json
and corresponding logs/captures. The earlier scene harness accidentally staged
the candidate in the preceding task's directory once. Its old images/logs remain,
but that old candidate binary directory is no longer a preserved baseline;
harness-correction.json records the correction. All final runs use the isolated
current-task candidate with verified hashes. The first cycle accounting check
included an empty shutdown frame; quality-shutdown-accounting.json preserves that
failure, and quality-final-r2.py checks only recorded rendering frames.
