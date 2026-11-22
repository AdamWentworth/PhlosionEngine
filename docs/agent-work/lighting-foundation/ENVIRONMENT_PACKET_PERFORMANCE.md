# Reusing environment and light packets across draws

2026-10-29. Baseline engine cbe6468e695a15e5afe21aef85ae07b4c80038ce,
game 2df1af23a1b863978c846ae7301e7078027f6343. Implementation, builds,
tests and processing ran on Windows gpu-worker. controller owns review/integration.

## Change and boundaries

The ordinary D3D12 and Vulkan draw paths repeatedly validated and packed the
same environment, point lights and area lights. Each separate light pack also
repeated the complete environment validation. Both backends now retain eight
complete input snapshots and their packed environment/light/receiver data.
A miss validates the environment once; point and area inputs still receive
their original independent validation. Existing one-argument packing APIs
keep their previous behavior.

The cache owns copied input representations, never caller pointers or GPU
resources. Full snapshots preserve all per-draw overrides, including weather,
exposure, lights and reflection response. Representation comparison preserves
signed-zero/NaN distinctions. Padding differences can cause a safe cache miss;
they cannot alias a different input. Fixed storage bounds memory and eviction
only recomputes values. D3D12 pipeline selection shares the same packet, and
Vulkan ordinary and indirect submissions both use it.

Camera, transforms, skinning, motion, shadow/probe/history state and native
allocation remain in their existing live paths. There is no GPU-buffer lifetime
change, new public virtual, shader/layout change, quality flag or sample cut.
Plugin ABI stays 43. GPU view uploads and remaining draw setup are not eliminated.

The supplied snapshot binds global constants at explicit render-graph stages
(renderGraphNodes.cpp, CRenderNode_BindGlobalConstants::Execute, line 1270),
alongside retained material-stage preparation documented by the prior pass.
This independent CPU cache separates reusable values from draw state; it is
not the complete source global-buffer/job architecture. Source hashes/function
anchors are in trace.json; research remained read-only and is no runtime input.
Technique percentages and C7/C8 completion do not increase from this optimization.

## Verification

- Optimized RelWithDebInfo engine editor, runtime/plugin and native targets build.
  EngineContracts, WorldMotionHistory and EnvironmentPackets CPU groups pass.
  New contracts compare byte-exact packets against fresh packing across changing
  light/weather/receiver inputs, invalid values, signed zero, bounded eviction
  and same-address input mutation.
- Twenty validation-enabled native runs pass on D3D12/Vulkan: four binding cases
  plus fresh/retained sun/light, wetness, emission and haze relighting on each API.
  All twelve native reference image pairs are pixel-exact.
- Twelve runtime/editor comparisons cover runtime wet/dry/orbit/battle and
  editor wet/battle. Baseline captures are reused from the immediately previous
  accepted candidate, whose final locked payload was byte-identical; provenance
  is recorded in reused-correctness-baseline.json. All twelve candidate runs
  are fresh. Required surface/cluster/probe phases and precipitation state agree;
  no API errors, invalid/dropped profile frames or fresh SSR source draw.
  Every pair was visually reviewed; maximum RGB8 difference
  4, worst mean
  0.006358. Full-scene bit identity
  is not claimed.
- All shaders and materials match baseline byte for byte; four packaged binaries
  changed. Rain quantity, Hybrid surface-specific splashes/puddle rings, lighting
  response, foliage, resolution and effect budgets are unchanged.
- Windows EVGA RTX 2060 SUPER GPU1, LUID 000000000000be8e,
  UUID GPU_1_UUID. Linux is not newly qualified.

## Matched warm performance

Native 1920x1080 Medium, fixed night/overcast, heavy Hybrid rain and matching dry
control. MSVC RelWithDebInfo, validation off, profiling on. Fresh wet ABBA and
dry AB on each API; 220 frames/run, analyze 40..179 (280 wet samples/version,
140 dry). All twelve runs accepted with equal settings, no API errors or thermal
flags, maximum 63 C.
No builds or other task processing overlapped timing. Milliseconds:

| API / scene | GPU mean | Whole CPU mean | Active CPU mean | Whole CPU p95 | Whole CPU p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3d12 / wet | 17.429 -> 17.625 | 18.491 -> 18.539 | 14.467 -> 14.409 | 28.497 -> 28.548 | 31.557 -> 30.526 |
| d3d12 / dry | 12.208 -> 12.421 | 13.756 -> 13.253 | 10.829 -> 10.110 | 22.628 -> 22.385 | 30.719 -> 23.612 |
| vulkan / wet | 18.438 -> 18.351 | 18.275 -> 18.171 | 8.782 -> 8.234 | 19.356 -> 19.150 | 20.235 -> 21.034 |
| vulkan / dry | 13.559 -> 13.539 | 13.509 -> 13.438 | 6.075 -> 5.618 | 14.351 -> 14.323 | 17.051 -> 16.569 |

| Wet CPU phase (ms) | D3D12 before -> after | Vulkan before -> after |
| --- | ---: | ---: |
| field-view/shadow/static-geometry | 2.538 -> 2.431 | 0.900 -> 0.753 |
| field-view/contact-depth/static-geometry | 2.319 -> 2.222 | 1.369 -> 1.271 |
| field-view/reflections/capture-surfaces/static-geometry | 2.302 -> 2.262 | 1.228 -> 1.127 |
| field-view/local-shadows/actors | 1.461 -> 1.497 | 0.926 -> 0.886 |

Vulkan active CPU improved 0.547 ms (6.2%) wet and 0.457 ms (7.5%) dry.
D3D12 wet active CPU mean is effectively flat (-0.058 ms, 0.4%); its
median improved 0.305 ms and p95 improved 0.634 ms. Dry active CPU mean
improved 0.719 ms, but the median worsened 0.673 ms, so that single dry
pair is mixed rather than a robust general speedup.

There is no meaningful GPU/frame-rate win: D3D12 GPU mean increased
0.195 ms wet and 0.212 ms dry; Vulkan decreased 0.088/0.021 ms. These
are observations with unchanged shader work, not a claimed GPU optimization.
Vulkan whole-CPU wet p99 increased 0.799 ms even though its mean/p95 improved;
D3D12 wet whole mean/p95 are flat while p99 improved. Keep these regressions
and variability visible. No sustained 60 FPS claim.

The retained packet is also a concrete preparation step for separating shared
view uploads from per-object transforms; current native per-draw copies/binds
are deliberately still present. The user's architectural-work authorization
does not require an immediate gain in every backend, but final performance
and quality acceptance remain mandatory.

Whole CPU includes waits/presentation; active CPU excludes them. Nested phases
must not be added to parent totals. Per-run/pooled mean, median, p95/p99 and
image comparisons remain in analysis-matched.json. This CPU change does not
reduce shader work or establish sustained 60 FPS.

## Next target

Contact-horizon shading remains a substantial GPU cost: before this pass it
measured about 1.37 ms on both APIs. Trace its repeated depth-to-position
reconstruction and prepared input layout while preserving all fifteen directions
and ten samples. Rain-far was about 1.5 ms; haze integration about 0.72/0.86 ms
D3D12/Vulkan. Qualify any shader/input-layout change separately. Remaining draw
setup, D3D12 CPU tails, cold shaders, C7/C8 and the 16.67 ms gate remain open.

Worker evidence: WORKER_EVIDENCE_ROOT/phlosion-draw-state-performance-20261029.
Small receipts/images are mirrored under the ignored engine
artifacts/lighting-foundation/draw-state-performance-20261029 directory.
Key receipts: trace.json, build.json, tested-source.json, cpu.json,
native-candidate/checks.json, qualification.json, reused-correctness-baseline.json,
payload-comparison.json, visual-review.json, analysis-matched.json,
timing-visual-review.json, strict-build.json, locked-payload-comparison.json
and integration-final.json. Raw profiles and native images remain on the worker.
