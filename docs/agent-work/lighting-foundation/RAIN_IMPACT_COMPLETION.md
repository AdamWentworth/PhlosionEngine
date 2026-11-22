## 2026-11-06 - finite lighting footprint for airborne billboards

Near-source inverse-square peaks were spreading through low-resolution dust
lighting tiles into bright rectangular flashes. Billboard lighting now filters
that peak using each texel's world-space footprint variance plus source radius.
Fixed-normal surface splashes retain their previous response. This deliberately
changes the point-sampled reference calculation for stable game presentation;
it is not a new retail-parity claim.

The native fixture crosses a coarse billboard tile with a nearby light and
checks bounded finite radiance, retained source hue and adjacent-position
stability. Existing direct/back-lighting, shadow, channel, preset and lifetime
checks pass on D3D12 and Vulkan. The matched 181-frame station sequence reduces
the worst 99.8th-percentile brightness change in the canopy crop from 179 to 40
(on an 8-bit channel scale); changes above 80 fall from 64 frames to zero.
Fog-off captures retain the original flashes, isolating the particle producer.
These are capture-specific artifact measurements, not a general performance
or quality score. Temporary per-frame readbacks are excluded from shipped code.

Evidence: WORKER_EVIDENCE_ROOT/phlosion-storm-light-audio-20261106 on gpu-worker;
native-filter-{d3d12,vulkan}, flash-comparison.json and locked-qualification.json.
The paired prepared shader release and controller installation receipt are
required for the normal editor handoff.

# Rain impact production and lighting
Status: P1-P8 qualified for the supported ordinary Windows technique; final
committed dependency verification is recorded by the handoff receipts.
Fixed acceptance boundary recorded before production edits, 2026-11-02.

## Reference and retained design
Supplied Mainline snapshot: ordinary CPU particle emission/initialization,
billboard packing, DPL allocation/lighting, blackbody material and transparent
composition. Source paths and independent fixtures are retained in the private
worker evidence WORKER_EVIDENCE_ROOT/phlosion-rain-impacts-20261102/reference.
The rain_ground_splash asset has 600 births/s, capacity 120, 0.10-0.15s life,
local simulation, no active collision module and baked sixteen-sample curves.
The inspected w_rain_effect references rain.particle, not rain_ground_splash.
Do not call the latter a proven active retail ground-placement path.

Game-owned collision placement and surface selection remain explicit adapters.
The user preserves Hybrid's sand/hard-surface geometry and real-water rings;
these are intentional artistic differences, not source identity claims.
Engine owns generic emission, particle-light preparation and GPU lifetime.
Game owns rain schedules, material policies, shapes and prepared art.

## Fixed completion gates
| Gate | Required evidence | Status |
| --- | --- | --- |
| P1 | Trace selected emitter, compiled curves, initialization, material, lighting and composition; state unverified retail/placement differences | Passed |
| P2 | Bounded fractional emission, persistent event lifetime, reset/repeated/older view behavior; no current-intensity erasure | Passed |
| P3 | Shared collision/wet mask semantics, cover and finite normal validation; rings contained in actual texture water beyond legacy hints | Passed |
| P4 | View-owned projected-size particle lighting tiles, conservative admission/culling, joined preparation, stable UVs and no per-pixel repeated light traversal | Passed |
| P5 | Current sun/indirect/local particle participation, wrapped/front/back response, local shadows and independent numerical native comparisons | Passed |
| P6 | Actual source and Hybrid consumers, linear atlas sampling, exposure convention, particle-depth fog once, alpha/temporal composition | Passed |
| P7 | Bounded resource ownership, stale/scene-return safety, four normal presets, prepared shaders and matching ABI/dependency pins | Passed |
| P8 | CPU/native/runtime/editor validation on D3D12/Vulkan, reviewed motion/weather visuals and matched optimized dry/heavy GPU cost | Passed |

Windows supported ordinary non-RT scene is the qualification boundary.
OpenGL, Linux qualification, arbitrary particle materials and retail frame/FPS
identity are separate. No lower rain or impact budget is an acceptance substitute.


## Connected implementation

ParticleEmission integrates fractional births once per accepted simulation time;
each consumer retains live events independently of current emission intensity.
The stock splash adapter reads the selected asset's compiled lifetime, one-axis
initial-size flag and sixteen-sample size/alpha tables. Collision placement is
performed at birth, not re-randomized every rendering frame. Repeated/older time
and older generations cannot create new events. Long frame gaps advance IDs
with bounded catch-up work rather than dropping the entire emission interval.

Hybrid samples exposed collision surfaces in stable world cells. Its water
query uses the same current base puddle mask as environmental wet surfaces;
water events are no longer limited to the sixteen legacy rectangular hints.
The full maximum ring footprint is checked at twelve perimeter samples.
Living events retain their birth strength through rain stopping. Shelter,
finite hit/normal validation and separate solid/water budgets remain enforced.

IWorldParticleLighting accepts one joined list of stock, solid and water quads.
The game applies returned tile coordinates to all three existing material
consumers. The engine selects projected tile sizes 2..64, filters participating
lights once per tile, and computes scene-linear ambient/probe, directional and
finite-source local response with wrapping/backlighting and current shadows.
The atlas is linear RGBA16F; radiance is stored divided by 64 and restored once
by the materials. The selected stock material retains its lighting/emission
mix, atlas sampling, source-style fog-aware premultiplied composition and
existing reactive presentation. Hybrid retains its straight-alpha art shapes,
physical face orientation and the common own-depth fog composition.

Particle culling borrows the near precipitation volume's already-produced
conservative opaque-depth tiles. The lookup requires the same resource owner,
view identity/generation/frame, camera/matrix and dimensions. A quad is rejected
only when every overlapped 16-pixel tile is nearer than its closest depth;
near crossings and ranges beyond the producer are admitted. No extra scene
depth producer or fragment-by-fragment exhaustive light traversal is added.

Each backend owns four bounded view slots with per-frame descriptor/upload
storage. At most 4096 quads can be submitted; atlas overflow is explicit.
An active view atlas costs 8 MiB, with scoped admission/release and stale-view
checks. Low reduces projected lighting resolution by one level; all four
ordinary presets retain the same rain and impact geometry/count limits.
The new interface requires matching editor plugin ABI56.

## Reference anchors and explicit adapters

- Selected snapshot shaders: decoupledParticleLighting.fx,
  allocateParticleTiles.fx, include_lightingHelpers.fx and
  transparent_material_scaffold_ps.fx. CPU controller and initialization:
  renderer/src/particleCPUSimulationController.cpp, particleInitialization.cpp,
  and the compiled particle updater data. Private evidence retains the selected
  rain_ground_splash.particle resource and material candidate 076d412e7f77cf9b.
- The snapshot is not authenticated current retail. The inspected weather effect
  references falling rain, not this ground-splash resource. Its 600 births/s,
  capacity 120 and compiled curves are verified resource values; its activation
  and actual retail placement cannot be inferred from that resource alone.
- Phlosion uses Y-up coordinates, deterministic random values, collision-based
  placement around the camera and within-frame birth times. The selected source
  asset has no active collision module and disables subframe emission. Those
  placement/random/timing choices are explicit game adapters, not identical
  source memory or scheduling.
- Compiled sixteen-sample values are preserved with bounded linear interpolation.
  The generic source LightEval definition was not located in this snapshot;
  exact evaluator bit identity is not asserted.
- Source parallel tile prefix allocation/2048 storage is adapted to bounded
  CPU shelf allocation/1024 storage. Both retain projected 2..64 tile levels;
  coordinates sample texel centers. Conservative local-light masks are prepared
  per quad for the engine's supported light arrays. The source HiZ admission is
  adapted to the existing conservative 16-pixel depth tiles, with independent
  hidden/visible and wrong-view tests. These are explicit storage/scheduling
  differences, not a claim that the engines have identical dispatch graphs.
- Native point/finite-source light, channel and shadow producers are reused.
  The dominant sun/moon and existing indirect producers supply their current
  scene inputs. Source sky/sun simulation and exposure/display treatment belong
  to their own categories. This pass does not establish new parity for them.
- Hybrid deliberately retains its distinct sand/hard-surface shapes and water
  rings. It uses physical quad normals; selected stock billboards use spherized
  camera-facing normals. The camera may alter the bounded visible subset, but
  shared event IDs keep their world positions, ages and birth strength.
- CPU placement uses exposed static collision and wet-mask queries. GPU rain
  maps/opaque depth remain rendering inputs; animated actor collision is not
  read back into this CPU placement pass. Future moving interactions need an
  explicit game collision producer.

Ordinary production has no dependency on source dumps, research tools or
reference executables. Prepared shader packages are versioned in the game lock.

## CPU placement correction

Broader actual-water placement exposed a CPU regression in qualification: the
old 65,536-entry shelter-depth cache cleared the entire working set on overflow,
and every eligible ring paid twelve perimeter queries before nearest selection.
The cache now evicts the least-recently-used entry at the same capacity. Water
coverage first rejects texture-dry samples before shelter rays; perimeter tests
run in distance order and stop after selecting the same nearest 32 valid rings.
Emission, collision history, density, geometry and visible budgets are unchanged.

The dedicated 180-frame Medium diagnostic on EVGA GPU1 measured 119 post-warmup
frames: placement median 120.408 -> 5.4175 ms; host-frame median 127.861 -> 17.2389
ms. These are diagnostic comparisons of the initial implementation and corrected
implementation, not the final baseline comparison. All recorded state except the
water-query count is exact, including contact IDs/positions/ages and lighting
allocation. Water queries at the captured frame fall from 8,112 to 3,920. Images
are not bit-identical: mean absolute RGB8 difference 0.105136; 0.01347% of channels
exceed eight levels; maximum 24. Reviewed side-by-side and 10x-amplified difference
show no visible placement/detail loss. The cause of the residual pixel difference
has not been established. A 19,881-point reverse-order CPU water-query test also
preserves coverage exactly. Full matched timing is recorded separately below.

## Qualification and cost

All four affected engine CPU groups and ten affected game CPU groups pass in
optimized RelWithDebInfo builds. The new reverse-order water-cache test passes.
Both native APIs pass independent ambient/pi, wrapped direct/backlighting,
particle/channel flags, one-sided finite sources, sun, directional/local shadow,
shared-depth culling/wrong-view rejection, preset/view lifetime and ordinary
material-binding checks. Existing finite-source regression passes 16 numerical
and 16 production/domain cases per API, maximum reference error 7.47621e-07.

Twenty-eight initial strict ordinary runtime/editor cases pass: both Windows APIs, all
four presets, camera orbit, battle, dry weather, full day/weather cycles,
close impacts, daytime rain and station/reference/station return. GPU validation
reports no errors; D3D12 requires the pinned prepared shader publication with
zero runtime HLSL compilation misses. Scene return reclaims the prior payload
with no pending scopes or allocation failures. Four cycles cover rain 0..1 and
more than 23 hours, with accumulation/drying still connected. Reviewed full,
close, orbit, battle and day/cycle screenshots retain the intended Hybrid art.

The scene-shaders-v1/2026-11-02-impacts-v2 publication contains 66 prepared
programs; its publication hash is
c5aab16a7ef9182ca85333ec4b9d233d191eceaeeb74f1d2653724a5deda6a1d.
Engine/plugin ABI56 is required. Production launches use this connected path;
only normal Low/Medium/High/Ultra budgets vary.

Matched timing: gpu-worker EVGA RTX 2060 SUPER, GPU1, 1920x1080 Medium,
RelWithDebInfo, validation off, 360 frames with frames 121..359 measured.
Heavy runs are ordered previous/new/new/previous; table heavy values are the
mean of each pair's frame medians. Dry values are single matched runs. Timings
are GPU milliseconds, separate from CPU host-frame time and cold initialization.

| API | Previous dry | New dry | Previous heavy | New heavy | Heavy change |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3d12 | 13.255 | 12.970 | 18.028 | 17.970 | -0.058 |
| vulkan | 13.844 | 13.802 | 17.661 | 17.733 | +0.072 |

No sampled thermal slowdown is accepted. Raw per-run temperature/clocks, CPU
host-frame statistics, totals-only timing and separate phase diagnostics remain
in performance.json. This adds connected impact lighting; technique completion
is distinct from reaching the whole-frame 16.67 ms target.

Evidence: WORKER_EVIDENCE_ROOT/phlosion-rain-impacts-20261102 on gpu-worker; reviewed copies
under PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/rain-impacts-20261102 on controller. Failed and superseded
attempts are retained. The handoff requires unchanged source hashes, a clean
engine commit, strict game dependency configuration, affected CPU tests and four
final runtime/editor D3D12/Vulkan checks. integration-pin.json,
integration-runtime.json and completion.json record those final results.

P1-P8 is 100% estimated coverage of this fixed category, not measured retail
pixel/FPS identity, all particle systems, Linux support or whole-engine parity.

## Isolated particle shader variant

The first qualified implementation also placed the particle wrap/backlighting
branch in the shared finite-light function. Although numerical surface response
was unchanged, measured Vulkan dry GPU time increased from
13.804 to
14.684 ms and heavy time
from 17.629 to
18.568 ms. The particle
pass itself was only 0.0411 ms; rain density was not the explanation.

The ordinary finite-light source is now restored exactly to the canonical
baseline. Its 51 existing engine SPIR-V programs match the previous release
byte-for-byte; the unchanged extended response lives only in particle lighting.
This retains specialized ordinary and particle programs without asking the
driver to eliminate particle-only calculations in every surface shader.

Both native numerical suites pass again. Twelve additional strict normal-host
runs cover four runtime presets, Medium editor and reference/return on both APIs, with no
validation errors or HLSL preparation misses. Source emission, placement,
resource ownership and weather code are unchanged by this shader-only isolation;
the original 28 cases and cycle/lifetime tests remain evidence for those parts.
Final report timing is the new complete dry/heavy comparison after isolation,
not the earlier regressed candidate. Heavy Vulkan GPU time is now
17.733 ms; dry is
13.802 ms.

The portable recipes were remapped from their recorded sources using the exact
shared source blocks and existing HLSL translation rules. Every original key
was independently checked; the normal cooker validates every new request key.
Strict ordinary hosts then prove that actual renderer requests find the
prepared publication. No runtime shader-compilation fallback is used.
Prior binaries, timing, receipts and the first immutable publication are kept
as superseded evidence.

Separate instrumented phase diagnostics (not added to totals-only timings):
- d3d12: joined particle lighting 0.0349 ms GPU median.
- vulkan: joined particle lighting 0.0418 ms GPU median.
