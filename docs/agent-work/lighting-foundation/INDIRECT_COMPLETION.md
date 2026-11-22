# Indirect lighting completion contract — 2026-10-31

Status: **100% estimated technique coverage of the fixed I1-I8 supported Windows
contract (8/8 qualified)**. Baseline engine c071f94, game ff29b51, ModelSupport
c7f972e. The requirements recorded before implementation were not removed or reduced.

This category means the supplied snapshot's ordinary raster indirect technique
for the supported Windows D3D12/Vulkan scene and material producers. It does not
mean retail pixel identity, hardware RT, an entire city streaming system, hair
scattering, or Linux qualification. The independent engine implementation must
not require the private source tree, research tools, or runtime geometry baking.

## Fixed gates

| Gate | Required mechanism | Verification required |
| --- | --- | --- |
| I1 | Prepared surfels grouped into directional bricks, material/emission and nearest-probe ownership; directional probe-to-brick factors and sky visibility; tetrahedra, adjacency and per-vertex visibility; validated versioned cooked data. | Reproducible actual-scene bake, independent geometry/visibility and malformed-data fixtures, identity/staleness checks. |
| I2 | GPU brick relighting from current sun visibility, selected local GI lights, emission and previous probe energy; bounded multibounce and saturation/energy compensation. | Separate sun/local/emission/previous-generation numerical fixtures, participation, shadow cache and cold/reset checks on both APIs. |
| I3 | GPU six-axis probe gather from brick factors, sky visibility and luminance-limited missing-energy fallback from prepared environment ambient. | Independent directional/energy/convergence expectations; completed-generation publication, no readback or per-object relighting. |
| I4 | Accelerated tetrahedron lookup and bounded neighbor traversal; visibility-reconstructed depth penalties, normal/distance weighting and normalized interpolation; interior/exterior separation. | Boundary, wall leakage, degeneracy, invalid/missing cell, channels and moving receiver sequences. |
| I5 | Prepared six-axis environment ambient and distant terrain relighting/fallback; blend near GI with environment ambient and corresponding reflection dimming. | Day/night/weather/light changes; near-to-distant continuity, missing-data fallback, no accidental feedback or duplicate ambient. |
| I6 | Actual standard, specialized character/foliage, reflected and supported forward consumers; shared evaluated opaque path and explicit material response. | Runtime/editor, field/battle/return, dry/wet, metal/unlit/masked and moving/skinned/wind producers on both APIs. Preserve Hybrid impacts. |
| I7 | Resident immutable transport, bounded relight scheduling, prewarm, age/fairness, visibility priority, invalidation, reset and frame/view ownership. | Overflow, eviction/reload, paused weather, multiple views and failed/cancelled publication; debug/validation clean. |
| I8 | Normal Low/Medium/High/Ultra path, exact-source evidence, numerical and visual review, optimized timing and memory accounting. | Same worker/GPU, locked before/after payloads, CPU/GPU distribution and tails. No retail FPS or fixed-60 claim from technique completion. |

None of these gates may be removed to turn an intermediate milestone into 100%.
Representation, capacities, scene bake density and Y-up coordinates are explicit
adapters. Alternative equations or omitted connected stages remain open gaps.

## Source trace

Read-only private snapshot, R6.Root/Mainline:
- dev/src/common/renderer/src/renderGIManager.cpp: Tick, PrepareSectorLights, LitSectors,
  LitSectorBricks/Probes, BuildVolume and distant relighting.
- dev/src/common/renderer/src/lightsPacker.h: PURPOSE_GI selection; area sources use the same
  simplified point/spot packing as indirect reflection lighting.
- engine/shaders/surfelLit.fx: registered brick, probe and feature variants.
  Six squared-normal axes, not the existing SH9 canopy transfer.
- engine/shaders/include/include_gi.fx: accelerated neighbor walk, tetrahedral
  visibility patches and penalty-weighted barycentric interpolation.
- engine/config/giweights.json: shadow distance .1, normal .2, probe distance 0,
  bias 0, threshold .28.
- engine/shaders/globalIlluminationCalculate.fx and include/include_envProbe.fx:
  shared-surface resolve, missing-data/edge blend, probe ambient/reflection coupling.
- engine/shaders/distantIrradiance.fx: terrain surface relighting and spatial reuse.
- dev/src/backend/backendPipeline/src/giProbeBakingUtility.cpp and
  dev/src/common/worldPrecomputedLight/include/giData.h:
  prepared surfels, brick influences, sky fractions, tetrahedra/visibility data.

Public background: [Precomputed Lighting in Call of Duty: Infinite Warfare,
SIGGRAPH 2017](https://advances.realtimerendering.com/s2017/Precomputed%20Lighting%20in%20CoD%20IW_20_PPS.pdf).
This is background for visibility separation, not evidence of Phlosion completion.

## Implementation and explicit adapters

The versioned preparation tool produces static surface samples, 4 m directional
bricks, six-axis probe factors, sky fractions, tetrahedral adjacency/visibility
and distant terrain. The game verifies geometry, mapped normals, emission
bindings and the preparation receipt before admitting this data. Runtime never
opens the research tree or bakes geometry.

Four shared GPU stages prepare environment ambient, relight bricks, gather
probes and relight distant terrain. Ambient preparation is reused until the
published environment generation or source parameters change. The previous
bounce and missing-energy ambient have separate 1000-unit clamps; the missing
fraction additionally uses the 1.2 luminance bound. Ordinary transparent
materials skip distant-terrain modulation. Local-source admission also preserves
the radius and squared-energy cutoff before brick culling.

Both backends keep eight immutable transport entries, two result banks per
entry, fenced replacement, immutable dispatch constants and frame-owned
consumer bindings. Plugin-owned immutable inputs name their world resource scope.
Scope release drops CPU control blocks while plugin code is still loaded; queued
GPU buffers remain protected by fenced cache replacement. The native regression
checks both input retention and release before GPU completion. Prewarm, visibility,
age/fairness and tier budgets run before publication. Normal field and battle supply independent ownership; editor and
runtime use the same game binding. The former SH9 canopy transfer remains an
independent legacy/calibration API, not the normal station path.

The ordinary shared evaluated-material path, composed D3D12 PBR adapter,
specialized character adapters and leaves consume the prepared field. Their
authored BRDF and translucency remain separate material-category work.
Probe reflection dimming uses the same nearby energy and prepared ambient.
A dedicated full-resolution GI receiver pass reconstructs the same positions
and evaluated normals used by primary surface composition. Ambient and reflection
dimming consume its shared result. Forward/specialized consumers retain direct
queries. This follows the reference's separate receiver pass; compute dispatch
and RGBA16F storage are explicit API/representation adapters. No resolution,
visibility-step or light-count reduction was introduced.
The receiver texture contains radiance and blend weight. Its logical storage is
16,588,800 bytes at 1920x1080 per active view/frame slot (31.64 MiB for one view
across the two frames in flight), plus implementation allocation alignment.
Each resolve owns a bounded frame slot and fenced resources, including repeated
resolves of the same surface. The cap is sixteen resolves per frame.
Rebinding or clearing the world field invalidates the prepared receiver.

Scene settings and representations are adapters: Y-up coordinates; a regular
tetrahedral lattice with per-cell acceleration; 0.25 m surface spacing; 64-face
preparation; 16 m distant-terrain cells; float32 GPU rows with packed surface
attributes; cube-based environment integration rather than source storage.
The per-face backface/obstruction validity mechanism is traced; the preparation
acceptance threshold of 0.5 is authored because its defining source constant
was not present in the supplied files. This is not asserted as that constant's
recovered value. The current sun/moon policy selects the dominant directional
key; the existing point/rectangle light contract supplies GI point proxies.
Broader light variants, material systems and world streaming retain their own
matrix categories.

The scene has 1,995 probes, 2,616 bricks, 481,074 surface samples, 244,328 factors,
8,640 tetrahedra and 4,096 distant-terrain cells. Per resident scene, immutable
GPU rows occupy 22,806,304 bytes and the two output banks 16,257,472 bytes.
A retained upload allocation adds another 22,806,304 bytes; CPU transport and
packed rows are additional. These counts are not total process/driver VRAM.

## Evidence and qualification

All eight fixed gates are qualified against the supplied ordinary raster
snapshot for the supported Windows path. Source anchors/hashes are recorded in
source-anchors.json; source-r10.json and tested-code-comparison.json identify the
tested implementation. Controller commits and dependency/payload verification
are recorded separately in finalization.json.

| Gate | Acceptance evidence |
| --- | --- |
| I1 | cook-reproducibility.json; cook-r10.json; cpu-r10-0.log; source-anchors.json |
| I2 | native-r10-d3d12.log; native-r10-vulkan.log: isolated sun/local/emission/clamps/previous generations |
| I3 | native-r10-d3d12.log; native-r10-vulkan.log: six-axis gather, fallback bounds, ambient invalidation |
| I4 | cpu-r10-0.log; numerical-consumers-r10.json: visibility/volume/edge/distant receiver expectations |
| I5 | scene-observations-r10.json: complete day/weather sweeps; native near/distant/probe/reflection cases |
| I6 | numerical-consumers-r10.json; primary-regression-r10.json; scene-r10.json; visual-review.json |
| I7 | native-r10-d3d12.log; native-r10-vulkan.log: scope release, revision/reset, 8-entry pressure, frame budgets and failures; scene-r10.json: editor shutdown and battle return |
| I8 | analysis-matched.json; timing-clocks.json; source-r10.json; tested-code-comparison.json; finalization.json (separate controller handoff receipt) |

- Eleven engine and seven game CPU groups pass on freshly built executables;
  the unchanged character shader inputs retain eight material-contract checks
  and the model boundary check.
- Both APIs pass independent sky/sun/local/emission/multibounce, high-energy
  limits, terrain recurrence, visibility/interpolation, cache/scheduling,
  malformed/reset and scope-lifetime expectations.
- Eighteen material/control cases plus fourteen prepared receiver cases per API include standard,
  leaf and SV subsurface response, reflection dimming, forward exclusion,
  near/distant/volume/priority/channel and fallback controls. Expected display
  error is below three 8-bit levels; exact per-case and cross-API errors are recorded.
  Compute recurrence error is at most 1.581e-7 in the ordinary fixture.
- Twenty-four existing primary/reflected/secondary composition regressions pass
  after rebuilding both native executables. D3D12 debug-layer and Vulkan
  validation report no errors.
- Twenty-six normal runtime/editor scene checks pass: wet/dry/orbit/battle,
  all presets, complete day/weather sweeps and battle return. Actual GI pass
  execution and rain/hour ranges are checked, not inferred from a setting.
- The actual station preparation reproduces byte for byte. Later final-link
  preparation matches the same SHA-256. Hybrid material-specific impacts and
  puddle rings are retained; measured before/after precipitation state is exact.

## Measured cost

Sixteen fresh ABBA runs use the same EVGA RTX 2060 SUPER (GPU1,
LUID 000000000000be8e), native 1920x1080 Medium and optimized RelWithDebInfo.
Validation and diagnostic readbacks are disabled for measured frames. Each
API/scene/variant contributes 280 accepted steady frames; weather, renderer and
quality settings match. These are Phlosion comparisons, not retail benchmarks.

| API / scene | GPU median ms, before -> after | GPU p95 ms | CPU frame median ms | CPU frame p95 ms |
| --- | --- | --- | --- | --- |
| d3d12 / wet | 15.097 -> 19.433 | 17.019 -> 20.652 | 14.341 -> 19.254 | 24.992 -> 22.966 |
| d3d12 / battle | 15.232 -> 20.298 | 16.336 -> 21.434 | 14.724 -> 20.055 | 28.661 -> 30.793 |
| vulkan / wet | 16.608 -> 19.943 | 17.757 -> 21.341 | 16.530 -> 19.848 | 17.681 -> 21.158 |
| vulkan / battle | 15.962 -> 19.437 | 17.095 -> 20.733 | 15.871 -> 19.355 | 17.218 -> 20.770 |

The new connected indirect path adds **3.3-5.1 ms** to these scenes versus the previously accepted simplified bounce. This is a technique-completeness change with a measured performance regression; the 16.67 ms whole-frame target remains open.

Reviewed night images contain darker, sometimes silhouetted characters facing away from fixtures. Coloured bounce, reflected surfaces and transitions remain coherent. Gameplay readability and exposure tuning remain separate presentation work.

Maximum accepted worker temperature: **64 C**. No accepted run has a thermal-slowdown flag.

The first-use diagnostic launches (including their short scripted runs) took d3d12: 714.8 s, vulkan: 590.2 s. Their startup cost is excluded from steady-frame timings and remains an explicit loading problem; see receiver-diagnostic-r10.json.

Full mean/median/p95/p99 GPU, CPU frame, present/wait and active-CPU distributions,
clock samples and observed total GPU memory are retained in analysis-matched.json
and timing-clocks.json. The retained/upload/working allocation counts above are
separate from driver/process VRAM. Cold shader preparation remains expensive;
normal scene launch durations are recorded in scene-r10.json. Startup and the
separate 16.67 ms whole-frame target are not closed by this category.

Evidence: artifacts/lighting-foundation/indirect-completion-20261031/evidence.zip.
Raw proprietary source, builds and extracted game assets are excluded.

## Failures retained during qualification

The first full editor run rendered correctly but crashed during shutdown in
the indirect cache entry destructor. A retained shared-pointer control block
referenced the unloaded game plugin. An initial non-owning address-key attempt
then failed the independent animated-emission fixture when an allocator reused
an address. Neither version is accepted.

The replacement integrates with existing world-resource scope release. It
retains immutable input ownership during use, releases plugin control blocks
before unload, and keeps GPU storage fenced independently. Numerical emission,
explicit owner release, full editor shutdown and scene return all pass in the
final r10 qualification. The failed runs and symbolized fault remain evidence.


The fused receiver revision (r8) passed correctness but increased median GPU
time by roughly 3-8 ms in matched scenes. Shader-private reuse (r9) preserved
images; its diagnostic battle medians were 21.56 ms on D3D12 and 21.17 ms on Vulkan. Those versions
were not integrated. The final implementation separates receiver preparation
from primary surface shading; all failed/intermediate checks remain in the
evidence. Early r10 build diagnostics also exposed and corrected a C++ string
assembly error, a test include, and the command recorder's new binding capacity.
The Vulkan rebind fixture then caught descriptor updates invalidating earlier
commands. Per-resolve frame ownership fixes that lifetime on both backends.
