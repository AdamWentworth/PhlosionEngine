# Reusing opaque depth to reduce reflection overdraw - 2026-10-25

The renderer can reuse the opaque depth already captured for contact visibility
and screen reflections. Hidden fragments are rejected before expensive material
and reflection shading. PokemonCyberpunk opts in for matching field/battle
geometry; arrivals and active battle effects retain their original composition.
Other callers default to the previous behavior.

No rain density, light strength, reflection resolution, ray budget, filter quality
or authored asset was reduced. Existing material/lighting/rain shader payloads
match the baseline. D3D12 adds only an integer-texel depth restoration shader;
Vulkan copies the captured depth back with explicit barriers. Temporary GPU
profiling instrumentation was removed before the final builds.

## Contract and diagnostic

packWorldScreenReflection(..., reuseOpaqueDepth=true) promises identical opaque/
masked geometry, poses, clipping, viewport and camera across the contact, source
and final passes. A partial-size capture cannot opt in. Contact reuse additionally
requires completed buffered contact data, matching matrices/dimensions, current
frame identity and the corresponding snapshot. Source and final color remain
freshly drawn. Source omissions or intentional depth changes must leave reuse off.

PHLOSION_DISABLE_DEPTH_REUSE=1 disables the game opt-in for matched diagnosis.
Normal launches enable reuse where the contract holds. The parameter packet and
virtual interface sizes are unchanged.

## Observed worker measurements

ASUS RTX 2060 SUPER, PCI 01:00.0 / NVIDIA index 0, Windows driver 610.88.
1920x1080, the same frozen inputs, 200 warm GPU samples per view, one pair per
case/API. Values are median GPU milliseconds, not host frame rate.

| API | View | Before ms | After ms | Less GPU time |
| --- | --- | ---: | ---: | ---: |
| d3d12 | wet | 131.853 | 117.363 | 10.99% |
| vulkan | wet | 134.110 | 127.787 | 4.71% |
| d3d12 | dry | 78.445 | 52.671 | 32.86% |
| vulkan | dry | 111.324 | 104.959 | 5.72% |
| d3d12 | orbit | 157.389 | 141.151 | 10.32% |
| vulkan | orbit | 161.082 | 145.766 | 9.51% |
| d3d12 | battle | 115.972 | 110.786 | 4.47% |
| vulkan | battle | 127.454 | 120.746 | 5.26% |

These are warm-worker observations, not thermally controlled code-only speedup
guarantees. The longer sequence reached 83-85 C and reported thermal slowdown;
pairs.json retains clocks and slowdown counts for both runs. The 88 C stop
remained active. Repaste/retest before treating the percentages as stable hardware
benchmarks. Even the improved views remain well above the 16.67 ms / 60 FPS budget.

## Verification

- Engine editor, game runtime and plugin builds pass on gpu-worker.
- Five CPU renderer/lighting contracts pass, including default-off reuse and
  mismatched/incomplete contact-view rejection.
- All 42 native reflection cases pass with Vulkan core/synchronization validation
  and the D3D12 debug layer. All 72 additional native depth-reuse comparisons are
  pixel-identical (with and without a contact prepass).
- All eight full-game pairs pass the existing 1/255 image bound with exact
  recorded scene/weather/animation state and clock/resource checks.
- Editor wet-night previews on both APIs pass the same image/state checks with
  validation enabled. An isolated worker project supplies the editor content;
  the production project manifest was not changed. There are no validation
  errors. Both D3D12 editor runs retain the same 942 pre-existing warning-1328
  messages about buffer initial states (severity 2); these are not zero-warning
  runs. Native checks and the validated Vulkan editor repeat are clean.
  The first instrumented Vulkan editor attempt stopped advancing and was
  cancelled with reuse disabled; normal on/off runs and a bounded instrumented
  retry then completed and matched exactly. The initial stall's cause is not
  established; all attempts remain in the evidence.
- No local GPU tests, local builds or local inference. Controller integration
  uses the exact reviewed worker delta. No commits or pushes in this pass.

This does not newly qualify Linux or Autochess, or establish a higher visual
parity percentage. The prior historical one-pixel dry-Vulkan discrepancy remains
documented in GPU_SELECTION_PERFORMANCE.md; these comparisons use the exact
current task baseline, including its existing GPU-selection edits.

## Remaining work

Temporary Vulkan phase timing found about 3.36 ms for local-probe capture/filter,
37.86 ms for screen-source rendering, and 75.47 ms from the end of source capture
through final scene composition in the heavy view (131.68 ms total). The final
interval includes geometry, surfaces, haze and rain, not just reflection rays.
Do not sum older feature-off marginal costs as independent phase timings.

Continue reducing expensive per-pixel reflection work with the same visual gates.
The different Vulkan/D3D12 depth restoration paths are a specific next experiment:
measure a Vulkan depth-draw alternative before attributing its smaller gain to
copy behavior. No cause or benefit for that experiment is established yet.

Evidence: the game's ignored
artifacts/lighting-foundation/depth-reuse-performance-20261025 directory.
Worker originals and frozen payloads: WORKER_EVIDENCE_ROOT/phlosion-reflection-cost-20261025.
final-check.json binds the accepted source, images, shader identity and tests.
