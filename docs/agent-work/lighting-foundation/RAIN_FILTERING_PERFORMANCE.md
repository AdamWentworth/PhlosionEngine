# Falling-rain hardware filtering performance

Date: 2026-10-29. Normal Windows D3D12/Vulkan paths; no new quality control.
This replaces manual trilinear mask emulation with the GPU's trilinear sampler,
keeping the same explicit LOD, textures, geometry, animation, depth fading and
blending. Surface-specific impacts and puddle rings are unchanged.

## Why this work

Separate rain-depth, near, far, source-splash, solid-impact and water-impact
profile scopes identify distant streaks as roughly 90% of the measured rain pass.
The diagnostic far-pass means were 1.853 ms D3D12 / 2.134 ms Vulkan. These
diagnostics are not the final matched baseline.

The previous shader used up to eight explicit texel loads per mask and three
masks per far fragment. The captured v4 sampler is MIN_MAG_MIP_LINEAR with
anisotropy 1. The new base-texture sampler contract selects explicit trilinear
clamp/repeat without generic renderer anisotropy. It preserves the recovered
gradient-derived LOD and snapshot bias rather than silently accepting host bias.
The native hardware now performs the interpolation.

The engine adds a generic base-texture sampling field (default unchanged),
D3D12 static samplers and Vulkan sampler/cache identity. Editor plugin ABI is 43.
The shelved OpenGL implementation stays structurally consistent; game OpenGL
enrollment is unchanged. Only near/far retail masks select the new modes; splash
textures retain the default. Tests preserve the existing numeric tolerance.

## Evidence and reference boundary

Baseline engine d2b855198e80bab0f88c6383650f130b80daa2ed; game
b4b89572573c5e1071a278dc610cd9e0d89a3b62. Worker source hashes are frozen in
tested-source.json. All implementation, builds, processing and tests ran on
gpu-worker; the controller reviews, integrates and commits.

The existing v4 capture package and research preparation recipe establish the
sampler. trace.json records the package and inspected source hashes. The supplied
source snapshot is not authenticated as current retail. Its ScreenSpaceRain node
modifies wet G-buffer surfaces; it is not the captured transparent streak draw.
We did not infer the active retail particle path from a legacy class name.

Explicit LOD alone does not establish non-anisotropic sampling. See
[Vulkan texture filtering](https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/chapters/textures.adoc)
and [D3D SampleLevel](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-to-samplelevel).
The dynamic producer of the captured LOD bias remains unknown; this change
preserves the baseline snapshot, not a newly recovered dynamic producer.

## Correctness

- RelWithDebInfo runtime, editor, game plugin and rain oracle build successfully.
  PhlosionVulkanTextureKey and PhlosionEngineContracts CTest groups pass.
- The native oracle checks 301,740 captured GPU vertices plus shelter, wind,
  depth fade, pause/dry behavior and splash lifetime. GL/SM5 mask checks cover
  1,152 samples each; maximum error 0.00108415 stays below the unchanged 0.002
  tolerance. Plugin exports ABI 43.
- Twelve scene pairs cover both APIs, runtime wet/dry/orbit/battle and editor
  wet/battle. All 24 runs exit successfully with no API errors or invalid frames.
  Settings and rain states match; required shared-surface/clustered/probe phases
  remain present.
- Scene RGB8 mean differences are at most 0.010065, maximum 4. The inherited
  one-LSB shortcut does not pass every D3D12 image; this is recorded explicitly.
  All twelve pairs were visually reviewed and accepted with the independent
  numeric oracle, without relaxing its tolerance.
- Six matched timing image pairs were also reviewed, including amplified crops
  of the worst pixels. Mean RGB8 difference is at most 0.011036, maximum 9;
  only two pixels exceed 4 in that D3D12 pair. No perceptible scene change.
  These images are not claimed to be bit-identical.

Runtime/editor and performance checks explicitly select EVGA GPU1, LUID
000000000000be8e. The legacy GL/SM5 oracle uses its default vendor adapter;
it is a standalone test, not an enabled OpenGL game backend. Linux is unqualified.

## Matched performance

EVGA RTX 2060 SUPER on gpu-worker, native 1920x1080, Medium, fixed night/overcast,
heavy rain versus dry. RelWithDebInfo MSVC build, optimized shaders, validation
off. Per API: wet A/B/B/A and dry A/B; 220 frames, analyze frames 40-179.
All twelve runs accepted, no exclusions, API errors or thermal flags; maximum
temperature 64 C. No builds or processing overlapped these runs. Means in ms:

| API / case | GPU before | GPU after | Rain GPU before | Rain GPU after | Whole CPU before | Whole CPU after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| D3D12 wet | 18.5163 | 17.9767 | 2.0992 | 1.6844 | 20.2025 | 19.3804 |
| Vulkan wet | 19.3127 | 18.6111 | 2.4004 | 1.6798 | 21.1333 | 20.4083 |
| D3D12 dry | 12.6406 | 12.5277 | 0 | 0 | 13.7430 | 13.4077 |
| Vulkan dry | 13.9992 | 13.9976 | 0 | 0 | 13.8240 | 13.8464 |

Rain GPU saves 0.4148 ms (19.8%) D3D12 and 0.7206 ms (30.0%) Vulkan.
Whole wet GPU saves 0.5396 / 0.7016 ms. Wet-minus-dry GPU cost changes
5.8757 -> 5.4490 ms / 5.3135 -> 4.6135 ms.
Wet active CPU means improve 16.3704 -> 15.6589 / 16.6033 -> 15.8914 ms;
whole CPU frames still exceed the target. Raw percentiles remain in analysis.

The 16.67 ms target remains open: wet GPU alone needs about 1.31 / 1.94 ms
more headroom. These are matched scene measurements, not a retail Cyberpunk FPS
comparison or a stable-60-FPS claim. Dry changes are control-run variation, not
an asserted new dry rendering optimization.

The first cold candidate shader preparation was expensive (roughly 11 minutes
D3D12, 6 minutes Vulkan); subsequent warmed checks were fast. Cold preparation
is excluded from frame-time samples. There was no matched cold baseline, so this
is neither a measured regression nor a solved startup problem.

## Remaining work

Follow the measured wet/reflection and rain costs, including redundant shader
work and scheduling, while preserving the accepted appearance. Cold shader
preparation and CPU tails need separate work. Captured bias production, spatial
rain-lighting and shelter inputs remain adapters. No technique-coverage
percentage changes; this does not close C7/C8, Linux or the overall frame budget.

Evidence on gpu-worker: WORKER_EVIDENCE_ROOT/phlosion-falling-rain-performance-20261028.
Small review receipts are mirrored in the ignored controller directory
artifacts/lighting-foundation/falling-rain-performance-20261028. Key receipts:
trace.json, tested-source.json, rain-oracle.log, cpu.json, qualification.json,
visual-review.json, timing-matched.json, analysis-matched.json,
timing-visual-review.json and the final strict-build/locked-payload comparison.
The earlier haze baseline uses its immutable locked payload; harness-notes.json
records an early staging-path mistake that did not modify that baseline.
