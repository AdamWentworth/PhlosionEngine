# Rough-reflection prefilter experiment — 2026-10-25

## Decision

Do not enable or merge the runtime prototype. Both raster-atlas candidates cost
more GPU time than the accepted renderer in the measured wet-night view. Runtime
source was restored; the accepted lighting, rain and launch settings are unchanged.
P1 remains open. This result applies to this implementation, not a general
estimate of the value of reflection prefiltering.

The next experiment should separate filtering from ordinary surface shaders and
reduce the number of filter submissions/copies. Measure that cost before adding
a cache lifetime policy: our probe follows the camera, and scene/lighting changes
must invalidate cached results correctly.

## Preserved inputs and prototype

Accepted engine: db3c232434c6d61887f9db71f8a6d9ffc2c0bb5f.
Accepted game: 1c717aac9d985cc7c979d22fa2f2f6abe1d722b6.
The pre-existing untracked game rendering roadmap was included in the baseline.
All 1,486 baseline files were verified after restoring the worker source.

The prototype uses Phlosion's existing 64-sample GGX kernel. It retains the
256-pixel source capture, visibility trace, parallax and distance-scaled footprint.
Seventeen logarithmic alpha levels cover 0.25–4; sharper lobes and pole/seam
collars retain the reference calculation, with a blend at the lower cutoff.
Each update adds 102 raster draws and snapshot copies. The first evaluated
candidate starts the filtered faces at 128 pixels; the lean revision starts
them at 64. Neither changes the original source-capture resolution.

The optional atlas grows from 1024 square to 2048 square at a 256-pixel capture.
That adds 48 MiB across two buffered RGBA16F atlases. D3D12 also increases its
uniform-packet budget for the extra filtering views. The trial was opt-in through
WorldLocalReflectionProbe::prefilterRoughness and the game's process-only
PHLOSION_LOCAL_REFLECTION_PREFILTER diagnostic.

The final uninstrumented prototype is retained as
[an unapplied engine patch](experiments/probe-prefilter-20261025.patch).
Its game toggle patch is in PokemonCyberpunk,
docs/experiments/probe-prefilter-20261025.patch. They require the accepted revisions
above and are experimental reproduction inputs, not approved production changes.

## Uninstrumented wet-night results

gpu-worker, Windows 11, ASUS RTX 2060 SUPER in the top slot, NVIDIA driver 610.88.
Selected UUID: GPU_0_UUID.
1080p, Hybrid heavy rain, fixed night, matched camera/state and original fixtures.
Each entry is the median of 60 valid GPU samples, frames 30–89. These are diagnostic
runs, not the longer performance qualification or a CPU/present FPS measurement.

| Vulkan build/path | First candidate ms | Lean revision ms |
| --- | ---: | ---: |
| Accepted renderer | 122.892 | 123.382 |
| Prototype, filtering off | 128.590 | 128.101 |
| Prototype, filtering on | 137.248 | 130.422 |

All three runs in each column recorded a 1935 MHz median busy graphics clock and
no thermal slowdown flags; peak temperatures were 70–74 C. Peak reported GPU
memory rose from approximately 1991–1992 MiB to 2040 MiB with filtering enabled.
No power, fan or driver settings were changed.

The candidate also incurs overhead when its filtering option is off. Separating
the extra filter kernel from ordinary material shaders is therefore a priority.
This experiment does not isolate shader instruction/register pressure as the
sole cause of that overhead.

## Phase diagnosis

A separate, temporarily instrumented Vulkan run measured the lean revision:

| GPU interval | Filter off ms | Filter on ms |
| --- | ---: | ---: |
| Local-probe filtering | 0.616 | 4.089 |
| Screen-source draw and snapshot | 36.283 | 34.114 |
| Final scene rendering | 72.528 | 68.933 |

These are independent per-phase medians, not additive frame-time totals.
The profiled total medians were 128.301/124.988 ms, which differ from the
uninstrumented pair. Use this breakdown to diagnose work distribution, not to
claim a repeatable improvement over the accepted renderer. Profile captures
stayed within one color step of the corresponding uninstrumented prototype.
The temporary timestamp instrumentation was removed.

## Visual and correctness gates

- Both candidates built as Windows runtime/editor targets; all five selected
  CPU contract tests passed. The restored accepted source was rebuilt and those
  five checks passed again.
- The larger candidate passed 56 valid native A/B cases across Vulkan and D3D12,
  including masking, blocking, reset, partial captures, six faces, seams, moving
  views/sources, grazing receivers and screen-reflection handoff.
- Two attempted motion16 cases violated the existing offscreen-emitter
  precondition before candidate comparison. Their failure logs remain; motion8
  replaced that invalid test position on both APIs and passed. No assertion
  or image tolerance was relaxed.
- The lean revision passed eight focused mirror/roughness comparisons across
  both APIs, including roughness 0.35, 0.50 and 0.65.
- Native Vulkan core/synchronization validation and D3D12 debug checks reported
  no API errors in these cases.
- The full Vulkan wet-night on/off toggle stayed within one color step, with
  matching recorded state. However, each prototype differed from the accepted
  renderer by two steps at one pixel, including with filtering disabled.
  The accepted renderer's repeated capture was identical. The original one-step
  acceptance gate was retained; full visual acceptance was not achieved.
- D3D12 full-game candidate runs were stopped during initial shader compilation
  after the Vulkan performance rejection. No D3D12 full-game candidate timing,
  editor, dry-day, orbit, battle or Linux qualification is claimed.
- A new Vulkan candidate's first wet-night run took about five minutes including
  shader compilation; subsequent runs were much shorter. The accepted build had
  a populated cache, so this is not a matched cold-start comparison.

## Evidence and handoff

Worker task: WORKER_EVIDENCE_ROOT/phlosion-probe-prefilter-20261025.
Controller review package:
PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/probe-prefilter-20261025/handoff.

Key receipts: controller-baseline.json, candidate-source.json, native-r4 and
native-r4-motion manifests, native-r5-preview manifest, runtime-comparison-r4.json,
runtime-comparison-r5.json, baseline-repeat.json, phase-comparison.json,
restored-source.json and recovery/build.json. Frozen payload manifests identify
the exact binaries/shaders; rejected and cancelled runs remain identifiable.
The accepted frozen deployment was not replaced. No commits or pushes were made.
