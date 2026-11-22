# Falling-rain completion

Status: **100% estimated coverage of the fixed R1-R8 supported Windows contract.**
The previous 65-80% category gaps are closed within this declared scope.
This contract was fixed before implementation on 2026-11-01. It qualifies the
ordinary Windows D3D12/Vulkan falling-rain category, not retail pixel identity,
all weather, splash simulation, hardware RT or Linux.

| Gate | Required connected behavior | State |
| --- | --- | --- |
| R1 | Retain qualified retail near/far geometry, group hash, card motion, mask mip sampling, depth intersection and reactive coverage. | Verified |
| R2 | Simulation-owned accumulated wind, independent current velocity, immutable per-view snapshots, pause/reset/interpolation and changing-wind tests. | Verified |
| R3 | Persistent two-cascade raster rain-depth production, opaque/masked casters, texel snapping, bounded updates and atomic publication of map plus transform. | Verified |
| R4 | Spatial light-volume production including current local/ambient/directional inputs, depth mapping, history and scattering integration; sample at rain fragments. | Verified |
| R5 | Recovered volume RGB/alpha normalization, intensity/day-night brightness and shelter/depth consumers; no camera-wide tint substitute. | Verified |
| R6 | Owned lifetime across runtime/editor, scene/camera resets, presets and rain-off/on transitions; no hidden opt-in. | Verified |
| R7 | Independent numerical and actual native checks on both enrolled Windows APIs, plus normal runtime/editor visual checks. | Verified |
| R8 | Prepared shaders, optimized matched performance/memory evidence and exact source/dependency integration. | Verified |

The volumetric producer needed by R4 is a real dependency, not waived because
mist/haze has a separate matrix row. Broader local fog emitters, sandstorms and
hair/character wind have separate requirements. Hybrid material-specific impacts
and rings on actual puddles remain the selected game art direction.

## Reference boundary

Read-only supplied Mainline snapshot and retail 2.31 rain captures are distinct
evidence. Register numbers are not interchangeable: Mainline t38 is a 2D rain
leak input; retail rain t38 is a 3D lighting volume. Match semantic producers,
sampling and data formats rather than assuming register-number identity.

- runtimeSystemWeather.cpp:829-843 integrates weather wind and supplies the
  frame offset; renderStateManager.cpp:257-261 packs current velocity separately.
- renderRainMap.cpp and renderNode_RainMap.cpp: persistent depth resources,
  vertical orthographic cascades, staged bounded rendering and publish-on-finish.
  renderSettingsVars.cpp:179-181 sets 2048 resolution and 20/200 metre coverage.
- include_interiorMap.fx: cascade selection, bilinear height decode and fallback.
- renderVolumetricFogManager.cpp, include_volumetricFog.fx and volumetricFog.fx:
  frustum volume, nonlinear depth, current lighting, reprojection and integration.
- Retail PS eb6058c3833bee1f: volume lookup, RGB times alpha, normalization and
  brightness min(sum(light)*(3+147*abs(2*(hour/24-.5))^5),.1).
- Retail VS 24280be8fba9521e and existing independent arithmetic fixtures retain
  the recovered card/hash/wind/coverage calculations.

Baseline engine c6cd310f0374287d89b21255250949ed4b01141c; game
1d472ffe87a2fc73115608f11b9ab0235a4cb481. Prior matched EVGA RTX 2060 SUPER,
1080p Medium RelWithDebInfo wet GPU times: D3D12 16.303 ms, Vulkan 16.864 ms.
These are Phlosion measurements, not matched retail Cyberpunk FPS.


## Connected implementation

Wind is now simulation-owned accumulated travel, separate from current velocity.
Pause, reset, interpolation, changing velocity and phase wrapping are checked.
Both field and battle take immutable environment/view inputs.

The ordinary Hybrid/v4 path rasterizes persistent 2048-square shelter cascades
covering 20 and 200 metres. Opaque and masked geometry participate. Missing maps
finish before first use; subsequent updates alternate cascades, retain the
pending camera transform, draw bounded static batches (300 proxies/200,000
triangles, with one oversized primitive allowed to make progress), then dynamic
casters, and publish depth and transform together. Existing prepared view and
deformation work is shared; normal sun/local shadow contents remain independent.

The per-view lighting producer computes a nonlinear frustum grid using current
directional, fog-participating local, ambient and prepared indirect lighting.
It includes grouped light rejection, maximum opaque depth, jitter, reprojection,
history rejection and 95% accumulation, followed by scattering/transmission
integration. Rain fragments sample this spatial result at their own depth.
Local fog lighting deliberately has no directional phase multiplier; its
intensity modifier precedes the source depth-dependent attenuation cap.

The recovered rain consumer normalizes RGB times transmission and applies the
time-of-day brightness calculation and cap. Near/far geometry, group hash,
mask mips, source gains, depth fade and reactive coverage remain connected.
The camera-wide irradiance path remains only for historical diagnostics and
the separately tracked source-splash adapter. Hybrid's material-specific solid
impacts and rings on actual puddles are preserved.

ABI52 carries the extended environment snapshot. Owners retain weather resources
through dry intervals, reject invalid history on return/cut/resize, and retire
resources with their scene. Low/Medium/High/Ultra choose volume budgets on the
ordinary path; there is no experimental enable switch.

## Explicit adapters and boundaries

- Mainline and retail 2.31 are separate reference revisions. Register numbers
  alone do not identify corresponding resources.
- D32 raster resources use source-equivalent UNORM16 quantization before
  bilinear shelter decoding. This takes 48 MiB/owner instead of source D16's
  24 MiB. The depth format reuses the existing shadow drawing pipelines.
- RGBA16F tiled slices and explicit Z interpolation implement the 3D lookup
  through the existing material bindings. No camera-wide lighting substitute
  remains on the ordinary path.
- The already qualified prepared GI/placed-probe producer supplies isotropic
  ambient. Its cache representation differs from the source's dedicated GI
  scattering volume; current spatial indirect energy is consumed.
- Exact source preset constants were not present in the supplied snapshot.
  Phlosion chooses tile sizes 24/16/12/8, depth slices 32/64/80/96, and rejected
  history sample counts 2/4/6/8. These are authored budgets, not claimed
  byte-identical Cyberpunk settings.
- Previously qualified geometry retains one submission per layer and the
  captured mask LOD bias. The observed retail multiplicity/dynamic bias is not
  generalized. Captured inactive material dither remains omitted.
- Full-screen fog composition, local fog emitters, cloud-shadow production,
  sandstorms, wet-material flow/porosity, splash generation and character/hair
  wind are separate matrix requirements. This completion does not rescore them.
- Windows D3D12/Vulkan are qualified. Linux, retail pixel identity and equal
  retail Cyberpunk frame time are not inferred from these checks.

## Memory and lifetime

At 1920x1080, retained volume payload per active view is 2.844 MiB Low,
11.984 MiB Medium, 26.984 MiB High and 71.750 MiB Ultra. Shelter maps add
48 MiB per prepared scene owner. Field and battle have separate owners.
The backend bounds retained views/dispatches to eight and each volume payload
to 256 MiB; scene admission reserves that bound plus the maps without allocating
unused allowance. Ultra initially exposed the missing scene allowance; the
corrected host matrix uses full quality. Dimensions exceeding the explicit
resource cap fail admission rather than silently lowering quality.

## Qualification and delivery

- Optimized RelWithDebInfo engine/runtime/editor builds; native shaders use O3.
- Engine contracts and existing engine suite pass. Game environment, surface,
  Hybrid/preset and independent native rain fixtures pass. Current-vs-integrated
  wind, phase wrapping, pause/reset/interpolation and glTF budgeted draw ordering
  are covered.
- Both native APIs execute the actual producer and actual rain shader helpers.
  Independent expected values cover near/far motion, mask/hash, D16 shelter,
  volume normalization/brightness, spatial local colors, fog participation,
  local phase invariance, directional phase energy, maximum opaque-depth rejection,
  analytic scattering/transmission, history/cuts, descriptor refresh and release.
- 26 strict prepared-shader host cases cover runtime/editor, all four presets,
  field/battle, scene switching, daylight and rain off/on with day-cycle changes.
  Four 300-frame weather-cycle traces each cross the active-rain threshold at
  least four times, with no allocation failures. Runtime captures are 1920x1080;
  the editor embeds an 832x468 scene viewport. Native ownership tests return
  live scopes to zero; the host retains its unrelated persistent owner.
- Native validation and host validation report no API errors. Prepared HLSL
  reports no source-compilation misses. The immutable shader release is
  scene-shaders-v1/2026-11-01-falling-rain (68 programs).
- Manual image review covers both API/preset/host sheets, daylight and weather
  transitions. Colored rain follows the station lighting and shelter; Hybrid
  impacts and puddle reflections remain. Vulkan's crisper/dotted fine streaks
  and the distant horizon band also exist in the baseline; they are not claimed
  as newly solved by this category.

Canonical integration is a guarded byte-for-byte task delta, followed by the
engine commit, strict game dependency pin/rebuild and both native host checks.
The final exact revisions and payload/source hashes are recorded in the separate
integration-final.json and integration-runtime.json delivery receipts. No local
GPU work or local build was used; gpu-worker performs qualification.

## Matched cost

EVGA RTX 2060 SUPER, worker GPU1, 1920x1080 Medium, optimized RelWithDebInfo.
239 accepted steady frames/run; two wet repeats/revision/API (ABBA), single dry
controls. GPU totals disable nested timestamps; separate runs diagnose passes.
No sampled thermal-slowdown runs are accepted. This is a new technique with real
spatial producers; it is not a claim of free additional rendering work.

| API | Dry baseline | Dry current | Heavy rain baseline | Heavy rain current | Current rain overhead |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3d12 | 13.857 | 13.402 | 15.622 | 16.222 | 2.820 |
| vulkan | 14.060 | 14.048 | 16.891 | 18.353 | 4.305 |

Separate diagnostic pass means (ms; not additive to total-only frame timings):

| API | Shelter map | Max depth | Lighting volume | Integration | Far rain | Near rain |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| d3d12 | 0.187 | 0.122 | 0.316 | 0.018 | 1.066 | 0.042 |
| vulkan | 0.466 | 0.174 | 0.426 | 0.018 | 2.371 | 0.151 |

Memory details and adapters above remain part of the result. GPU duration alone
is not end-to-end 60 FPS or matched retail performance. Low/Medium/High/Ultra
volume budgets are authored, while source algorithms and connected inputs are
qualified; the complete general graphics-settings category stays open.

Evidence: controller artifacts/lighting-foundation/falling-rain-20261101/evidence.zip
contains source path/hash/line metadata, task scripts, failed and final attempts,
CPU/native/host reports, prepared publication receipt, image reviews and matched
telemetry. performance-analysis.json records per-run clocks, temperatures, CPU/
host-frame medians and effect settings; handoff-manifest.json pins tested source.
