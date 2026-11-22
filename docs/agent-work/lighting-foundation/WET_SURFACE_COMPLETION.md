# Wet-surface completion contract

Frozen before implementation, 2026-11-01: engine 21957e6, game 06feeec.
Status: 100% estimated coverage of fixed U1-U8, qualified 2026-11-02 for supported Windows.
Release acceptance requires the exact-delta and strict pinned-host receipts below;
any source change invalidates the associated acceptance until requalified.

Scope: connected ordinary non-RT environmental wet-surface technique in the
supplied Mainline snapshot, supported Windows D3D12/Vulkan runtime and editor.
This is technique coverage, not authenticated-current-retail pixels/FPS, Linux
qualification, vehicle gameplay, deep-water simulation or character wetness.
The source excludes characters/weapon-plane receivers from its ordinary pass;
its vehicle and water/interior exclusion inputs must nevertheless be supported
and independently tested. Do not omit them merely because this scene lacks cars.

## Fixed gates

- U1: Separate moisture/puddle state, source linear accumulation while rain is
  positive and independent evaporation otherwise, bounded values and overrides.
  Simulation owns time, pauses, resets and field/battle continuity. Authorable
  rates and the game's 180-second cycle are explicit inputs, not new algorithms.
- U2: Normal-offset exposure uses bilinear interior occupancy, the two existing
  raster rain cascades with the active four-sample PCF, bilinear vehicle top/bottom
  exclusion and water-height exclusion. Real scene geometry supplies rain maps;
  bounded scene-owned spatial inputs have validated transforms and lifetime.
  Drying surfaces continue to update exposure after falling rain stops.
- U3: Dry material roughness/metalness determine porosity; apply source dampening,
  roughness response, geometric orientation/distance fades, final metallic clamp,
  emissive-color preservation and receiver exclusions. No double application.
- U4: Two-scale texture puddle mask, slope/intensity thresholds, wet/water
  separation, material/normal response and the same actual-water query for Hybrid
  impacts. Replace normal authored analytic puddle islands; preserve surface-
  specific splash shapes and rings only on actual puddles.
- U5: Source texture flipbook ripples, droplets and multi-directional wall leaks,
  correct independent distance fades/time/intensity/normal composition and
  temporal reactivity. Source animated leakage is not a fluid solver.
- U6: Shared evaluated wet attributes feed ordinary lighting/contact/SSR/probes.
  Apply once before lighting, with current-state probe relighting and required
  transparent/forward consumers; no mirrored scene or separate lit puddle layer.
  Preserve material emissive, metallic, motion and identity contracts.
- U7: Engine owns reusable wet rendering/data APIs; game owns prepared assets,
  scene fields and weather inputs. Ordinary field/battle/runtime/editor use the
  connected technique; Low/Medium/High/Ultra budgets are the only quality choices.
  No normal launch reads research or source-game dumps. Prepared programs and
  versioned package/dependency/ABI handoffs must be valid.
- U8: Independent CPU and native numerical oracles on both APIs; exposed, sheltered,
  indoor, vehicle-contained, submerged, sloped, emissive, dry/wet/drying and
  multi-view/lifetime fixtures. Normal host/preset/cycle/camera/scene-return visual
  acceptance; optimized matched dry/wet GPU cost and memory. Integrate only
  reviewed tested deltas and update the matrix after all gates pass.

## Reference anchors

Mainline engine/shaders/postfx_screenSpaceRain.fx,
include/include_rain.fx, include_interiorMap.fx, include_topDownCarMap.fx,
include_waterMap.fx; renderer/renderNode_ScreenSpaceRain.cpp and
renderGraphNodes.cpp; renderData/envParameters.cpp; worldEntities/
runtimeSystemWeather.cpp::AccumulateRain and FinalizeEnvironmentInfo.
The active ordinary raster branch includes all five material stages; the
RTX roughness tweak and inactive commented branches are not this target.

The source weather pass updates evaluated receiver attributes once. Phlosion may
fuse that operation into its owned surface producer with independent equivalence
checks; repeated per-light work or omission is not an acceptable adaptation.
Y-up, typed source-neutral data, API texture/packing layouts and authored rates
must be documented alongside independent measurements. Remaining non-equivalent
mechanisms must be listed explicitly; a new name or a narrowed test cannot
establish full completion.

## Connected implementation

The ordinary primary surface producer evaluates moisture, puddle coverage,
ripple/drop/leak normals and roughness before shared light dispatch. Contact,
screen reflection, placed-probe response and temporal reconstruction consume the
resulting attributes. Static probe captures retain dry attributes; current probe
relighting evaluates weather once. There is no new mirror pass or lit water quad.

WorldWetSurface.h owns accumulation and physical texture-mask sampling;
IWorldWetSurfaces.h owns the typed scene/view binding and bounded view history.
The shared world_wet_surface.glsl is used by both supported native backends.
WorldWetSurfaceView carries three prepared linear, mipmapped mask textures and
one compact interior/vehicle/water field atlas alongside the existing two raster
rain cascades. Scoped admission, prewarming, owner retirement and current-view
checks cover these borrowed inputs.

PokemonCyberpunk owns the prepared package and constructs the interior field
from its scene collision geometry. Its present Outskirt scene has no moving
vehicles or deep-water volumes, so their atlas tiles are explicitly empty.
Native fixtures populate those inputs and verify their exclusion behavior.
Scene replacement rebuilds owned fields; future moving vehicle gameplay must
update the supported top/bottom field rather than silently leaving it empty.

The prior authored puddle ellipses remain impact spawn hints only. Visible water
uses the source two-scale texture mask. Hybrid's sand/hard-surface splash shapes
remain selected; actual-puddle rings query the same base mask, slope, accumulated
water and scene exposure. The former analytic receiving-material islands and
legacy dampening in connected nonreceiver materials are not used.

## Explicit representation and game adapters

- Coordinates are Y-up and the engine stores conventional device depth. Camera
  axial distance, including perspective jitter and orthographic diagnostic views,
  is reconstructed from the actual view. The optional weapon-depth band is
  disabled in this third-person game, which has no separate first-person pass;
  native coverage verifies the enabled band. Character receivers are excluded.
- The source's moisture/puddle rates are 5/1.5 while raining and 2.5/.25 while
  clear, with its caller's minutes conversion. The game expresses those rates
  per real second and preserves its authored profile scales and 180-second cycle.
  Any positive rain accumulates water; evaporation starts only at zero rain.
- Texture gradients, wrapped sampling, four-by-four ripple timing, wall
  projections, intensity/slope thresholds and separate wet/water masks retain
  the active source calculations. Physical impact queries sample the base mask
  at a world point; visible pixels use their mipmapped footprint. This avoids
  making impact placement depend on camera resolution or mip selection.
- The engine's primary producer has interpolated vertex normals and world
  derivatives before buffer quantization. Its geometric normal uses the
  oriented triangle derivative with a degenerate fallback, rather than
  reconstructing the triangle after a separate depth pass. Native flat/sloped/
  wall fixtures qualify its handedness. This is not identical depth-edge noise.
- Packed surface roughness/temporal reactivity are UNORM8, albedo and normals
  retain the existing compact engine encoding. Emissive preservation maps the
  source emission control to this engine's scene-linear RGB maximum; tests cover
  zero, half and full preservation. No new exposure or tone-map behavior is claimed.
- Interior occupancy uses 32 vertical bits in a bounded three-tile atlas;
  car top/bottom and water heights retain normalized 16-bit values. These are
  typed storage adapters, not source memory layouts. Current scene geometry
  produces the interior field and persistent rain raster; independently populated
  native fields cover vehicles and water. CPU impact exposure uses static scene
  geometry sampled at raster texel centers; animated actor shelter remains a GPU
  rain-map input and is not read back to CPU splash placement.
- The active four-point Poisson calculation and occluder-distance expression are
  retained. A stable spatial integer hash supplies rotation rather than a private
  texture allocation. Its exact spatial noise realization is an adapter.
- The three masks were exported using the pinned official WolvenKit CLI. A
  second DDS export decoded with Pillow matches every PNG channel exactly despite
  the tool's old-snapshot chunk warnings. Prepared runtime RGBA mip chains and
  publication hashes have no runtime dependency on the research directory.
- Source RTX-only roughness changes, character wetness, deep-water simulation,
  future moving vehicle gameplay, Linux qualification and retail pixel/FPS
  identity remain outside the frozen ordinary environmental category.

The independent double-precision material oracle uses 0.003 albedo/roughness/
reactive and 0.004 normal component tolerances for constant masks. Patterned
RGBA8 samples permit four final UNORM8 steps only for roughness/reactivity after
the two-alpha, 15x water threshold; albedo/normal bounds remain unchanged.
Same-device D3D12/Vulkan packed readbacks must additionally agree exactly.
This filtered-texture precision budget is not a relaxation for missing stages.

## Qualification and release evidence

- Optimized RelWithDebInfo engine/game builds; wet CPU contracts, existing engine
  contracts and six relevant game CPU groups pass.
- Both native backends: 27 material/exposure cases, 243 sampled pixels each,
  compared against an independent double-precision oracle. The two packed GPU
  readbacks agree exactly. Constant and patterned masks cover interior/vehicle/
  water edges, shallow water, emissive levels, walls/slopes, exclusions, drying,
  view validity and scoped release. Maximum errors: albedo 0.000512, normal
  0.003092, roughness 0.007505, reactive 0.012426; the precision budget above applies.
- 26 strict normal host cases pass: runtime/editor, Low/Medium/High/Ultra,
  field/battle/orbit/daylight, dry and repeated weather/day cycles, and return
  between the station and Colosseum reference. No backend validation errors or
  prepared HLSL misses. Scene-return payloads match and old scopes are reclaimed.
- Four cycle traces contain 301 samples each, cross rain 0..1 and a complete
  day/night range, and accumulate independent moisture and puddle amounts.
  CPU integration and native drying fixtures cover the slower evaporation phase.
- Full-resolution D3D12/Vulkan images and editor, orbit, battle, daylight and
  cycle sheets were reviewed. Texture-defined puddles and sheltered dry surfaces
  remain coherent. The existing dark night character and dense battle foreground
  rain are unchanged categories, not newly claimed character/weather art parity.
- Three prepared masks total 12,582,908 bytes including all mips; the packed
  scene field adds 196,608 bytes (about 12.19 MiB combined). Existing raster rain
  cascades are reused. Native release and scene-return checks pass without
  allocation failures, pending scopes or return-view payload growth.
- ABI55 requires matching game/editor/engine builds. Immutable packages:
  wet-surfaces-v1/2026-11-01 and scene-shaders-v1/2026-11-01-wet-v1
  (65 prepared programs); hashes are pinned in the game package lock.

All implementation, cooking, builds, CPU/GPU checks and timing ran on gpu-worker.
controller receives only reviewed exact source/package deltas and owns commits.
Worker: WORKER_EVIDENCE_ROOT/phlosion-wet-surfaces-20261101.
Controller evidence: PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/wet-surfaces-20261101.

Release receipts are handoff-manifest.json / controller-package-install.json,
integration-pin.json (strict dependency lock, optimized rebuild, CPU contracts)
and integration-runtime.json (final runtime/editor on both APIs). Those final
receipts must pass for the committed engine pin; code acceptance is not inferred
from compilation or from this prose.

## Matched GPU cost

EVGA RTX 2060 SUPER GPU1, 1920x1080, Medium, optimized builds, validation disabled.
For heavy rain, run order is before/after/after/before; each median uses frames
121..359 (239 frames). Dry uses the same night/overcast viewpoint with intensity
zero. No quality/sample/density reduction was made.

| API | Dry before | Dry after | Heavy before | Heavy after | Heavy change |
| --- | ---: | ---: | ---: | ---: | ---: |
| D3D12 | 13.307 ms | 13.542 ms | 16.614 ms | 17.327 ms | +0.713 ms |
| Vulkan | 13.695 ms | 13.820 ms | 16.256 ms | 17.661 ms | +1.405 ms |

Heavy columns are means of the two run medians; individual runs remain visible:

- d3d12: before 16.590 ms, candidate 16.673 ms, candidate 17.981 ms, before 16.638 ms.
- vulkan: before 16.251 ms, candidate 17.677 ms, candidate 17.646 ms, before 16.261 ms.

No sampled thermal slowdown was accepted (0 rejected runs);
all accepted frames have valid GPU totals and zero dropped spans. Detailed
diagnostic runs are kept separate from total-only comparison runs. These
measurements qualify cost, not a locked 60 FPS guarantee or retail performance
identity. Source wetness changes water coverage, so image identity with the
older authored puddle model is neither expected nor asserted.

## Startup limitation and retained failures

The new shader package avoids runtime HLSL source compilation in all strict
launches. Its initial development source cooking took about 726 seconds.
The first cold Vulkan driver-cache host check took 424 seconds, including
373 seconds of renderer startup; subsequent Vulkan host checks took roughly
41-59 seconds end-to-end. These are different measurements from steady GPU
frame time and remain a startup cost to improve. Prepared SPIR-V does not remove
the driver's first pipeline compilation.

An actual D3D12 mask sampler mismatch was found by patterned-mask oracles and
fixed by using the explicit linear-repeat sampler. Invalid fixture winding and
the editor fixture's missing scene manifest were corrected before acceptance.
Earlier cancelled/superseded captures and failed fixture runs are retained in
the evidence; none are counted as passes. The final host audit uses the battle's
scene-local motion counter, which begins at the field/battle transition.

This closes ordinary environmental wet-surface technique coverage under the
frozen contract. Hybrid impact production, sky/weather simulation, sandstorms,
hair wind, character wetness and retail pixel/FPS identity remain separate.
