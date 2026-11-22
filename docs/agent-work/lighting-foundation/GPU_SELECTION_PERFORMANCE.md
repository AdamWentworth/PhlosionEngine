# Windows GPU selection and performance follow-up — 2026-10-25

The accepted lighting/rain implementation remains at the committed checkpoint.
No new shader optimization was accepted in this pass. gpu-worker performed every
edit, build, GPU/CPU test and image comparison; controller only reviewed and
integrated the returned delta. No local inference was used.

## GPU selection

PHLOSION_GPU_LUID selects the exact Windows device in D3D12 and Vulkan, including
two cards with identical model names. Explicit invalid/unavailable IDs fail.
Unset it for automatic/preferred-name selection. IDs are boot-local, so refresh
them after reboot; NVIDIA index and Vulkan enumeration order are not substitutes.

| Physical card | NVIDIA index / PCI bus | This boot's LUID |
| --- | --- | --- |
| ASUS, top slot | 0 / 01:00.0 | 000000000000ad83 |
| EVGA, lower slot | 1 / 02:00.0 | 000000000000be8e |

Both are RTX 2060 SUPER 8 GB, driver 610.88. Their VRAM is not pooled.
UUIDs: ASUS GPU_0_UUID;
EVGA GPU_1_UUID.
Telemetry confirmed the selected physical GPU. Automatic selection already
chooses ASUS on this worker in both APIs; switching to EVGA is not a speedup.

## Matched measurements

1920x1080, full-quality fixed wet-night/dry-noon scenes, 200 measured frames per
run, same source and settings. Values are median GPU milliseconds, not host FPS.

| View | ASUS | EVGA |
| --- | ---: | ---: |
| D3D12 wet | 131.688 | 141.987 |
| D3D12 dry | 76.819 | 84.076 |
| Vulkan wet | 131.715 | 141.007 |
| Vulkan dry | 109.718 | 118.485 |

ASUS took 6.6–8.6% less GPU time under these conditions. Its loaded clocks were
1905–1920 MHz and peak temperatures 81–82 C, with no thermal-slowdown samples.
Later final-build repeats reached 83-84 C on ASUS and reported thermal slowdown
in three of four views; those warmed results are retained and are not code-only
performance claims. EVGA was cooler (71 C) but ran at 1725–1792.5 MHz and reported thermal slowdown.
The cause of that behavior is not established. These observations do not qualify
shipping performance or thermally controlled card comparisons. The 88 C stop
and bounded run watchdog remained active; fan, driver and power settings were
not changed. The full wet view still greatly exceeds the 16.67 ms budget.

## Rejected experiments and remaining target

- Skipping repeated local-reflection texels: D3D12 wet 131.148 ms, only a 0.4%
  difference from baseline, with costly first shader preparation. Not accepted.
- Two-level 8/64-pixel screen bounds: Vulkan wet 136.259 ms, a 3.5% regression.
- Wider 16-pixel bounds: Vulkan wet 131.837 ms and dry 113.762 ms. No benefit.

The first two trials and the wider-tile wet view stayed within the existing
1/255 bound. The wider-tile dry view also failed that bound at one pixel (two
color steps). Performance did not justify keeping any trial. Failed setup attempts and the deliberately
cancelled first trial's Vulkan preparation remain in the evidence.

Diagnostic feature isolation on the unchanged Vulkan build measured 70.717 ms
with local reflections disabled and 63.844 ms with screen reflections disabled,
versus 131.715 ms with both enabled. These are diagnostic comparisons, not
quality-preserving optimizations or additive per-pass timings. Both reflection
paths warrant attention. Next investigate redundant reflection evaluation and
overdraw, with separate GPU timings for capture, tracing/filtering and final
composition before another algorithm change. Keep the existing visual gates.

## Verification and limits

- Final worker editor, game runtime and game plugin builds pass.
- Five CPU tests pass: renderer policy and four lighting contracts.
- All 42 native reflection cases pass with Vulkan core/synchronization validation
  and the D3D12 debug layer: local mirror/rough/masked plus all 18 screen cases
  on each API, including six thin-source offsets and square/partial extents.
- Four invalid/unavailable-GPU cases refuse to run; both automatic-selection
  mirror checks pass. Both physical GPUs rendered the full game on both APIs.
- Final full-quality wet/dry captures on both APIs match the selector baseline
  within 1/255 and match all ten recorded game-state groups. Both diagnostic
  disable flags are zero. Simulation clock and resource checks pass.
- All rain, lighting, reflection and material shaders are restored to HEAD.
  No game configuration or rendering algorithm changes are included.
- A prior-reference discrepancy remains: dry Vulkan differs from the older saved
  capture at one pixel (674,502) by two 8-bit color steps. An unchanged new-build
  repeat is pixel-identical to its first capture and reproduces the historical
  discrepancy. This predates these trials; the strict historical comparison
  remains failed. Its cause is not established and the threshold was not relaxed.
- This pass does not newly qualify Linux, the editor's GPU presentation matrix,
  Autochess, production startup time, or 60 FPS. No new visual-parity claim.

The worker's Direct3D Graphics Tools are now installed. The initial SSH denial
was resolved with a one-shot elevated interactive installation; no reboot was
needed and the temporary installation task was removed.

Evidence: the game's ignored
artifacts/lighting-foundation/gpu-selection-performance-20261025 directory.
Worker originals and frozen executable/shader payloads remain at
WORKER_EVIDENCE_ROOT/phlosion-perf-20261025. final-check.json binds accepted code hashes,
images and validation results; analysis.json retains all measured distributions
and telemetry summaries. Cold preparation is excluded from GPU frame times.
