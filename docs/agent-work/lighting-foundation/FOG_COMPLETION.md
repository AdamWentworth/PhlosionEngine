# Volumetric fog completion contract

Frozen 2026-11-01 before implementation, against engine 59a3f5e and game c646519d.
Status: complete. **100% estimated coverage of the fixed F1-F8 supported Windows
contract**, with numerical, native, normal-host, visual, timing and strict-pin
integration gates passed.

The target is the connected ordinary raster volumetric-fog technique in the
supplied Mainline snapshot, on the supported Windows D3D12/Vulkan paths.
This is technique coverage, not retail pixel equality, all platforms, or the
separate cloud/sky/weather, GI, sandstorm and character-wind categories.
Current qualified sky, directional-shadow and GI producers remain explicit
adapters; this category must consume them without substituting a light overlay.

## Fixed gates

- F1: One owned preparation per view, nonlinear near and half-resolution distant
  grids, distant depth = near range + 20 times nonlinear near depth; independent
  temporal banks, fenced resources and bounded Low/Medium/High/Ultra budgets.
- F2: Active snapshot medium math: height/altitude density, anisotropic directional
  phase, independent scattering and absorption, global/local/ambient scales.
  NEW_ABSORPTION is zero in the supplied shader: absorption affects extinction,
  not RGB scattering. Preserve the active segment-transmission integration order.
- F3: Priority-oriented local fog boxes (32 visible), nearest density particles
  (64 visible), edge/density falloffs, medium blending and light channels.
  Accept live per-view data; do not invent active source particle noise.
- F4: Consume qualified sun/moon shadows, current indirect light and native local
  light attenuation/Fog participation/channel masks, including the native tube
  exception to area-light hemisphere clipping. Local fog lights have no
  phase factor or local shadow sample in the active snapshot branch. Distant fog
  has global sky/ground lighting and no local lights or local density emitters.
- F5: Jittered world reprojection and independent near/far history; reject camera
  cuts, disocclusion, invalid history, resize/quality/view/scope changes and
  discontinuous weather. Smooth live medium/light changes remain responsive.
- F6: Reconstruct near and distant RGB/transmittance, compose opaque surfaces and
  sky, and fog transparent lit geometry at its own depth. Apply exposure once,
  remove duplicate legacy local haze, preserve the separate distant atmosphere.
  Falling rain reuses the scene's prepared near volume.
- F7: Ordinary field and battle paths, dry through heavy rain, all four tiers,
  pause/resume/camera changes and resource release. Game owns authored weather
  and emitter data; engine owns generic fog. No experimental enable switch.
  Preserve Hybrid material-dependent impact shapes and actual-puddle rings.
- F8: Independent numerical fixtures, both native APIs, meaningful normal-host
  evidence and visual checks, optimized paired dry/wet GPU timings on GPU1.
  Publish source-to-implementation-to-test evidence, prepared shader identity
  and strict engine/game dependency pins; integrate only the tested delta.

## Reference anchors

Worker private source:
Mainline/engine/shaders/volumetricFog.fx and include/include_volumetricFog.fx;
dev/src/common/renderer/src/renderVolumetricFogManager.cpp;
include/include_lightingHelpers.fx; applyGlobalFog.fx;
include/scaffolds/transparent_lit_material_scaffold.fx.
Research receipts live in phlosion-fog-completion-20261101. Production code is
independently implemented. Source algorithm quirks are tested explicitly.

The previous rain volume supplies near light integration only. Legacy local
haze is a separate screen-space approximation; neither alone satisfies F1-F8.

## Implementation and independent evidence

| Gate | Production path | Acceptance evidence |
| --- | --- | --- |
| F1 | `IWorldPrecipitationLighting`, both native precipitation backends; paired near/distant publications | CPU dimension/identity checks; native all-tier resize, separate histories, resource release |
| F2 | `precipitation_lighting.glsl` medium, lighting and integration | Independent ambient, directional, non-unit absorption, Beer-Lambert and source segment-scattering values |
| F3 | `WorldFogMedium`, priority/frustum admission, shader medium blending | Visible caps/order, invalid axes, uniform box, rotated overlapping edge/falloff blend, particle edge-bleed expression and channels |
| F4 | Existing directional shadow/GI inputs, native local sources, separate distant ambient | Spatial pink/blue fields, participation/phase exclusion, tube versus area mapping, distant exclusion of local media/lights and near opaque depth |
| F5 | Near/far temporal banks, world reprojection, view/weather rejection | Camera/frame/quality/resize/revision cuts, 95% valid history, continuous versus abrupt weather; native disocclusion and final moving/cycle hosts |
| F6 | Per-view fog bindings, primary material composition, sky and own-depth transparency | Native opaque, fused surface, shared-depth, distant, zero, exposure, alpha and additive HDR-to-display oracles on both APIs |
| F7 | Game `OutdoorRain::prepareSceneFog`, ordinary field/battle schedules | Normal runtime/editor, four tiers, scene returns and live weather/day cycles; original Hybrid impact/ring consumers |
| F8 | Prepared publication, strict pins, exact-delta handoff | 26-case host matrix, reviewed captures, optimized paired timings, strict pin/build and four final integrated native hosts |

The primary opaque surface already owns depth. Fog reads that exact view's
published depth, rather than restoring it into the attachment and copying it
again for each volume. Fog color is fused into existing primary composition;
transparent consumers use their own depth, and additive effects only attenuate.
The generic opaque-first composition API is separately qualified.

D3D12 uses two extra SRVs and a small per-view constant block. Vulkan extends
the existing view/receiver descriptor set. The editor/plugin ABI is **54**.
GPU images and temporal banks remain owned by the existing world-resource
scope. No local AI, controller build, or controller GPU test is part of this pass.

### Source decisions and explicit adapters

- Source `volumetricFog.fx`, `include_volumetricFog.fx`,
  `renderVolumetricFogManager.cpp`, and `renderData/src/envParameters.cpp` define
  the active branch and authorable controls. `NEW_ABSORPTION=0`: absorption
  changes extinction, not the RGB source term. The source's segment rather than
  accumulated-transmission RGB ordering and PI/4 phase normalization are
  intentional, independently checked decisions.
- Native tube/point/sphere sources must not inherit the proxy rectangle's
  one-sided clipping. The source explicitly exempts tubes. Actual one-sided
  area emitters retain their plane test. Capsule cone attenuation uses the closest
  segment point and the separate outward direction, matching the existing native
  surface helper. Fog explicitly defines `FORCE_NO_LIGHT_ATTENUATION_CUT` in the
  reference: its dim tail must not inherit either surface cutoff. There is no local
  phase multiplication or local shadow sample in this active fog-lighting branch.
- Distant fog uses its own one-sample Bayer jitter and half-sized grid. Near
  cold-start supersampling follows the game's four bounded quality budgets.
- Phlosion uses Y-up metres, linear floating-point radiance and an explicitly
  interpolated tiled 2D volume. The reference uses its own coordinate convention,
  HDR packing and native 3D textures. Local emitter authoring uses validated
  floats instead of the reference's packed authoring representation.
- The qualified Phlosion directional-shadow, GI and sky/ground producers supply
  fog's inputs. This does not implement the separate reference cloud simulation,
  world-irradiance atlas, sky/weather or sandstorm categories. The station's
  ground irradiance/elevation inputs are authored for its flat desert.
- The game owns shallow apron moisture boxes and slow spray-density particles,
  driven continuously by rain. Source particle-noise code is inactive; no
  substitute active noise algorithm is claimed.
- Surface-calibrated station light values require their own fog light gain.
  The source exposes this separately as `LocalLightScale`; this scene uses
  **0.08**, with the same gain feeding scene fog and rain's shared volume.
  This is authored brightness, not fewer samples, disabled stages or a quality cut.
- Perspective gameplay and Game preview use the full path. The existing flat
  editor layout camera has no perspective depth and keeps its authoring
  atmosphere, as established by the earlier temporal/startup contracts.
- Legacy local haze is removed from these gameplay paths. The separate authored
  distant analytic atmosphere remains; broader sky/weather is a different row.

### Development findings

The first scene launcher selected an older staged revision; that run was rejected.
The corrected launch exposed an FXC nested-discard compiler failure for material
249. Evaluating fog before particle clipping fixes it without removing clipping;
the isolated optimized compiler reproduction and subsequent normal launches
cover it. The first complete image also exposed excessive station fog light gain
and the native-tube hemisphere mapping error; neither image is acceptance.
A game CPU fixture was updated from ABI53 to ABI54. Test harness corrections
include the registered battle-arrival arguments and reading scene-fog evidence
from the runtime handshake instead of searching display logs.


The first tube-plane correction did not remove the visible split. Controlled
staged-shader captures isolated local lights, then the angular cone, rather than
ambient GI, depth rejection or group culling. Fog incorrectly used the capsule's
longitudinal axis for that cone. It now uses the nearest capsule point and the
separate outward direction already used by native surface lighting. Both APIs
have native regressions for longitudinal-half symmetry and independently
calculated dim-tail energy. `fog-final-cone-vulkan` removes the visible split.

The first prepared-asset restore encountered the worker's existing research-asset
junctions while validating unrelated lock entries. Qualification uses a shader-only
lock containing the exact production shader entry; package validation is unchanged.
The rejected pre-cone publication is retained separately; the final immutable
release is `scene-shaders-v1/2026-11-01-fog-v2`. Timing wrappers explicitly select
the immediate pre-task integrated rain-efficiency baseline, not its older input.


## Qualification results

- Optimized engine/editor/game builds pass. CPU engine/precipitation contracts,
  five game contract groups, the 18 API-native fog/rain fixture runs and four
  game rain-lighting/motion runs pass with no API validation errors.
- 26 strict prepared-shader host cases pass: 24 active-fog runtime/editor cases
  across presets, dry/wet, orbit, battle and weather/day cycles, plus two
  orthographic authoring scene-return checks. Authoring has no gameplay fog;
  its scene-return resource totals match and replaced scopes are reclaimed.
- Representative full images and API/weather sheets were reviewed, including
  the corrected capsule cone. The development seam captures are rejected.
- Hybrid material-specific splash producers and real-puddle ring producers are
  preserved. Their transparent outputs now receive scene fog at their own depth.
- The immutable prepared HLSL package contains **65 programs**:
  `scene-shaders-v1/2026-11-01-fog-v2`, publication SHA-256
  `d3e8ef57b9b4011e7aec66802226fe3c4f955e4cd143fade9c77a006d6ffbe22`.
  Strict hosts have no runtime HLSL source-compilation misses.
- The editor/plugin ABI is 54. Strict locked-dependency configuration, optimized
  runtime/plugin rebuild and all six game CPU contract groups pass against the
  committed engine. Four final integrated runtime/editor hosts on D3D12/Vulkan
  pass with prepared shaders, no API errors or runtime HLSL misses, two active
  boxes/eight particles and no legacy local haze. F8 is closed.

### Matched optimized GPU cost

EVGA RTX 2060 SUPER, worker GPU1 / LUID `000000000000be8e`, 1920x1080 Medium,
RelWithDebInfo. Heavy-rain values below are the mean of two independent run
medians in ABBA order; dry controls are single runs. Each accepted run uses
239 frames after the first 120. Validation and nested phase tracing are off
for comparison; diagnostic runs are separate. These are local GPU-cost
measurements, not retail CP2077 FPS or long-session whole-game guarantees.

| API | Dry before | Dry after | Heavy before | Heavy after | Heavy change |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3d12 | 13.313 | 13.414 | 16.096 | 15.302 | -0.793 ms |
| vulkan | 14.018 | 13.704 | 17.184 | 16.282 | -0.903 ms |

| API / revision | Wet run medians | Pooled GPU p95 | Active GPU clocks per run | Maximum temperature |
| --- | --- | ---: | --- | ---: |
| d3d12 / before | 15.038, 17.153 | 18.180 | 1635.0, 1672.5 MHz | 64 C |
| d3d12 / candidate | 14.230, 16.374 | 17.039 | 1680.0, 1650.0 MHz | 63 C |
| vulkan / before | 17.226, 17.143 | 18.628 | 1672.5, 1687.5 MHz | 65 C |
| vulkan / candidate | 16.282, 16.281 | 16.754 | 1680.0, 1680.0 MHz | 64 C |


Accepted timing runs have no sampled thermal-slowdown flags, invalid GPU frames
or dropped spans. Excluded attempts: 0. Absolute times vary;
the repeated run medians and pooled tails are retained rather than reporting
only the fastest sample. No preset volume budget, rain density or impact detail
was reduced. The old local-haze pass is removed, scene fog uses already prepared
opaque depth, opaque fog is fused into ordinary composition, and rain reuses
the same near volume.

Full receipts and raw evidence are in `WORKER_EVIDENCE_ROOT/phlosion-fog-completion-20261101`
on gpu-worker and the reviewed controller handoff at
`PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/fog-completion-20261101`.
Key records: `fog-native.json`, `connected-qualification.json`,
`final-source-requalification.json`, `final-qualification.json`,
`host-audit.json`, `visual-review.json`, `performance.json` and
`performance-summary.json`. Final integration records are
`integration-pin.json` and `integration-runtime.json`.

### Reference identity

| Snapshot file | SHA-256 |
| --- | --- |
| `engine/shaders/volumetricFog.fx` | `9b8260534a047f0d71eab96941afe5846e2545cba294c2975588c948b7d48428` |
| `engine/shaders/include/include_volumetricFog.fx` | `57dd7bd1e88aeafa06d484c0f44a6f35ccd20f495f173ac7ae6ab8de9a1f8611` |
| `dev/src/common/renderer/src/renderVolumetricFogManager.cpp` | `32a9ae4f3cc6561fc60c4c5cf349e93c1c94f9833b34a930577b5fdb9ac62944` |
| `engine/shaders/include/include_lightingHelpers.fx` | `018f2789097ea3bfdde2fe73165b79cd3dfc2ea620c9ee16b1f5d51f3c197235` |
| `dev/src/common/renderData/src/envParameters.cpp` | `5a64b3ea311a2d253d5fa98a207bf012beb7a1408b54b5325758af1f92c2c54c` |
