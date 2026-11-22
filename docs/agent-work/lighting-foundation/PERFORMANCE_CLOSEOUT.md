# Lighting performance closeout — 2026-10-21

2026-10-24/25 follow-up: [screen-reflection search acceleration](SCREEN_REFLECTION_PERFORMANCE.md)
records the new conservative tile search, remote Windows measurements and image
preservation checks. The historical results below remain unchanged.

2026-10-24 follow-up: [remote Linux Vulkan measurements](REMOTE_GPU_BENCHMARK.md)
are now available from `gpu-worker`, with no competing game or local inference.
The historical Windows measurements below remain unchanged. The remote runtime
passes its targeted correctness checks but still misses 60 FPS; GPU clock and
thermal limits are recorded explicitly. Windows D3D12 timing remains separate.

## Status

Visual work remains parked at the subjective **about 90%** estimate for the tested
non-RT lighting foundation. This pass removes redundant shader work without
changing quality settings. It does not raise that visual estimate. The current
full-quality 1080p configuration has **not met 60 FPS / 16.67 ms**.

The user explicitly deferred clean performance timing after competing GPU work
was identified in ChatGPT and Character Creator. Collected timings remain
diagnostic observations, not an isolated speedup claim or performance acceptance.
The bounded code pass and correctness checks are complete, with the existing
strict reflection failures retained below. No commits or pushes are included.

Evidence lives in each affected repository under
`artifacts/lighting-foundation/performance-closeout-20261021/`.
The game's `before-source/`, `before-source.json`, `performance-only.patch`,
`final-source.json`, and `qualified-builds.json` preserve the starting edits and
identify this pass's source/binaries separately from earlier uncommitted work.

## Measurement conditions

Windows, RelWithDebInfo, 1920×1080 runtime, primary RTX 2060 SUPER, driver 617.42.
Its UUID is `GPU_0_UUID`; the second RTX 2060 SUPER
is not combined with it. GPU runs within this task are sequential; other apps
continued using the same card. Per-second utilization, clocks, temperature and
memory are retained, along with the later `gpu-owners.jsonl` process samples.
The GPU owner monitor was stopped when the user deferred clean timing.
Each stationary result uses 200 warmed frames, frames 30 through 229, with
fixed simulation steps and existing performance diagnostics. Graphics validation
is disabled during timing and enabled during native/image qualification.

| View | Backend | Median GPU ms | Median host frame ms | Host p99 ms |
| --- | --- | ---: | ---: | ---: |
| Wet night | D3D12 | 212.725 | 218.155 | 291.625 |
| Wet night | Vulkan | 248.590 | 250.480 | 307.632 |
| Dry noon | D3D12 | 125.424 | 127.025 | 163.470 |
| Dry noon | Vulkan | 212.019 | 215.897 | 260.660 |

These are complete scene costs, not isolated light evaluation times. The GPU is
the principal limitation. Host wall time includes GPU waits and must not be
presented as CPU compute time. Startup/shader preparation is excluded from the
warmed frame window and reported separately.

Matched D3D12 wet-night diagnostic controls measured 190.958 ms with contact
visibility disabled, 134.753 ms with local reflections disabled, and 83.748 ms
with screen reflections disabled. These independent controls are not additive
pass timings. Their disabled features are not proposed production settings.

## Changes

- Reuse the selected contact depth and explicitly scan its four candidates,
  retaining the existing tie order, search samples, spatial/history filters and
  reconstruction.
- Skip local/screen reflection fallbacks only when the planar source completely
  replaces their contribution. Reuse the computed planar coordinates for partial
  coverage, retaining the original source blend order.
- Reuse local-probe coordinates, avoid revisiting the same captured plane,
  avoid identical mip samples, and omit zero-weight filter taps. Every distinct
  trace sample and contributing filter tap remains.
- Recover screen-trace clip depth directly from inverse homogeneous W. This
  removes a redundant inverse/forward matrix round trip at each ray sample.
  Pixel-sized ray spacing and the existing 4096-step ceiling remain unchanged.

These changes are engine-owned and shared by D3D12/Vulkan world and character
materials. No light, shadow, reflection, contact, rain or resolution setting was
reduced. Game scene policy and research inputs are unchanged.

## Verification

All 188 native contact, planar, probe, screen-reflection and response cases pass
with graphics validation enabled and the original deadlines. Of 162 retained
reference images, 150 are exact; twelve screen-reflection images differ by at
most one byte following the depth calculation simplification. All contact,
planar, selected probe and material-response reference images are exact.

Four engine, two Cyberpunk and four Autochess CPU contracts pass. The normal and
presentation editor builds, Cyberpunk runtime/plugin and Autochess runtime/plugin
build successfully. The four initial/final full-HD stationary game comparisons
differ by at most 1/255.

Normal game launch/staging and same-session station/reference switching pass on
both APIs. Eight wet station/battle captures pass the existing expected-content,
host and renderer guards. Autochess passes four native and 34 editor material
captures on both APIs with the original content/image guards; all 38 match the
previous accepted images exactly. Its editor/plugin compatibility and source
proof also pass. No graphics validation errors were reported by these matrices.

The 22 station/battle/close-view/replay images preserve all five recorded game
state groups. The lower-resolution comparisons are not pixel-identical: at most
17 pixels per image differ by more than 1/255, at most one differs by more than
8/255, and the largest individual difference is 18/255. Worst image-wide mean
difference is 0.00165/255. The full-HD static comparison above and native limits
must not be presented as a bound on every moving/close view. Existing station
content and cross-API guards pass without changing their thresholds.

Five full-HD actor captures retain the original strict failure: four D3D12
pixels outside the reflected-character bounds differ by 1/255, including the
same-input repeat. Vulkan remains exact outside those bounds but has the same
dimmer character signal (347 pixels above 3/255 versus D3D12's 2760). All actor
isolation diagnostic fields match the previous pass. The strict verifier still
returns failure; it was not relaxed. Actor images versus the previous build
differ by at most 7/255. Graphics validation reports no errors. The native
away-facing cross-API 2/255 discrepancy also remains, because its before/after
contact images are exact. None of these existing issues is claimed fixed.

`closeout-audit.json` in the game evidence verifies the nine changed code/shader
files, five final binaries, all 17 restored Vulkan shader binaries and 31
untouched files from the starting dirty trees. Earlier uncommitted work was
preserved. The normal launch stages the final game plugin and uses the new build.

## Final timing observations — clean repeat deferred

The final combined matrix (`final/timings.json`) observed:

| View | Backend | Median GPU ms | Median host ms | Host p99 ms |
| --- | --- | ---: | ---: | ---: |
| Wet night | D3D12 | 185.699 | 190.380 | 275.392 |
| Dry noon | D3D12 | 85.980 | 86.857 | 117.349 |
| Moving wet night | D3D12 | 195.572 | 195.786 | 226.181 |
| Wet battle | D3D12 | 145.705 | 150.916 | 301.606 |
| Wet night | Vulkan | 161.221 | 163.137 | 175.137 |
| Dry noon | Vulkan | 134.979 | 137.013 | 152.724 |
| Moving wet night | Vulkan | 201.153 | 203.143 | 238.600 |
| Wet battle | Vulkan | 165.299 | 167.211 | 312.254 |

Field captures use frames 30–229; battle uses 120–319. An original/final warm
repeat reached the same median 1755 MHz active GPU clock but still overlapped
other applications. These are **not controlled gains**:

| View | Backend | Original median GPU ms | Final median GPU ms |
| --- | --- | ---: | ---: |
| Dry noon | D3D12 | 94.491 | 94.429 |
| Dry noon | Vulkan | 157.262 | 139.935 |
| Wet night | D3D12 | 195.410 | 166.799 |
| Wet night | Vulkan | 202.959 | 175.815 |

Full distributions are in `baseline-warm/timings.json` and
`final-warm/timings.json`. The original executable, shader reconstruction,
commands and hashes are retained. Temporary original Vulkan binaries were
restored to the final versions and hash-checked; see
`original-vulkan/swap-receipt.json`. Ordinary profiles were not replaced.

The initial baseline and intermediate candidate also ran at different clocks
(about 1755–1770 versus 1890–1905 MHz). Those results remain in the artifacts but
must not be used to advertise a code-only percentage improvement.

## Remaining work and restart point

The current screen tracer searches individual depth pixels, and local probes
perform visibility tracing and up to 64 convolution taps at each receiving
fragment. Accelerating those searches/filtering is a larger architectural task.
Investigate accelerated searches and reusable filtering against the retained
image gates, separately from any new visual-parity target or feature reduction.

Startup remains slow. The first final D3D12 case took about 1020 seconds overall,
including 777 seconds reported in battle prewarming; Vulkan took about 345
seconds overall. Warm caches do not eliminate loading/transition stalls. Keep
cold preparation separate rather than presenting warmed captures as fast startup.
Autochess's separate initial shader preparation took 497 seconds on Vulkan and
1060 seconds on D3D12; its second cohorts took 65 and 60 seconds respectively.
These preparation runs passed their separate watchdog, not the production
capture deadline. Compilation/caching work remains a substantial follow-up.

Clean performance qualification is deferred by the user. Resume the retained
fixed scenes/commands after competing GPU activity is paused. Repeat original
and final runs with comparable clocks, reporting frame distributions and startup
separately. No new Cyberpunk 2077 capture is needed. Linux and other hardware
remain unqualified; neither newer hardware nor two installed cards proves 60 FPS.

Keep the remaining visual backlog in `CHECKPOINT.md`. Other game/environment work
can resume with the performance debt explicitly recorded; this is not a claim
that the full-quality scene is ready for a 60 FPS release.
