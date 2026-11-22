# Haze reconstruction performance - 2026-10-28

Normal Windows D3D12/Vulkan rendering now separates cheap depth-aware haze
reconstruction from the full analytic fallback needed at thin foreground edges.
A stencil mask prevents the expensive fallback shader from being launched on
ordinary pixels. Primary and reflected views use the same path; there is no
new quality switch.

The quarter-width/height integration, sixteen equiangular samples per source,
light/shadow equations, depth tolerances and full-resolution edge integration
are unchanged. Surface-specific rain impacts, puddle rings, rain geometry,
wetness, exposure, lights and scene assets are unchanged. This is work avoidance,
not reduced resolution or fewer samples.

## Implementation and lifetime

Material 247 integrates the existing low-resolution radiance/distance texture;
248 reconstructs ordinary pixels and writes stencil zero only when its
depth-weight predicate succeeds. The mask is cleared to one first. Material 246
runs the same analytic fallback only where stencil still equals one. Both shader
specializations retain complementary depth predicates, with precise weight
arithmetic on the qualified backends. Scene alpha and actual scene depth are
preserved; mask depth/stencil is separate scratch storage.

D3D12 uses D24S8 and frame-fenced view slots. Changing an extent replaces that
slot's old resource only after its frame fence completes; viewport resizing
cannot accumulate one allocation per historical size. Capacity follows the
largest number of views submitted in a frame. Vulkan uses supported D24S8 or
D32S8, per-frame/view framebuffers, explicit pass dependencies, and editor-owner
cleanup before resize/destruction. Scene-target teardown clears the scratch.
This costs extra GPU memory and pass transitions, included in the measurements.

New profiling scopes separate depth, integration, mask, reconstruction and edge
work. Plugin ABI is 42; engine/editor and game plugins must be rebuilt together.
Legacy OpenGL retains the software predicate path; it remains shelved and was
not runtime-qualified.

Splitting the shaders alone did not improve the first D3D12 scene: the edge
shader still launched across the screen. Hardware stencil rejection completes
that work avoidance. Initial diagnostics, superseded payloads and rejected
overlapping runners are retained separately; none enter the final timing table.

## Qualification

All implementation, builds, processing and tests ran on gpu-worker Windows,
EVGA RTX 2060 SUPER index 1, LUID 000000000000be8e,
UUID GPU_1_UUID. MSVC RelWithDebInfo,
shader optimization level 3. No local inference or controller build/GPU execution.

- Three CPU CTest groups pass: PhlosionLocalHaze, PhlosionPlanarProjection
  and PhlosionEngineContracts.
- Sixty-four native GPU cases pass across both APIs, including independent
  integration/shadow/color oracles, thin edges, odd extents, repeated composition
  and changing mask sizes. The 62 shared fixture images are bit-identical to
  the software-split version.
- The additional Vulkan editor lifetime check passes resize, owner destruction,
  swapchain resize and repeated teardown with validation enabled.
- Twelve full scene pairs pass: runtime wet/dry/orbit/battle and editor wet/battle
  per API. Immutable baseline captures are reused for the final resource-pool
  revision; all candidate captures are fresh. Rain snapshots match, profiles
  have no invalid/dropped frames, clustered/probe/surface paths remain present,
  and no redundant SSR source draw returns.
- Before/after/difference-x16 sheets were inspected. No visible detail loss.
  Pixel-exact identity is not claimed. Exceptions above one display code:
  d3d12 runtime dry: max 4, mean 0.006246; d3d12 runtime orbit: max 2, mean 0.001735.
- Both optimized builds pass and the staged editor plugin explicitly reports
  ABI 42. Shader changes and runtime/editor binaries are listed in the payload
  receipt; scene assets/material source settings are unchanged.

## Matched timing

Native 1920x1080 Medium, fixed night/overcast, matched wet and dry review state.
Per API: wet ABBA and dry AB, 220 frames/run, analyze frames 40-179:
280 wet and 140 dry frames per variant. Validation off, profiling on,
cooldown to <=45 C between runs. No concurrent builds, tests or processing.
All twelve final runs have equal paired settings, no API errors or thermal
flags. Maximum temperature: 65 C.

| Wet mean cost (ms) | D3D12 before | D3D12 after | Vulkan before | Vulkan after |
| --- | ---: | ---: | ---: | ---: |
| Whole GPU frame | 19.266 | 18.180 | 21.043 | 19.328 |
| Primary haze | 1.790 | 1.067 | 2.613 | 1.215 |
| Reflected haze | 0.736 | 0.432 | 0.965 | 0.454 |
| Whole CPU frame | 20.626 | 19.503 | 22.444 | 21.411 |
| Active CPU, excluding begin wait/present | 15.999 | 15.365 | 17.198 | 16.933 |

Combined primary/reflected haze improves
2.526 -> 1.500 ms D3D12
(40.6%)
and 3.578 -> 1.669 ms Vulkan
(53.4%).
Whole wet GPU savings are 1.086 ms /
1.716 ms.

Dry GPU means: 13.465 -> 12.998 ms /
15.205 -> 14.030 ms.
Remaining matched wet-minus-dry GPU cost:
5.181 /
5.297 ms.
Haze also runs without rain, so its entire saving is not a reduction of the
rain-only premium.

Peak reported GPU memory: d3d12 wet: 1657 -> 1680 MiB; d3d12 dry: 1516 -> 1533 MiB; vulkan wet: 1963 -> 1983 MiB; vulkan dry: 1811 -> 1828 MiB.
The mask is not free. This is card telemetry, not an exact allocation inventory.
Timing images were separately reviewed; rain state matches. D3D12 wet2 has
six pixels above ten code values (max30, mean0.006776); other pairs max<=7.
These short runs
do not establish sustained FPS or another GPU's performance. CPU frame tails,
first-use shader/pipeline compilation and the 16.67 ms wet target remain open.

## Remaining work and evidence

No technique-coverage percentage changes. C7/C8, broader weather/volume
technique coverage and Linux remain open. Falling rain, wet reflection work,
remaining haze integration and CPU pose/capture preparation are still measured
costs; preserve surface-specific impacts and puddle rings when addressing them.

Worker evidence: WORKER_EVIDENCE_ROOT/phlosion-rain-haze-performance-20261028.
Final receipts: tested-pooled-source.json, build.json, cpu.log,
qualification.json, native-mask-comparison.json, editor-lifetime.json,
scene-gates-final.json, visual-review.json, payload-comparison.json,
timing-matched.json, analysis-matched.json and timing-visual-review.json.
Full captures/profiles stay on the worker. Small receipts/review images are
mirrored in the engine's ignored artifacts/lighting-foundation/rain-haze-performance-20261028.
