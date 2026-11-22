# Screen-reflection quality and measured performance

2026-10-26. PokemonCyberpunk now defaults to Medium screen reflections in runtime,
editor, exploration and battle. This is an authorized quality/performance tradeoff,
not an equal-quality optimization or a complete graphics preset. The 16.67 ms
frame target remains unmet.

## What changed

The engine accepts an explicit per-ray step budget and roughness cutoff. The game
owns names/defaults through CYBERPUNK_REFLECTION_QUALITY. Values are read once at
process startup; restart to change them. Invalid names fail clearly. No persisted
settings UI was added.

| Setting | Max steps | Roughness fade to existing local reflections |
| --- | ---: | --- |
| Low | 32 | 0.3-0.5 |
| Medium (default) | 64 | 0.4-0.6 |
| High | 128 | 0.5-0.7 |
| Ultra | 256 | 0.6-0.8 |
| Reference | 4096 | 0.45-0.75 |

Reference retains the previous pixel-sized exhaustive search and remains the
default for engine callers that do not supply settings. Bounded modes sample
the same ray range more coarsely, so thin/distant reflected sources can be missed
or change sparkle as the camera moves. Rough materials fall back earlier to
the existing local reflections. Their larger strides omit the conservative
depth-tile build. Output resolution, ray range and hit refinement remain intact.

The normal game still uses inline screen reflection shading. The prior shared
surface/reflection consumer remains experimental and disabled by default; its
bounded path also passes a native fixture. No temporal accumulation, stochastic
rays, motion history or elimination of the source-colour redraw was added here.

Rain density/style, light shapes and colors, geometry, simulation, shadows,
planar puddle reflections and native output resolution were not changed.

## Relation to the source study

Cyberpunk2077Research/lighting/quality/README.md and
lighting/reflections/SOURCE_RENDER_CALL_PATH.md trace quality-dependent small
reflection budgets, roughness transitions, local fallbacks and temporal feedback
in the supplied June-2021-era source snapshot. Its shipped revision is unverified.
This step adopts bounded work and material selection, with independent Phlosion
code. It does not reproduce that game's stochastic/temporal reconstruction.
Our 32/64/128/256 values are Phlosion choices, not imported Cyberpunk preset values.
No retail-game performance or visual-parity percentage follows from this study.

## Primary comparison

gpu-worker Windows, one ASUS RTX 2060 SUPER:
GPU_0_UUID, LUID 000000000000ad83.
Native 1920x1080, fixed wet night/heavy Hybrid rain. Optimized RelWithDebInfo
(/O2, assertions enabled), optimized shaders, validation off. No local-controller
builds, tests or rendering. No local inference.

For each API the order was frozen prior Reference / new Medium / new Medium /
prior Reference. Total-only GPU timestamps; frames 61..240, 180 valid samples/run.
The table averages the two per-run medians, not all samples pooled. No builds or
heavy image processing overlapped these eight timed runs. Starts cooled to <=55 C.
They peaked at 74-75 C with median active clocks 1920-1935 MHz and no thermal
slowdown flags. These are GPU timings, not a direct FPS measurement.

| API | Before GPU ms | Medium GPU ms | Reduction |
| --- | ---: | ---: | ---: |
| D3D12 | 52.906 | 40.405 | 23.6% |
| VULKAN | 67.319 | 45.245 | 32.8% |

D3D12 mean per-run GPU p95 fell from 56.140 to 41.357 ms; Vulkan from 71.854 to
47.477 ms. Renderer CPU-frame medians also decreased: 53.043 -> 40.571 ms and
68.863 -> 46.666 ms respectively.

Broader controls below use one baseline/candidate pair each, not repeated ABBA.
Dry/orbit use frames 61..240; battle uses 161..340 after the encounter begins.
All use 180 samples, the same GPU/build/output configuration and validation off.

| API | View | Before GPU ms | Medium GPU ms | Reduction |
| --- | --- | ---: | ---: | ---: |
| D3D12 | dry | 24.576 | 32.341 | -31.6% |
| D3D12 | orbit | 62.039 | 40.009 | 35.5% |
| D3D12 | battle | 60.022 | 42.073 | 29.9% |
| VULKAN | dry | 55.414 | 36.506 | 34.1% |
| VULKAN | orbit | 81.500 | 48.498 | 40.5% |
| VULKAN | battle | 66.577 | 45.029 | 32.4% |

Full scene-state JSON matched in wet, dry, orbit and battle comparisons after
removing exactly the two new reflection-quality metadata fields. The rain and
light state did not change to obtain these numbers.


### D3D12 daylight: mixed frame-time distribution

The initial short daylight median regressed even though its mean and p95 improved.
Both builds exhibit two timing bands, with a different proportion of fast/slow
frames. Four extended 1200-frame runs were attempted to stabilize the mixture,
but reached 82-83 C and reported thermal slowdown. Those runs are retained in
dry-followup.json/dry-analysis.json and excluded from accepted performance claims.
The diagnostic analyser originally rejected them; its original script is retained.
The preliminary approximately 7% long-run average improvement is not accepted.

A fresh, shorter ABBA repeat avoids that thermal limit: 240 frames/run, samples
61..240, starts <=55 C, no reported thermal slowdown. Table values average each
pair of per-run statistics, using all accepted samples.

| Statistic (ms) | Prior Reference | Medium |
| --- | ---: | ---: |
| GPU mean | 31.031 | 26.550 |
| GPU median | 24.619 | 32.191 |
| GPU p95 | 46.807 | 32.750 |
| CPU renderer-frame mean | 31.510 | 27.616 |
| CPU renderer-frame median | 25.601 | 28.793 |

This is a mixed daylight result, not a blanket speedup across every statistic.
The wet-night improvement must not be applied as a universal percentage.
The two 300-frame full-phase diagnostic runs stayed at 74-75 C without thermal
slowdown; they also show both timing bands. This pass does not establish the root
cause of their differing frequency or claim to solve D3D12 frame pacing.

## Qualification and visual limits

- Engine, game runtime/plugin and editor builds passed on the Windows worker.
  Seven engine and four game CPU tests passed.
- 42 native GPU cases passed across D3D12/Vulkan, including Reference thin sources,
  Medium mirror/rough/matte/masking/occlusion/reset/offscreen/camera/PBR coverage,
  32/128/256 budget smoke cases and the experimental shared-surface consumer.
  The low/high/ultra native smoke cases hold the cutoff at 0.6 to isolate budget;
  they are not a full-scene qualification of each tier.
- 18 runtime/editor scene checks passed: wet/dry, runtime orbit at three positions,
  and actual encounters/battles on both APIs. The effective Medium budget was 64
  in every handshake. Vulkan validation was clean. D3D12 retained only the known
  severity-2 ID-1328 warnings (938-974 depending on view); no new messages.
- Full-scene before/after views and sampled moving-camera/battle frames were
  inspected. Main pink/blue strips, broad puddle reflections and rain remain.
  This is sampled image review, not a comprehensive temporal-animation assessment.
- The wet Medium comparison has mean absolute RGB differences <=0.038/255 and
  maximum 78/255 on each API, with 383 pixels differing by >16/255. A small mean
  does not negate visible lost glints. No exact-image gate was relaxed.
- Reference fixed-view images differ by at most 1/255 (90 D3D12, 510 Vulkan pixels),
  consistent with previously recorded long-run low-level variation; not bit equal.
- Preliminary High/Reference timings are retained separately. One new-Reference
  Vulkan spot was 70.84 ms versus 66.99 ms for the frozen prior build. These were
  exploratory runs, not repeated acceptance timings. Do not claim unchanged
  Reference-mode performance or an optimization for every engine client.
- Cold first-use shader compilation took roughly nine minutes on D3D12 and six
  on Vulkan in this task. That cost is excluded from steady-state GPU timings;
  startup has not been improved. Linux Vulkan was not requalified in this pass.

## Evidence and remaining work

Private evidence: Cyberpunk2077Research/data/evidence/
lighting-quality-performance-20261026. Worker task:
WORKER_EVIDENCE_ROOT/phlosion-quality-performance-20261026. Baseline source revisions:
engine d47f55685b4d9f4aa96c5d90d44b996b2ef2ad7e,
game 013f8b355a296a1e324d9e1733fb10331b7b3551, plus the exact recorded dirty trees.
All 1504 tracked/unignored source files matched the controller baseline before
implementation. Integration transfers only this task's delta and verifies
intervening edits; prior uncommitted work remains intact.

Frozen prior runtime SHA-256:
7d0257066b0c5e79a580632b2b0403a656c00efdfdce0383b48babb271c93c9b9.
Candidate:
9e1190d6054e9ceb4800257ca040be3f1aab2f6eb83b32558a9b544c824edde9.
Payload manifests, build/test logs, per-frame profiles, thermal CSVs, images,
scene states and source delta hashes accompany the summary.

This delivers materially faster wet-night, orbit and battle views; D3D12 daylight
is mixed as documented above. It does not complete P2 or meet
60 FPS. The remaining architecture work is motion/history and temporal
reconstruction, then reuse of prior frame colour to remove the source redraw.
Independently budgeted shadows/fog/AO and full game preset persistence/UI remain
separate work. Use measured phase costs to prioritize the next change.
