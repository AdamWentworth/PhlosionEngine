# Compact temporal histories - 2026-10-27

The standard temporal renderer now stores color/coverage history and velocity/
invalidation masks as RGBA16F, with R11G11B10F reflection feedback. This is a
C1/C4 resource step, not completion of C1-C8 or a new retail parity percentage.
Low/Medium/High/Ultra continue to select their existing budgets; no feature
switch or reduction in rain, reflection resolution, ray count or view distance
was introduced.

## Source and precision boundary

Research lighting/reflections/SOURCE_RENDER_CALL_PATH.md records the
renderNode_Prepare.cpp feedback resource: half width/height, R11G11B10F, prepared
from composed post-TAA color. This change brings the feedback storage into that
traced format. Phlosion still uses separate mip images/dispatches and a full chain.

RGBA16F color/masks are independently qualified Phlosion adapters, not a claim
that every source history has this layout. Color alpha still stores reactive
coverage, including disappearing transparency. Signed mask velocity is preserved.
Closest-depth metadata stays RGBA32F and object identity stays exact R32_UINT;
the shader rejection equations, tolerances and lifecycle are unchanged.
Complete compact shared-surface packing and material/producer coverage remain.

Both backends allocate explicit formats and check storage support. Vulkan builds
three explicit storage-image shader variants (metadata, half-float, feedback);
D3D12 uses typed UAV conversion with the existing shader. Diagnostic readback
decodes the actual compact storage back to the existing float API.

## Memory

Actual image descriptions are counted by native readback and asserted at
320x240 and 1920x1080 on both APIs. The following is texel payload, excluding
allocator alignment, SSR outputs, shared surfaces, readbacks and other views.

| Per-view image group at 1920x1080 | Format | Bytes |
| --- | --- | ---: |
| Paired full-size reconstructed color/coverage | RGBA16F | 33,177,600 |
| Paired half-size closest-depth/motion metadata | RGBA32F | 16,588,800 |
| Paired half-size exact identity | R32_UINT | 4,147,200 |
| Half-size raw/dilated velocity/invalidation | RGBA16F | 8,294,400 |
| Paired feedback mip chains, starting half-size | R11G11B10F | 5,528,440 |
| Unused-stage identity binding | R32_UINT | 4 |

Total: **67,736,444 bytes (64.60 MiB)**, down from **125,793,764 bytes
(119.97 MiB)**: **46.15% less**, or **55.37 MiB saved per full-size view**.
This is not a 46% reduction in the game's entire VRAM allocation.

## Correctness

All execution used gpu-worker Windows, optimized RelWithDebInfo, EVGA RTX 2060 SUPER
GPU_1_UUID / LUID 000000000000be8e.

- Five engine and six game CPU contracts pass.
- Both native temporal and native stochastic-reflection fixtures pass on
  D3D12/Vulkan: accumulation, camera cuts, IDs, depth disocclusion, exposure,
  reactive coverage, motion, odd/full-HD resize, retirement, display and SSR tiers.
- Compact HDR feedback is tested with additive radiance 32 and unequal RGB.
  The independent nonlinear-filter oracle remains 8/27, distinct from .32.
  Its float32 absolute bound is replaced by one compact storage step:
  1/256 for R/G and 1/128 for B at that value. This accounts for typed-UAV
  truncation without changing or disabling the filter test.
- Eighteen runtime/editor cases pass: wet/dry, orbit and battle at Medium on
  both APIs, plus wet Low/High/Ultra. Twelve Medium images compare against the
  frozen previously qualified default: worst image mean 0.05895/255,
  worst pixel/channel 4/255, and worst 99th percentile 1/255.
  The acceptance bounds were recorded before scene captures: mean <.5/255,
  99th percentile <=3/255 and fewer than .1% of channels differing by >8/255.
- Four first-battle/return-to-field cases reject all incompatible old history.
- No D3D12/Vulkan validation errors; recorded warning IDs: 1328.
  No obsolete temporal opt-in variables are set by these qualification runs.

Initial attempts are retained separately: the old float32 filter tolerance
failed on the expected packed rounding; an HDR fixture that tried putting 32
into UNORM vertex color was corrected to generate real additive HDR. Neither
required a shader equation change or relaxation of motion/rejection tests.
An inherited editor harness path initially launched the prior executable;
those editor rows were excluded and all four cases rerun with the candidate.
The accepted manifests verify the exact runtime/editor executable hash.

## Matched performance

Native 1920x1080 Medium, same scene/replay, GPU selection, optimized build,
no validation or diagnostic motion/temporal readback. Each run renders 220 frames;
frames 40-179 supply 140 complete GPU samples, excluding startup and the final
screenshot. Wet uses baseline/candidate/candidate/baseline order (280 samples
per version/API); dry uses one matched pair (140 samples). Selected-GPU thermal
slowdown invalidates a run. Every accepted run reports none.
The initial D3D12 and Vulkan dry baselines reported software thermal slowdown and are excluded;
their raw evidence is retained. Remaining runs start at <=45 C (the initial wet
ABBA block used <=55 C), with the same cooldown policy for each matched pair.
No fan, clock, power, driver or cooling settings were changed.

Baseline: frozen temporal-defaults-20261027/candidate-r1, engine 772d2839.
Candidate: this task's frozen candidate-r1 with source/product hash manifests.

| API / scene | Before median ms | After median ms | Improvement | Before / after p95 ms |
| --- | ---: | ---: | ---: | ---: |
| d3d12 wet | 33.166 | 32.401 | 2.3% | 33.782 / 32.943 |
| d3d12 dry | 25.447 | 24.601 | 3.3% | 25.941 / 25.075 |
| vulkan wet | 36.246 | 34.262 | 5.5% | 37.231 / 35.156 |
| vulkan dry | 28.444 | 26.431 | 7.1% | 28.908 / 26.903 |

These are bounded warm worker measurements, not retail Cyberpunk comparisons
or broad hardware guarantees. The native 1080p Medium 16.67 ms target remains
open. Raw samples, individual-run statistics and phase timings are retained.

## Remaining work and evidence

Continue C1's shared-surface packing and material-class/producer coverage, then
C7 clustered light/material dispatch and C8 probe/shadow scheduling. Preserve
the Hybrid sand/hard-surface impacts and actual puddle rings. Linux Vulkan and
cold-start qualification remain open. No overall parity score is increased
merely because buffers became smaller.

Evidence: controller PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/compact-history-20261027 and worker
WORKER_EVIDENCE_ROOT/phlosion-compact-history-20261027. Baseline/tested source hashes,
native allocation checks, frozen-product hashes, GPU manifests, comparison
metrics, profiles and telemetry accompany the returned task delta.
