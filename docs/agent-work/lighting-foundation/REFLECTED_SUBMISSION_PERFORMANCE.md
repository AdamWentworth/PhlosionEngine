# Composed-view CPU preparation - 2026-10-28

Following [reflected surface lighting](REFLECTED_LIGHTING_PERFORMANCE.md), the
normal D3D12/Vulkan paths now reject already-composed glTF materials before
preparing transforms, palettes, texture mip bindings and backend draw state.
This applies to primary and reflected views; specialized, unlit and transparent
materials still draw. There is no quality switch or new rendering approximation.

The backend exposes its active composition lighting only while standard geometry
is suppressed. glTF uses the existing material/view/light eligibility predicate
once per material, after full transactional input validation. Prewarming and
motion capture retain their normal path. Separate composed-draw/triangle counters
avoid reporting skipped work as submitted geometry. The new virtual interface
requires editor plugin ABI 41; engine and game plugins must be rebuilt together.

Both backends also reuse their motion-packet vector storage, including empty
resets. Input validation precedes mutation. The packet format, GPU resources and
shader binaries are unchanged. Capacity stays at the largest encountered packet
for the renderer lifetime.

## Qualification

All edits, builds, processing and tests ran on gpu-worker Windows, EVGA RTX 2060
SUPER index 1, LUID 000000000000be8e, UUID
GPU_1_UUID. MSVC RelWithDebInfo, shader optimization
level 3. No controller build or GPU execution.

- Three CPU CTest groups passed: PhlosionRendererPolicy,
  PhlosionWorldMotionHistory and PhlosionEngineContracts. New controls cover
  standard/masked suppression; unlit, transparent, specialized and mismatched
  view/light preservation; prewarming; invalid-pose rejection; exact packet
  layout, retained storage, empty resets and invalid-input preservation.
- 34 native GPU checks passed across both APIs: primary/planar composition
  controls and secondary-before-primary temporal resource lifetime/resize.
- Twelve scene pairs passed: runtime wet/dry/orbit/battle and editor wet/battle
  per API. Rain snapshots match; capture, clustered lighting, composition and
  depth reuse remain present with no redundant SSR source draw.
- All battle images are exact. Nine scene pairs differ by at most one display
  code value. D3D12 dry/orbit/editor-wet exceptions have max 5/2/7 and mean
  0.006511/0.002295/0.001113. Before/after/difference images were reviewed with
  no visible detail loss. Pixel identity for every scene is not claimed.
- All SPIR-V, materials and assets match the accepted baseline. Only the
  engine/editor/runtime/plugin binaries changed.

## Matched timing

Prior accepted locked payload versus qualified candidate, native 1920x1080,
Medium, fixed night/overcast and matching review/weather state. Per API: wet
ABBA and dry AB; 220 frames/run, analyze frames 40-179. There are 280 wet
frames/variant and 140 dry frames/variant. Validation off, profiling on,
GPU cooled to <=45 C between runs; no parallel builds or processing.

All twelve runs accepted with equal paired settings, zero API errors and zero
thermal flags; maximum observed temperature 65 C. These short runs do not
establish sustained FPS or performance on another GPU.

| Wet mean cost (ms) | D3D12 before | D3D12 after | Vulkan before | Vulkan after |
| --- | ---: | ---: | ---: | ---: |
| Active CPU frame, excluding begin wait and present | 17.558 | 15.313 | 18.214 | 16.829 |
| Whole CPU frame | 21.372 | 19.976 | 22.499 | 22.349 |
| Reflection CPU, inclusive | 4.846 | 3.738 | 4.648 | 3.857 |
| Reflection final static preparation | 0.876 | 0.094 | 0.579 | 0.091 |
| Primary final static preparation | 0.844 | 0.086 | 0.560 | 0.085 |
| Reflected surface static capture CPU | 2.593 | 2.353 | 2.629 | 2.442 |
| Whole GPU frame | 19.321 | 19.120 | 20.940 | 20.983 |
| Reflection GPU, inclusive | 2.091 | 2.075 | 2.258 | 2.264 |

Active CPU savings are 2.245 ms (12.8%) / 1.386 ms (7.6%).
Reflection CPU savings are 1.108 ms (22.9%) / 0.791 ms (17.0%).
The final static preparation drops about 89-90% D3D12 / 84-85% Vulkan.
Capture, pose evaluation and validation still run; this does not remove all
geometry preparation.

Whole GPU cost is effectively unchanged. Begin waits increase 3.637 -> 4.479 ms
and 4.232 -> 5.468 ms as the CPU reaches GPU work sooner. Whole CPU p95 is
32.049 -> 31.151 ms D3D12 and 35.956 -> 36.202 ms Vulkan; Vulkan p99 also
does not improve (38.071 -> 42.461 ms). Do not equate the active CPU saving to
the same delivered-frame improvement, or claim stable 60 FPS.

Dry controls: active CPU 12.515 -> 11.181 ms / 9.522 -> 8.888 ms; whole CPU
15.221 -> 14.466 ms / 14.996 -> 15.028 ms; GPU 13.502 -> 13.468 ms /
15.176 -> 15.211 ms. The remaining wet-minus-dry GPU premium is about 5.65 /
5.77 ms. Peak process GPU memory is unchanged to the telemetry precision:
wet 1657 MiB D3D12 and 1963 -> 1962 MiB Vulkan; dry 1516 / 1811 MiB.

Timing-run images also passed visual review. Their maximum differences are
5/30/2 (D3D12 wet1/wet2/dry) and 3/9/1 (Vulkan); mean differences stay below
0.0075. Only six D3D12 wet2 pixels exceed ten code values. These short temporal
captures are not bit-exact; rain state and scene detail remain preserved.

## Remaining work and evidence

C7/C8 and the 16.67 ms target remain open; no technique-coverage percentage
changes. Per-asset pose evaluation, preflight validation, material classification,
capture identity/history maps and specialized backend submission still cost CPU.
The next GPU target is measured rain/haze work, including reflected haze,
without dropping the required surface-specific impacts or puddle rings.
CPU tails and sustained measurements remain separate concerns. Linux is unqualified.

Worker evidence: WORKER_EVIDENCE_ROOT/phlosion-reflected-submission-20261028.
Key receipts: tested-source.json, build.json, cpu.json/cpu.log,
qualification.json, scene-gates-final.json, visual-review.json,
payload-comparison.json, timing-matched.json, analysis-matched.json,
timing-visual-deltas.json and timing-visual-review.json. Full profiles, images
and logs remain there. Small receipts and review images are mirrored under the
engine's ignored artifacts/lighting-foundation/reflected-submission-20261028.
