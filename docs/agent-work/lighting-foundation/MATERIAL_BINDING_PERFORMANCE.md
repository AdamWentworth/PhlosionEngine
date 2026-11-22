# Retained material bindings and repeated view preparation

2026-10-29. Baseline engine 18c95dc409b7ceedeafeb3d2389db98c344ec635 and game
477d56cc984f4bc8b079c0f086195b0147745243. Implementation, builds, processing and
execution ran on Windows gpu-worker; controller owns review and Git integration.

## Measured target and change

The earlier static-geometry phases combine CPU preparation and native draw
submission. Diagnostic breakdown found glTF pose preflight cheap; persistent
geometry was already uploaded. Repeated material resolution cost about 1.30 us
D3D12 / 4.23 us Vulkan per call in the instrumented last twenty batches.
Native draw setup remains another substantial cost. These diagnostic values
are not matched performance results; temporary timers/logging were removed.

Normal D3D12/Vulkan managed submissions now reuse resolved texture/material
bindings across shadow, surface and reflection views. A bounded 1024-entry
CPU lookup owns its identity strings. Its identity includes all eight texture
roles, resource scope, dimensions, wrap/color-space/mip state and linear sampling.
It does not retain caller texture/mip pointers or use pointer identity.

Native caches continue to own allocation/admission and descriptors. Lookup
entries clear before scope collection, cache teardown or native material/texture
retirement. Unowned/anonymous inputs and per-frame depth snapshots use normal
resolution. Failed managed allocations cannot become cached fallback bindings.
Per-draw transforms, skinning, motion, lighting, emission/factors and draw order
stay current. No public interface/shader change, extra GPU image, quality switch
or sample reduction; plugin ABI stays 43. This is material binding reuse, not
elimination of all geometry traversal, pose preparation or native draw setup.

## Source relationship

The existing COMPLETE_TECHNIQUE.md C7/C8 path remains the contract. The supplied
snapshot's renderMaterialSetup.cxx, CRenderMaterialSetup::GetMaterialSetupEntry,
retains material setup entries; renderMaterial.cxx,
CRenderMaterial::PrepareRenderStagePassesRange, reuses prepared stage ranges with
explicit allocation-frame ownership. Function anchors/hashes are in trace.json.
Phlosion's independent resource-scoped binding lookup removes repeated resolution
inside its existing renderer. It does not implement the complete source pass/job
cache, parallel collector or full C7/C8 architecture. Research source stays
read-only and is not a production/runtime dependency.

## Verification

- Optimized RelWithDebInfo engine editor, game runtime/plugin and tests build.
  EngineContracts and WorldMotionHistory CPU groups pass. Binding contracts cover
  owned identity text, scopes, all texture/sampling key fields, dynamic constants,
  unmanaged/snapshot bypass, bounds and explicit invalidation.
- Four native cases (reuse/retire on each API) compare scoped reuse against normal
  unowned resolution. All four image pairs are pixel-exact with no API errors.
  The cases include two simultaneous scopes using the same key for different
  images, changing per-draw factors, changed maps and native retirement/reuse.
  Early fixture attempts assumed pure display RGB; those failed captures/logs
  are retained. The final fixture compares exact reference pixels instead of
  assuming tone mapping preserves saturated source RGB. No existing test
  threshold or renderer color behavior was changed.
- Twelve full-scene pairs cover runtime wet/dry/orbit/battle and editor wet/battle.
  Required surface/cluster/probe phases and rain state agree. No API errors or
  invalid/dropped profile frames; no fresh SSR source draw.
  Scene maximum RGB8 difference 6;
  worst mean 0.008355.
  Every pair was visually reviewed; full-game byte identity is not claimed.
- All shader binaries and material payloads match baseline. Candidate changes
  four packaged binaries only. Hybrid surface-specific splashes and puddle rings,
  foliage motion, lighting math, resolution and budgets are unchanged.
- EVGA RTX 2060 SUPER GPU1, LUID 000000000000be8e,
  UUID GPU_1_UUID. Linux and shelved OpenGL are not
  newly qualified.

## Matched warm performance

Native 1920x1080 Medium, fixed night/overcast, heavy Hybrid rain and a matching
dry control. Optimized MSVC RelWithDebInfo; validation off, profiling on.
Wet ABBA and dry AB per API; 220 frames/run, analyze 40..179:
280 wet samples/version and 140 dry. All twelve runs accepted, equal settings,
no API errors or thermal flags; maximum 62 C.
No builds or processing overlapped timing. All values are milliseconds.

| API / scene | GPU mean | Whole CPU mean | Active CPU mean | Whole CPU p95 | Whole CPU p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3d12 / wet | 17.656 -> 17.267 | 19.155 -> 18.545 | 15.498 -> 14.970 | 29.774 -> 28.843 | 33.614 -> 30.747 |
| d3d12 / dry | 12.264 -> 12.117 | 13.093 -> 12.979 | 10.289 -> 10.317 | 22.545 -> 22.759 | 23.337 -> 24.164 |
| vulkan / wet | 18.428 -> 18.414 | 18.255 -> 18.277 | 12.111 -> 8.835 | 19.445 -> 19.345 | 21.245 -> 20.781 |
| vulkan / dry | 13.600 -> 13.572 | 13.417 -> 13.399 | 8.138 -> 5.757 | 14.497 -> 14.253 | 14.736 -> 14.525 |

| Wet CPU phase (ms) | D3D12 before -> after | Vulkan before -> after |
| --- | ---: | ---: |
| field-view/shadow/static-geometry | 2.943 -> 2.696 | 1.702 -> 0.903 |
| field-view/contact-depth/static-geometry | 2.479 -> 2.373 | 2.251 -> 1.372 |
| field-view/reflections/capture-surfaces/static-geometry | 2.448 -> 2.393 | 1.917 -> 1.224 |
| field-view/local-shadows/actors | 1.571 -> 1.524 | 1.217 -> 0.925 |

Vulkan wet active CPU improved 27.0% (3.276 ms), and dry active CPU
improved 29.3% (2.380 ms). Wet GPU time and whole-frame time are effectively
unchanged on Vulkan: the reduced CPU work becomes waiting for the GPU.
D3D12 wet active CPU improved 3.4% (0.528 ms); its measured wet GPU mean
improved 0.389 ms, but GPU median moved only 0.085 ms. This change does not
reduce shader work. Do not extrapolate that mean into a sustained GPU gain.
D3D12 dry active CPU is effectively unchanged/slightly higher (0.028 ms);
its whole-CPU p95/p99 increased by 0.214/0.827 ms. Wet tails improved on
both APIs, but D3D12 tails remain above the frame target.

Timing images were reviewed separately: Vulkan maximum one RGB8 step;
D3D12 wet maximum six and dry maximum thirty, with only 29 dry pixels
differing by more than four steps across 1920x1080. Enlarged worst-point
crops show sparse differences around the pumps with no coherent surface
loss. Full-scene image identity is not claimed.

Whole CPU includes waits/presentation; active CPU excludes them. Nested phase
times must not be added to their parent totals. Mean, median, p95/p99, memory,
per-run results and timing images remain in analysis-matched.json. This CPU
change does not promise an equal GPU reduction or establish sustained 60 FPS.
Cold shader preparation, Linux and retail Cyberpunk comparison remain separate.

## Remaining work

Native draw state/lighting packet preparation and traversal still repeat across
views. Continue from those measured costs and wet GPU phases. Keep the 16.67 ms
wet-frame target and CPU tails as separate gates. Technique percentages and
C7/C8 completion do not change merely because a cache is faster.

Worker evidence: WORKER_EVIDENCE_ROOT/phlosion-geometry-preparation-performance-20261029.
Small receipts/reviews are mirrored under the engine's ignored
artifacts/lighting-foundation/geometry-preparation-performance-20261029 folder.
Full raw profiles and diagnostic/correctness images remain on the worker.
Key receipts: trace.json, build.json, tested-source.json, native-candidate/checks.json,
cpu.json, qualification.json, payload-comparison.json, visual-review.json,
analysis-matched.json, timing-visual-review.json, strict-build.json,
locked-payload-comparison.json and integration-final.json.
