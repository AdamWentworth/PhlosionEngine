# Probe filtering batches and CPU update bursts

2026-10-29. Baseline engine f9314fdea358eb05cdc87c9f71034c40fc74af15 and game
23a40c3b350fa2712ce7fb32f18ed413c7008b7a. All implementation, builds, image
processing and execution use Windows gpu-worker. Controller owns review and Git.

## Connected change

Ordinary D3D12/Vulkan probe filtering groups six independent faces at each
roughness level into one 3x2 scratch draw. Copies publish those six regions after
the draw, before the next dependent level. Prepared filtering drops from 60
draws to 10; synchronization groups also drop from 60 to 10. Vulkan uses one
multi-region copy command per group. D3D12 keeps six region copies per group.
The five-level box pyramid and 64-sample GGX convolution are unchanged.

The existing scene target is the scratch buffer; no extra image is allocated.
Each level falls back to individual faces when its packed rectangle would exceed
the actual runtime/editor target. A permanent 128-square viewport fixture
exercises that fallback alongside later batched levels.

Atlas offsets, double-buffered complete-generation publication, update cadence,
probe resolution, samples and quality budgets remain unchanged. Nothing becomes
a user opt-in. No public plugin layout/interface changes; ABI remains 43.
Game rain, impacts, rings, fixtures and material settings are unchanged.

This follows the existing C8 retained-material -> relight -> filter -> publish
path described in PLACED_PROBES.md and the research SOURCE_RENDER_CALL_PATH.md
(CEnvProbeAtlas::Render / CEnvProbeRelighter). Batching is independent Phlosion
work inside that path, not a claim of exact source scheduling or full C8 parity.
No source dump or research program becomes a runtime dependency.

## Qualification

- Optimized RelWithDebInfo engine/editor, runtime/plugin and native tests build.
  Three CPU CTest groups pass: LocalReflectionContracts, ProbeSurfaceCache and
  EngineContracts.
- The old native binaries establish 28 reference runs. The candidate passes
  those plus two narrow-viewport cases. All 50 paired native images are
  pixel-exact, including face orientations, seam, rough/glossy, reset and
  partial/complete probe publication. Existing assertions stay unchanged.
- Twelve full-scene pairs cover runtime wet/dry/orbit/battle and editor wet/battle
  on both APIs. All 24 runs pass without API errors, invalid or dropped frames.
  Required surface/cluster/probe stages remain; no removed SSR source redraw
  returns. Rain snapshots match.
- Scene maximum RGB8 difference is 8, worst mean 0.008235.
  The inherited one-LSB shortcut is not passed by every scene. All twelve pairs
  were visually reviewed separately; native exactness is not misrepresented as
  full-game bit identity. Timing images have maximum 30, worst mean
  0.006785; their six pairs were also reviewed.
- Runtime/editor and native tests explicitly select the EVGA GPU1, LUID
  000000000000be8e, UUID GPU_1_UUID.
  Linux and shelved OpenGL are not newly qualified.

## Matched performance

Native 1920x1080, Medium, fixed night/overcast, heavy Hybrid rain and dry control.
RelWithDebInfo MSVC, optimized shaders, validation off, profiling on. Per API,
wet A/B/B/A and dry A/B; 220 recorded frames, analyze 40-179 (280 wet samples
per version and 140 dry samples). All twelve runs accepted, settings equal,
no API errors or thermal flags, maximum 63 C. No builds or processing
overlapped timing. Times in ms:

| API / scene | GPU mean | Whole CPU mean | Active CPU mean | Whole CPU p95 |
| --- | ---: | ---: | ---: | ---: |
| d3d12 / wet | 17.906 -> 17.300 | 19.312 -> 18.572 | 15.552 -> 14.979 | 30.305 -> 29.041 |
| d3d12 / dry | 12.864 -> 12.202 | 13.971 -> 13.119 | 10.937 -> 10.321 | 24.116 -> 22.960 |
| vulkan / wet | 18.423 -> 18.332 | 21.303 -> 18.160 | 18.063 -> 12.016 | 35.261 -> 19.536 |
| vulkan / dry | 13.731 -> 13.372 | 15.035 -> 14.123 | 11.561 -> 10.230 | 25.809 -> 23.486 |

| Wet probe-filter cost | D3D12 before -> after | Vulkan before -> after |
| --- | ---: | ---: |
| GPU mean, all measured frames | 0.739 -> 0.176 | 0.713 -> 0.226 |
| CPU mean, all measured frames | 0.405 -> 0.103 | 1.093 -> 0.068 |

Probe filtering occurs on publication frames; the all-frame means include zero
on other frames. baseline-bursts.json records the old association between probe
updates and slower CPU frames. That association does not mean filtering explains
every CPU spike. Whole CPU includes waits/presentation; active CPU excludes them.
The full mean/median/p95/p99, GPU memory and timing images remain in analysis.

Vulkan whole GPU time is nearly flat (18.423 -> 18.332 ms), despite the
filter's isolated 0.487 ms saving. D3D12 whole GPU improves by 0.606 ms.
Do not treat pass-time savings as additive predictions of whole-frame savings.

Wet CPU tails are mixed on D3D12: whole p99 **33.534 -> 36.428 ms** and active
p99 **25.362 -> 29.354 ms** worsen, despite improved mean and whole p95.
Vulkan whole p95 **35.261 -> 19.536 ms**, p99 **36.879 -> 20.585 ms**;
active p95 **30.116 -> 12.945 ms**, p99 **30.568 -> 15.095 ms**.
The larger CPU improvement is consistent with reduced submission/driver bursts,
but the direct filter CPU timer alone does not explain all of it. No claim is
made that every scheduling effect is causally isolated. D3D12 tails need further
work; neither backend has passed the complete 16.67 ms wet-frame target.

Cold shader preparation still takes minutes after shared shader changes; these
warmed measurements exclude startup. This pass does not solve that separate
problem or establish sustained 60 FPS / retail Cyberpunk FPS equivalence.

## Remaining work

The 16.67 ms wet target and CPU tails remain explicit gates. Repeated static
geometry preparation across shadow, primary and reflected views remains a
substantial CPU cost. Continue from the measured GPU phases rather than reducing
rain/reflection quality. This does not close C7/C8, Linux or revise a technique
percentage.

Worker evidence: WORKER_EVIDENCE_ROOT/phlosion-wet-reflection-performance-20261029.
Small receipts/reviews are mirrored in the engine's ignored
artifacts/lighting-foundation/wet-reflection-performance-20261029 directory.
Key files: baseline-build.json, build.json, tested-source.json, cpu.json,
native-baseline/checks.json, native-candidate/checks.json, native-comparison.json,
qualification.json, visual-review.json, timing-matched.json,
analysis-matched.json, timing-visual-review.json and final strict-build /
locked-payload-comparison / integration receipts.
