# Local shadows - fixed completion contract, 2026-11-03

Status: L1-L8 implemented and qualified; 100% estimated coverage of the fixed
contract, subject to the matching committed-head integration receipts below.
Acceptance was fixed before implementation. This is the ordinary
Windows D3D12/Vulkan local raster-shadow category. Directional sun/distant
cascades belong to the separate sky/global-lighting work; hardware RT, HDR
monitor transport, Linux and retail pixel/FPS identity are separate.

## Required gates

- L1: Explicit supported spot/capsule admission, authored shadow coverage/range,
  softness, static-only policy and view-distance opacity. Ordinary point sources
  do not generate raster maps in the inspected source branch.
- L2: Source projection/near/range and receiver-distance conventions, including
  rotated and very short sources, independent of their luminous mesh dimensions.
- L3: Persistent static radial depth, current dynamic depth, nearest-depth merge,
  source-snapshot separable kernels 2/5/10/15 and RG16 UNORM intermediate/final
  quantization. Preserve a separately named captured-revision numerical fixture;
  its per-tap ray normalization differs from the supplied snapshot's center ray.
- L4: Bilinear moment receivers with variance/bleed thresholds, bounded atlas
  addressing and per-light opacity, consumed consistently by forward/clustered,
  reflected surfaces, foliage, transparents and particles. The inspected volume
  shader does not enable local shadow maps; preserve its separate sun-shadow path.
- L5: Opaque/cutout/current rigid/skinned/wind casters and correct static/dynamic
  changes, removal, frozen-pose reload and scene/view return. No stale owner,
  projection, filter or participation result after authored changes.
- L6: Existing W1-W8 cache ownership/fair scheduling and independently fenced
  backends remain qualified; normal presets adjust budgets without feature opt-in.
- L7: Independent numeric/projection/filter/receiver checks, analytic caster
  controls and native validation on both APIs, including tile edges and lifetimes.
- L8: Normal runtime/editor, exploration/battle, all four presets, camera and
  day/weather/scene transitions, strict prepared shaders, affected CPU tests,
  visual inspection, optimized matched GPU costs and locked committed integration.

Reference: supplied 2021-06-10 source snapshot, not authenticated current retail.
Source files are read-only. Independent engine code and explicit atlas/API
adapters implement the mechanism. No new percentage is assigned until the gates
are implemented and verified.

## Connected implementation

| Contract | Source trace | Engine/game integration |
| --- | --- | --- |
| Admission and authored controls | renderProxyPointLight.h; renderProxyLight.cpp 244, 310-322, 644-670 | WorldLocalShadowSettings, spot/capsule admission, OutdoorProfile validation; ordinary non-cone point lights do not allocate shadow maps. |
| Projection and caster range | renderProxySpotLight.cpp 105/130; renderProxyAreaLight.cpp 111-128/184; renderShadowManager.cpp 680-950 | IWorldLocalShadow separates full-range raster/receiver normalization from authored caster-query range. glTF rigid/skinned bounds and foliage revisions use that query in field and battle. |
| Static/dynamic merge and filter | renderShadowManager.cpp; vsmFilter.fx | Persistent static radial cache plus current dynamic perspective depth; nearest radial merge; center-ray dynamic taps; 2/5/10/15 kernels and RG16 UNORM quantization. |
| Receivers | include_lightingHelpers.fx | Bilinear distance moments, 0.00001 minimum variance, 0.04 bleed threshold, XY admission and clamped radial receiver depth, per-light opacity; native spots share the existing forward/clustered/particle routes. |
| Lifetimes and budgets | renderShadowManager.cpp; previously qualified W1-W8 | Raw cache invalidates on source/scene/geometry/projection/query change. Filter/static-only changes refresh final moments; opacity-only changes reuse them. Static-only ignores dynamic revisions. Existing fair scheduling and fenced frame/view banks remain. |

Skinned caster bounds are conservative unions of transformed bind boxes prepared
once per retained pose; normalized linear blends and residual bind weights are
contained. Unsupported inputs fail open. The selection key separates shadow
queries from normal visible-character drawing. Native fresh/retained/collected
image controls and independent weighted-point bounds tests cover this adapter.

The inspected volume shader's compile configuration does not enable local shadow
maps. Local-light injection retains that behavior and its separate sun path;
adding shadow maps there would contradict the inspected branch.

## Numerical and native evidence

Worker root: WORKER_EVIDENCE_ROOT/phlosion-shadows-20261103. Controller handoff:
PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/shadows-20261103.

- source-manifest.json identifies read-only inspected source inputs.
- tests/shadow_source/reference.py independently evaluates float64 projection,
  center-ray filtering, intermediate quantization and receiver visibility.
  All 32 GPU cases pass: maximum error 4.62159e-08 D3D12, 8.32573e-08 Vulkan.
- The historical captured-revision fixture remains separate and passes. Its
  dynamic taps use each tap's ray; the supplied source uses the center ray.
- native-qualified.json: 146 passing jobs across both APIs, with executable
  hashes. Analytic blocked/unblocked and empty/cleared controls cover native
  spot/capsule lights, four kernel widths, fades, cutouts, skinning, clustered
  receivers, atlas borders and radial range edges.
- Thirteen cache/lifetime scenarios per API compare retained and scheduled
  results to the same source graph rebuilt every frame, byte for byte. They cover
  motion/removal, dynamic changes, scene/projection/source replacement, masking,
  reset, reorder, sustained updates and multiple views.
- cache-reference-audit.json preserves why a combined-raw-depth diagnostic is
  a different oracle: cached static depth already contains per-tap radial
  conversion, while live dynamic filtering uses the center ray. The projection
  case differs by one 8-bit code value in two pixels; the actual source-graph
  refresh controls are exact.
- Particle native tests include a spot-shadow index above the rectangle range.
  Motion-producer tests and retained glTF controls pass on both APIs.
- CPU evidence covers authored validation, conservative posed queries, cache
  invalidation, static-only scheduling, opacity reuse and engine contracts.

## Explicit adapters and limits

Twenty addressable sources (sixteen area slots plus four point/spot slots) share
eight resident 1024-square local tiles in the existing 4096-square 2D atlas.
The source uses array storage; addressing and fenced lifetime are engine adapters.
Quality tiers retain their existing update budgets. Legacy LTC/finite-line and
PCF diagnostics remain supported; ordinary native cone sources use the inspected
moment path. A 0.02 near floor guards singular zero-length capsule projections.
The authored 150-degree default and view-distance defaults remain scene policy;
the source formulas, authoring controls and invalidation are connected.

No claim of current retail pixels, all source branches/platforms, ray-traced
shadows, directional-sun completion or whole-engine 100% follows from this row.
Editor ABI59 and model-plugin ABI31 require matching engine/game/providers.

## Ordinary hosts and prepared programs

Twelve complete-package runtime/editor checks and fourteen strict prepared
checks pass across D3D12/Vulkan, all four graphics presets, field/battle,
orbiting cameras, Colosseum scene return and full day/weather sweeps.
API validation reports no errors. Final posed-caster native comparisons pass
on both APIs; engine contracts and all six affected game contracts pass.
Visual inspection covers daylight, rain/night, orbit and battle views.
The normal scene retains Hybrid splashes/rings, full lighting and existing
preset budgets. No shader feature opt-in was added.

Publication scene-shaders-v1/2026-11-03-shadows carries the pinned prepared
programs. The generated world and rain SPIR-V deployment is verified separately.
Earlier incomplete harness deployments, failed fixtures and superseded attempts
remain in evidence; only complete-host-* and strict-* count as host acceptance.

Completion checks finished on 2026-11-04 after the worker restart.

## Matched GPU cost

EVGA RTX 2060 SUPER GPU1, 1920x1080 Medium, optimized RelWithDebInfo,
validation off. Heavy rain uses ABBA order; dry uses the same night camera
with rain zero. Accepted run medians use frames 121..239 (119 samples),
valid GPU totals and no dropped spans. Heavy values below average accepted
run medians; per-run GPU/CPU values and temperature samples are preserved.

| API | Dry before | Dry current | Heavy before | Heavy current |
| --- | ---: | ---: | ---: | ---: |
| d3d12 | 10.632 ms | 11.281 ms | 14.872 ms | 15.108 ms |
| vulkan | 11.610 ms | 11.598 ms | 15.061 ms | 15.044 ms |

3 run(s) excluded: sampled thermal slowdown or an unpaired pre-reboot control. The detailed accepted
and rejected receipts are performance.json and timing-rejected.json. These are
workload GPU costs, not a retail comparison or an end-to-end 60 FPS guarantee.
Heavy D3D12 increased by 0.236 ms; Vulkan heavy was effectively unchanged.
Dry D3D12 increased by 0.649 ms in this short pair. Quality was not reduced.
Vulkan dry measurements were repeated after the shutdown with the same physical
GPU UUID and its new Windows LUID. The interrupted run and a failed stale-LUID
launch are retained but excluded from timing.

## Integration and receipts

gpu-worker performed all implementation, builds, tests and processing. controller
owns reviewed source integration and commits. Editor ABI59/model ABI31,
matching providers, the engine dependency pin and shader publication are required.

qualification.json, native-qualified.json, source-manifest.json,
cache-reference-audit.json, performance.json and the CPU logs describe the
qualified code. integration-pin.json and integration-runtime.json must confirm
the final committed heads with locked dependencies and strict runtime/editor
launches on both APIs before this category is delivered as complete.
