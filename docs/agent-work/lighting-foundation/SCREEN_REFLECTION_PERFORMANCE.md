# Screen-reflection search acceleration — 2026-10-24/25

The current subjective **about 90% visual baseline stays parked**. This pass
accelerates the existing reflection search without changing lighting, rain,
materials, resolution, reflection range, roughness filtering or sample spacing.
The full-quality scene still misses 60 FPS. No new retail capture is required.

## Change

Both enrolled backends build conservative minimum/maximum depth bounds for each
8×8 screen tile after capturing the original depth and radiance. The ray walker
skips original samples only when their entire depth interval cannot intersect
that tile. Potential intersections retain the original per-pixel search,
thin-surface handling, uncertain-hit selection and radiance filter.

The bounds use spare rows below radiance in the existing immutable RGBA16F
snapshot. At 1920×1080 the allocation remains 2048×2048. Some square/portrait
extents require a larger allocation; the 1024-square test explicitly checks that
growth preserves radiance. Above the existing 8192 allocation limit, the renderer
uses the original search. Empty, partial and unsafe-depth tiles are handled
explicitly; bounds expand beyond half-float rounding. The captured scene depth
must be bound during the tile-building pass on both backends.

`PHLOSION_DISABLE_SCREEN_DEPTH_TILES=1` is a diagnostic comparison control. It
disables bounds generation and uses the original search in the same executable.
There is no public uniform-layout or plugin ABI change.

## Remote measurements

Evidence: PokemonCyberpunk's ignored
`artifacts/lighting-foundation/reflection-performance-20261024/`.
The original source/compiled shaders are retained under `before/` and `baseline/`.
`analysis.json`, per-run manifests, GPU telemetry, screenshots and runtime state
make the observations reproducible. This pass does not commit existing dirty work.

Windows gpu-worker uses the ASUS RTX 2060 SUPER in the top PCIe slot, UUID
`GPU_0_UUID`, driver 610.88. The second card remains
idle. No local AI, driver, fan, power or clock changes are involved. Tests use the
logged-in console through SSH and a bounded scheduled task, with an 88 C stop
and 20-minute watchdog. Prepared game content stays in the isolated test package.

Each 1920×1080 run measures 200 rendered frames: field frames 30–229, battle
frames 120–319. Rain, animation, camera and weather state match between variants;
cold preparation is excluded from these frame-time summaries.

| Vulkan view | Original executable GPU ms | New executable, bounds off | Bounds on | Reduction vs original | Reduction on/off |
| --- | ---: | ---: | ---: | ---: | ---: |
| Heavy rain/night | 153.271 | 158.703 | 133.247 | 13.1% | 16.0% |
| Dry/noon | 125.439 | 132.115 | 113.920 | 9.2% | 13.8% |
| Moving camera/rain | 189.134 | 201.099 | 160.079 | 15.4% | 20.4% |
| Wet battle | 151.898 | 162.562 | 131.843 | 13.2% | 18.9% |

Values are median GPU milliseconds. These are useful observed improvements,
**not an isolated code-only percentage or shipping performance qualification**.
Maximum loaded temperatures were 80–84 C. Several runs reported thermal slowdown.
Bounds-on/off median loaded clocks were respectively 1920/1905, 1905/1905,
1905/1845 and 1800/1815 MHz. The original runs were 1920 MHz except battle at
1905 MHz. Repeat after cooling work for a controlled comparison. Battle p95
remains higher than its median; the saved distributions retain transition costs.

| D3D12 view | Original executable GPU ms | New executable, bounds off | Bounds on | Reduction vs original | Reduction on/off |
| --- | ---: | ---: | ---: | ---: | ---: |
| Heavy rain/night | 153.09 | 155.61 | 132.03 | 13.8% | 15.2% |
| Dry/noon | 83.82 | 84.24 | 77.90 | 7.1% | 7.5% |

The original/final D3D12 rain runs both had 1920 MHz median loaded clocks and no
thermal-slowdown samples; peak temperatures were 81/82 C. Dry original/final
clocks were 1905/1890 MHz, with one/two thermal samples. The final rebuilt Vulkan
executable repeats heavy rain at 134.67 ms, 1905 MHz median and 85 C peak.
That repeat validates the final binary after the D3D12 depth-binding correction
and copy-bounds guard; the earlier Vulkan matrix used the same shader and search.

These values remain far above the 16.67 ms GPU budget. The first new D3D12 run
took 460 seconds including preparation, versus 43–61 seconds for subsequent
runs. Trusted, source/options-keyed DXBC blobs were seeded into the isolated
cache; these are **not cold-start performance results**. Driver-specific Vulkan
pipeline caches were not transferred between GPUs. Remote D3D12 timings run
without its absent debug layer; D3D12 debug correctness is checked locally.

## Image and correctness evidence

- Four engine CPU contracts pass, including partial-tile and square layout checks.
- Remote Vulkan: 37 native reflection cases pass with core/synchronization
  validation. Its following D3D12 setup refuses because the remote Windows
  debug layer is absent; that environmental failure remains in the manifest.
- Local D3D12: 25 cases each with bounds enabled and disabled pass with the debug
  layer. All 50 captured image comparisons are pixel-identical.
- A square atlas-growth case passes on both APIs, enabled and disabled. Its four
  image comparisons are also pixel-identical.
- Four full-game Vulkan views match all ten recorded state groups. Their on/off
  and original/new comparisons differ by at most one 8-bit colour step. Combined
  with native and square cases, the initial matrix records 62 passing
  comparisons without relaxing the existing 1/255 preservation bound.
- Final D3D12 wet/dry on/off and original/new captures, plus the final Vulkan
  executable repeat, bring `tile-image-preservation.json` to 67 passing comparisons.
  All final full-game differences remain at most 1/255.
- Both normal editor launch probes pass. Wet station and battle runtime/editor
  checks pass all eight expected-content captures, including geometry, fixtures,
  weather, shelters and API parity. Their graphics-validation logs are clean.
- The game environment CPU contract and three Autochess material/parity CPU
  contracts pass. Autochess's source-bound editor/plugin pair builds and verifies.
  Both native model cohorts and all 17 two-API editor cases pass their existing
  content/parity gates. Compared with the previous build, 35 of the 38 images
  are exact and three differ by at most 1/255. Final validation logs are clean.

`closeout-audit.json` verifies 12 final code files, 42 generated/material files,
five built/staged binaries and 101 unchanged checkpoint files. The normal game
launch uses the new plugin. Both owned remote GPU tasks are idle and the final
candidate deployment is hash-verified. No fan/driver changes or local inference
were used. Linux has not been requalified for this new optimization.

Autochess's separate cache preparation took 300/63 seconds for its Vulkan cohorts
and 1399/60 seconds for D3D12. All four preparation processes succeeded within
their existing 1800-second preparation watchdog. The normal capture deadlines
remain unchanged. The 23-minute first D3D12 preparation is substantial startup
debt, not a passed production startup target; cache state differs from historical
runs, so this does not isolate a compile-time regression percentage.

The first native Autochess matrix was interrupted during D3D12 cohort 2 by a local
GTX 1070 display-driver reset: Windows recorded event 4101 at 00:47:36 and a
LiveKernelEvent 141, followed by Autochess and Character Creator exits. The remote
ASUS test machine was already idle. `native-first-attempt/` retains the failure,
images and Windows events. An unchanged two-API retry of cohort 2 passes, as does
the combined native matrix. The reset's root cause is not established, and no
driver or watchdog setting was changed. This is not a claim of system stability.

This bounded optimization pass is complete. All 19 accepted/baseline remote
performance runs passed their clock/resource checks; the lighting and shared
consumer audits total 105 image comparisons within 1/255. Owned test processes
are stopped. The normal editor/runtime builds and staged game plugin are current.
Thermally controlled performance acceptance, production startup performance and
the existing strict reflection discrepancies remain open. No commits or pushes
were made, and unrelated edits were preserved.

Execution correction, 2026-10-25: performance measurements ran remotely, but
renderer correctness and editor/shared-consumer GPU checks ran on controller. That
crossed the user's worker-only boundary. Those historical results are retained;
they do not authorize further local GPU execution. All subsequent GPU work for
this campaign, including validation and captures, must run on WORKER_LAN_ADDRESS
(`windows-gpu-worker` while Windows is booted). Missing tools on the worker must be
configured there or reported as a limitation, never bypassed by running locally.
Local frame times were not used for speedup claims.

The original strict actor-isolation and cross-API discrepancies described
in [the previous closeout](PERFORMANCE_CLOSEOUT.md) remain separate unresolved
issues; this optimization does not claim to fix them.

An earlier lazy-evaluation/filtering candidate regressed GPU time and was
discarded. Its `candidate-01` and `perf-reflection-after-*` evidence is retained
but is **not** the accepted implementation. The original lighting composition
was restored before implementing tile bounds. An initial D3D12 tile pass bound
the fallback depth texture; the thin-source test caught it. The corrected depth
binding is covered by the exact native image comparisons above.

## Next target

The original search remains inside tiles which may contain a hit. Offscreen
local reflections also retain visibility tracing and up to 64 convolution taps
per receiver. Investigate reusable reflection filtering next, against the same
image gates. Shader compilation/startup is another distinct cost. Keep these
tasks separate from new lighting features or reducing visual quality.

## Commit preparation and worker build qualification - 2026-10-25

The controller and worker matched all 60 outstanding engine/game files before
commit preparation. Fresh Windows builds on gpu-worker passed for PhlosionEditor,
PhlosionEditorPluginProbe, CyberpunkRuntime and CyberpunkEditorPlugin, along with
four engine lighting CPU contracts. Receipts are at
`WORKER_EVIDENCE_ROOT/phlosion-20261025/build-receipts.json` on the worker. Source manifests
bind those checks to the implementation; subsequent handoff changes are docs only.

The newer MSVC build has not replaced the measured runtime or received fresh GPU
qualification. Direct3D Graphics Tools installation through SSH returned Access
is denied. Resolve that on the worker before new D3D12 debug-layer checks. Existing
runtime/image evidence above remains historical and its limitations still apply.
