# Placed-probe performance qualification - 2026-10-28 task

The accepted placed-probe path reduces warm median wet-night GPU time by 4.4%
on D3D12 and 4.5% on Vulkan in this fixture. Mean reductions are 3.8% and 2.3%.
The 16.67 ms target remains unmet. Vulkan wet p95 is slightly worse
(31.256 -> 31.344 ms); this is not an across-the-board frame-pacing improvement.
This task adds measurements and roadmap scope, not another rendering change.

## Compared source and workload

- Baseline: engine 7b12c3dc1b599aaba7768a1dfbdd8a3fb7d9a8a4 and game
  7d7d3654f770116b07c7fa9f02813d0c75409731, rebuilt in isolated worker worktrees.
- Candidate: engine 1c793ade134ddea767e1d3cdf2b5ad4c6583eb87 and game
  beffc83a090f00fae3828c4880d128e02e1fcfa0. Payload hashes match the preceding
  placed-probe qualification; no renderer or shader edits were made here.
- gpu-worker Windows 10.0.26300.0, EVGA RTX 2060 SUPER, LUID 000000000000be8e,
  UUID GPU_1_UUID. Builds, rendering and analysis
  ran on this worker. controller only orchestrated, reviewed and integrated.
- Optimized RelWithDebInfo, native 1920x1080, Medium, the same Outskirt front
  camera and assets. Wet night uses Hybrid heavy rain; the second case is dry noon.
- Wet order per API: baseline, candidate, candidate, baseline. Dry has one pair
  per API and is supporting evidence, not a repeated dry acceptance campaign.
- Each timing run records 220 frames; frames 40..179 supply 140 samples.
  The table pools the two wet repeats per version (280 frames) and uses 140 dry
  frames. Screenshot/readback at frame 185 occurs after the measured interval.
- Validation layers and motion/temporal diagnostic readbacks are disabled during
  timing. GPU phase profiling remains enabled; these are instrumented warm
  timings, not zero-overhead shipping FPS or cold-start measurements.
- Each run verifies payload hashes, GPU and render inputs. No missing, invalid
  or dropped profile frames; no reported renderer errors. Selected-card peak
  temperatures were 62..66 C, with no reported thermal slowdown during the runs.

The baseline uses one camera-following 256-square probe; the accepted candidate
uses two placed 128-square probes, spatial blending and a three-face Medium
update budget. This comparison includes those algorithm and quality-budget
differences. It does not isolate caching at identical resolution, and it does
not prove old/new pixel equality. No additional quality reduction was made in
this timing task. See [the implementation report](PLACED_PROBES.md).

## Warm results

All times are milliseconds; arrows mean baseline -> placed probes.

| API | Case | GPU median | GPU mean | GPU p95 | CPU frame median |
| --- | --- | --- | --- | --- | --- |
| D3D12 | Wet night | 28.442 -> 27.196 | 28.367 -> 27.283 | 29.189 -> 28.996 | 28.391 -> 27.355 |
| D3D12 | Dry noon | 21.000 -> 18.906 | 20.771 -> 18.115 | 21.328 -> 20.939 | 20.248 -> 18.279 |
| VULKAN | Wet night | 30.576 -> 29.196 | 30.223 -> 29.513 | 31.256 -> 31.344 | 30.299 -> 28.985 |
| VULKAN | Dry noon | 23.065 -> 20.895 | 22.991 -> 21.520 | 23.373 -> 23.490 | 22.731 -> 20.134 |

GPU time alone is not delivered FPS. CPU frame pacing, cold startup, other
views, quality tiers and Linux still need their respective acceptance checks.
There is no same-worker retail Cyberpunk benchmark in this result.

## Where the time goes

Mean wet-frame GPU scopes, pooled across the two repetitions:

| Non-overlapping top-level scope | D3D12 old -> new | Vulkan old -> new |
| --- | --- | --- |
| Old probe capture/filter -> new relight plus filter | 3.592 -> 1.131 | 4.063 -> 1.405 |
| Final geometry/material shading | 7.207 -> 7.570 | 6.333 -> 7.115 |
| Shared surfaces, clustered lighting and contact resolve | 5.119 -> 5.123 | 4.952 -> 5.010 |
| Separate planar reflection | 3.641 -> 3.963 | 3.559 -> 3.731 |

The probe saving is larger than the whole-frame improvement. Consumers now
perform placed-probe selection/blending, while the old path did not. These
measurements locate the added cost; they do not isolate a causal instruction cost.

The final geometry scope contains 7.043 ms of static-geometry work on D3D12 and
6.580 ms on Vulkan; foliage is approximately 0.44 ms. This is rendering/shading
work, not a claim that mesh submission alone costs seven milliseconds.
Likewise, contact-depth is a combined scope: its static shared-surface draw is
1.330/1.318 ms and clustered lighting 1.259/1.249 ms (D3D12/Vulkan).
Do not call its full five milliseconds AO. The remaining contact/temporal setup
work needs finer attribution before choosing an optimization.

Medium relights three faces per frame and filters/publishes every second frame.
Wet candidate medians grouped by publication are 26.690 vs 28.503 ms on D3D12,
and 28.408 vs 30.842 ms on Vulkan (without vs with publication). These correlated
groups explain why the tail improves less than the median; they are not an
isolated filter toggle experiment. The filter itself averages 0.865/1.151 ms
across all frames, about twice that on publication frames.

Next performance priorities:
1. Complete material-evaluation reuse in ordinary opaque composition, preserving
   specialized foliage/character/forward participation and the qualified images.
2. Budget filter/publication work as well as face relighting, while retaining
   complete-generation publication and the same reflection data.
3. Split the residual contact-depth work into useful measured scopes before
   tuning it. Probe packing alone cannot account for the dominant shading cost.

C8 residency/priority/shadow scheduling and Linux qualification remain open.
These measurements do not rescore technique coverage or establish source-engine
parity.

## Correctness and evidence

Four validation-enabled controls compare each version with its own previously
accepted image. Baseline D3D12/Vulkan and candidate D3D12 are pixel-exact;
candidate Vulkan differs by at most one byte per RGB channel, within the
unchanged tolerance. All four pass. This guards reproduction of accepted
checkpoints, not equivalence between different probe layouts.

Candidate measured frames contain exactly three probe-relight spans and no
static-material recapture. Neither version draws the removed screen-reflection
source pass. Hybrid surface splashes and puddle rings remain unchanged in source.

Worker record: WORKER_EVIDENCE_ROOT/phlosion-probe-performance-20261028.
Controller evidence: artifacts/lighting-foundation/placed-probe-performance-20261028.
Key files: timing.json, analysis.json, publish-cost.json, baseline-build.json,
candidate-source.json, baseline-rain-inputs.json, per-run manifests, phases,
GPU telemetry, logs, validation images and payload hashes. timing-r2.py records
order, windows, gates and metrics.

The first isolated baseline run lacked its ignored v4 rain inputs. Its failure
is retained in timing-missing-input.json and validate-d3d12-baseline. The 24
required files were copied from the worker game checkout and verified identical
before all reported runs. No source change was needed. An initial analysis
script used a nonexistent span ID; final-analysis-r2.py correctly uses the
profile array-index parent links. Bookkeeping failures are retained and are
not counted as successful timing runs.
