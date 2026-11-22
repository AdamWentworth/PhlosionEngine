# Sandstorm weather completion contract

Frozen 2026-11-05 before implementation against engine a2b67aa, game fa94372,
research f72d151. Implementation qualification is recorded below; final release
acceptance requires matching D10 receipts.

Target: the connected ordinary weather sandstorm and authored approaching
storm front in the supplied Mainline snapshot, on Windows D3D12 and Vulkan.
100% here means every gate below passes, not identity to every retail patch,
every quest, assets, random sequence, platform or pixel.

- D1: A pinned prepared package, traceable to the active weather definition,
  environment curves, effect tracks, enabled emitters/materials and audio.
  Resolve inheritance/defaults/disabled properties; preserve provenance.
  Normal launches never read source dumps or run research tools.
- D2: Shared simulation-owned entry, sustained storm and exit. Blend environment
  overrides and effect lifetimes, retain particles during exit, keep wind phases
  continuous, support interruption/reset/pause/replay and field/battle continuity.
  Rain remains separately controlled; dust does not produce rain impacts/wetness.
- D3: Connected weather curves for sky/sun/moon/clouds, near/distant extinction
  and scattering, exposure and shared wind. The final source audit adds the active
  directional specular tint and depth-protected distant heat shimmer; these are
  required before closing D3. GI, probes, vegetation and particles
  consume the same snapshot. Preserve existing dry/rain behavior outside storms.
- D4: Active near/transition particle emitters, source module/evaluator semantics,
  world/local transforms, spawning, lifetime, curves, wind/drag/noise, LOD,
  billboards/motion stretch, bounds and deterministic seeded behavior.
- D5: Dust material stages, flipbook interpolation, grain masks, smoke scattering,
  lit-particle atlas, depth softness, sorting and own-depth fog. No flat colour
  overlay in place of those stages.
- D6: Authored horizon/front placement, spherical orientation and distance LOD;
  trace optional lightning and distinguish independent quest-only content.
  Connected visibility, directional shelter and indoor weather/audio response.
- D7: Storm audio event/continuous intensity, wind coupling and shelter,
  smooth crossfades with existing rain ambience. No repeating unqualified
  rain clip substituted for sandstorm audio.
- D8: Normal runtime/editor selection, explicit Low/Medium/High/Ultra budgets,
  bounded allocations, no work when inactive, no experimental renderer opt-in.
- D9: Independent source fixtures/numerical checks and negative cases, both APIs,
  field/battle, time-of-day/quality/pause/transition/shelter visual checks, resource
  release and validation. Optimized timings report actual storm cost separately.
- D10: Reviewed integration with exact dependency pins and installed editor
  handoff; preserve unrelated work and retain receipts and known differences.

Source anchors: runtimeSystemWeather.cpp/environment overrides/WeatherProgress;
24h_weather_sandstorm.envparam; e_weather_sandstorm.effect;
e_sandstorm_inside.particle; e_sandstorm_transition.particle;
e_weather_sandstorm_horizon.effect/entity and linked particle content;
runtimeSystemAudio.cpp and weather_state_events.json; corresponding particle
modules, blackbodyradiation material graphs and compiled/render shader paths.

Qualification evidence: WORKER_EVIDENCE_ROOT/phlosion-sandstorm-20261105 on gpu-worker.
Controller orchestration: PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/sandstorm-20261105.

## First implementation checkpoint (2026-11-05)

Not complete; no gate is closed solely from this checkpoint.
Source references and remaining gaps are documented in
Cyberpunk2077Research/environment/sandstorm/README.md.

Qualified primitives: grouped authored curves (156 / 14,007 independent probes),
persistent effect-loop timing, signed exposure metering and separate celestial
sky/direct-light colors. Forty-two cooked particle tables match retained source
bytes; the particle simulation and material consumers are still pending.
The first numeric weather consumer includes 61 curves/six constants, correct
HDR/exposure authoring units, shared 20 m/s wind and separate rain control.
CPU tests cover malformed packages and state transitions; native sky tests and
initial ordinary runtime/editor checks cover both enrolled Windows APIs.

The actual source Night City default has an empty sandstorm effect reference.
Connecting the authored moving dust and horizon effects is a deliberate game
binding and remains part of the frozen D4–D6 target. Current retail audio event
hashes differ from the source snapshot; controlled containers were identified,
but decoded audio, event mapping and consumers are not qualified.

Worker candidate ABI65/model37/VFX22 is not a locked release or local install.
Do not replace the existing qualified editor with these partial binaries.
Distant fog, dust, front/shelter, audio, full matrix, costs, package publication
and canonical integration remain open. Preserve the fixed gates above.


## Moving-dust checkpoint (2026-11-05)

The worker now has the eight inside/transition emitters, a shared rain/dust
lighting update and a dedicated animated smoke material. Independent CPU
geometry/simulation and native material checks pass; ordinary host checks are
recorded separately in dust-qualification.json. Research README records the
exact tested subset and outstanding ownership, transition and front work.

No fixed gate is closed by this increment. In particular, native material
agreement is not the complete D4/D5/D9 contract. The 21/23 completed-category
count is unchanged. Candidate binaries remain separate from the installed
editor until package, lifecycle and full acceptance work is finished.


## Connected implementation and release acceptance (2026-11-05)

D1-D9 implementation qualification has passed. D10 is accepted only when matching
committed revisions have locked-qualification.json and a successful controller
controller-installation.json in the task evidence directory. A source commit
alone does not close the category.

| Gate | Implementation and evidence |
| --- | --- |
| D1 | Pinned cp2077-sandstorm-v1: 24 files, 89,774,718 bytes. Curves, particle programs, nine lossless texture payloads, heat array and ten controlled audio clips retain input/output hashes. Normal launch restores prepared content. |
| D2 | Shared 60 Hz simulation, persistent loop tails, interrupted transitions and copy-on-write field/battle state. Independent 30/60/120 Hz replay agrees at 1,978 particles; pause, reset, particle survival and 120-second complete release pass. |
| D3 | Authored atmosphere/celestials, filtered cirrus, near/four-component distant fog, exposure, directional specular tint, shared wind and depth-protected heat shimmer feed ordinary GI/probe/vegetation/particle consumers. Numerical and native controls pass. |
| D4 | Eight near/transition and thirteen front emitters; grouped/cooked curves, seeded modules, quadratic drag, motion stretch, local/world transforms and LOD; capacity 3,280. |
| D5 | Dust family 237: adjacent flipbooks, grain, alpha/facing response, soft depth, sorted geometry, shared HDR particle atlas and own-depth fog. Native oracles cover perspective and orthographic depth. |
| D6 | Moving front, emitter orientation/LOD, directional wall shelter and actual doorway transitions. Independent quest lightning is traced separately. |
| D7 | Two wind layers with five recordings each, wind/5 RTPC, one-second equal-power crossfades, avoid-previous-two selection, eight maximum voices and sheltered response. Live/capture PCM agrees; tested peak 0.564178 without clipping. |
| D8 | Normal runtime/editor weather selection; four particle-light budgets and source distance LOD. Simulation/draw/audio stop after tails; scene changes reclaim resources. |
| D9 | Both APIs, four presets, runtime/editor, field/battle, four times of day, simultaneous rain, pause/interruption/clear, real indoor entry/return and resource return. Validation errors rejected; optimized timings below. |
| D10 | Exact dependency pin, clean committed E/G trees, warnings-as-errors optimized build, CPU/native/normal-host requalification, required prepared programs, editor publication and verified controller installation. |

final-storm-evidence.json maps required cases to frozen binary hashes.
tail-source-audit.json records specular/heat consumers and the source's unused
uploaded local-light-range field. Failed attempts are retained. Final checks
corrected negative RTPC weights, perspective-only heat invocation, and
orthographic dust camera/depth handling.

### Measured whole-storm cost

1920x1080 Medium, EVGA RTX 2060 SUPER GPU1 on gpu-worker, RelWithDebInfo.
Rows average two reversed-order clear/storm pair medians (frames 121-239).
Same view/dry surfaces; prepared programs required, validation and verbose
gameplay/environment logging off. Starts <=45 C; sampled thermal throttling
invalidates a run. This sampled early-storm view is not a worst-case moving-front
bound or a retail FPS/image-parity measurement.

| API | Clear GPU ms | Storm GPU ms | Added GPU ms |
| --- | ---: | ---: | ---: |
| D3D12 | 12.414 | 13.122 | 0.708 |
| VULKAN | 13.610 | 14.562 | 0.952 |

### Adapters and limits

The supplied Mainline snapshot is not authenticated as current retail. Its Night
City default has an empty sandstorm effect reference. Our game deliberately binds
the separately authored effects, placing a front 1 km upwind moving at 20 m/s.
Seeds, scene queries and bounded storage are engine/game adapters.

Heat runs before particles with the recovered screen-space calculation; general
tiled refraction/frosted-glass reuse is separate. Orthographic authoring uses
linear dust depth and an owned particle-light camera; perspective-only heat and
temporal reconstruction remain disabled.

Retail audio event hashes differ from Mainline. Controlled storm containers and
wind response are traced; event wiring, .7 mix gain and low-pass interior
treatment are explicit adapters. PCM was checked, not auditioned by ear here.

Quest-only lightning, Linux, RT, retail pixel/FPS identity, replacement human hair
and Pokemon wind response are outside D1-D10. General editor exception-reporting
lifetime hardening after a plugin unload remains a separate host issue; its
original failed case is retained. Qualified weather/scene paths no longer
trigger that exception.

Default day/night and independent Hybrid rain cycles remain. In the focused
Game view, Ctrl+End cycles clear/cloudy/overcast/sandstorm.
Launch-Editor.ps1 -WeatherPreset sandstorm starts a deliberate review.
