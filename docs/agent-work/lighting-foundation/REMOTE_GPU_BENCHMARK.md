# Remote Linux lighting benchmark — 2026-10-24

## Scope and status

The user requested that the new `gpu-worker` machine perform the deferred lighting
performance work. Planning and code review remain with the cloud model; no local
LLM is used. Lighting visuals remain parked at the existing subjective ~90%
estimate. This qualification does not change lighting quality.

Native Linux compilation, nine CPU contracts, six native GPU contracts and the
four-case 1080p runtime matrix pass their correctness checks. The full-quality
scene does not meet 60 FPS. Twelve full-HD measured runs include a paired
original/current shader comparison and a warm repeat. The warm pair observed
6.6% lower median GPU time in dry daylight and 13.6% lower in heavy rain.
Windows D3D12 timings remain a separate qualification. Do not compare these Linux
results to the previously contended Windows runs as a measured optimization gain.

## Host and staging

- SSH alias `gpu-worker`, `WORKER_LAN_ADDRESS`, Ubuntu 26.04.1, kernel 7.0.0-38-generic.
- Intel i7-8700, 45 GiB usable RAM, GCC 15.2.0, CMake 4.2.3, Ninja 1.13.2.
- Two RTX 2060 SUPER cards, 8 GiB each; NVIDIA 595.99.02. The renderer uses one
  card, not a combined 16 GiB device. GPU 0 has UUID
  `GPU_0_UUID`.
- Source snapshot, native builds and caches:
  `USER_HOME/projects/phlosion-benchmark` on the Ubuntu SSD.
- Prepared assets:
  `GPU_WORKLOADS_ROOT/datasets/phlosion-benchmark-20261024`.
- Transfer snapshot:
  `GPU_WORKLOADS_ROOT/archives/phlosion-benchmark-20261024`.

The snapshot includes the existing dirty engine/game sources and prepared runtime
content, not Windows build outputs. All 5,077 transferred files were SHA-256
verified. The three dependency source snapshots are recorded by revision and
per-file hashes; they intentionally have no Git credentials or `.git` directories.
The source/build manifest is `build-receipt.json`. All 17 generic engine and seven
game Vulkan SPIR-V modules match the current Windows modules byte for byte.

Local evidence and setup scripts live in the game's
`artifacts/lighting-foundation/remote-linux-20261024/`. Remote logs, captures and
manifests live under the benchmark workspace's `logs/`, `contracts/` and `results/`.

## Linux setup fixes

- Request ImGui's Direct3D binding only on Windows; enable the Vulkan loader's
  XCB, Xlib and Wayland features on Linux. Without the latter, the bundled loader
  could enumerate devices but could not create the SDL desktop surface.
- Replace Jolt's variadic logging lambda with an equivalent free function that
  GCC supports. Guard two D3D12 implementation details with `_WIN32`.
- Export the game's battle-rules plugin on ELF platforms using the existing
  plugin-export convention. Explicitly default-initialize two aggregate fields,
  order a constructor's initializers by declaration, and split misleadingly
  indented statements. No gameplay or lighting algorithm changes are intended.
- Configure with `CMAKE_POSITION_INDEPENDENT_CODE=ON` for the shared plugins.

The pinned vcpkg baseline is unchanged. Its libsystemd 257.8 dependency needs the
[upstream errno-alias fix](https://github.com/systemd/systemd/commit/3cfb16998808a6ec8012a6120d0a82f0e1a0c8bb)
on the new Linux headers. `prepare-overlay.py` creates an explicit local port
overlay with that one-line backport. The benchmark does not replace the host's
systemd or NVIDIA driver. `build-dependencies.sh` also exposes the additional
Autoconf macros through a user-owned tools prefix.

## Measurement protocol

`run.py` uses the existing weather verifier's scene inputs and fixed-step replay
contract, translated to native Linux paths and `.so` plugins. It requests a hidden
1920x1080 X11 surface on the logged-in desktop, pins the NVIDIA ICD, checks that
the model service is inactive, and records GPU clocks, power, memory and processes.
Validation runs precede timing runs; validation is disabled for timings.

Cases: dry noon, wet night, moving wet-night camera, and battle. Keep full lighting,
contact visibility, reflections, weather and native model content. Use the same
200-frame windows as the previous pass: 30–229 for exploration and 120–319 for
battle. Record host wall time separately from GPU time. Cache preparation is
outside those windows. A successful capture is not a 60 FPS pass; the target is
16.67 ms per frame and remains explicit in the result manifest.

## Checks completed

- Game: outdoor profile, world environment, battle rules, starter moves (4).
- Engine: collision world, contact visibility, local reflections, screen
  reflections, reflection response (5). These unchanged existing tests link
  against the same native libraries as the runtime; compile/link commands are
  retained in `contracts/commands.json`.
- Vulkan validation: contact/direct visibility, local-reflection mirror,
  screen-reflection mirror, and reflection-response energy/wet (6). All keep
  their existing assertions and 240-second native test timeout.
- Game runtime: a validation-enabled wet-night capture, followed by four full-HD
  timing captures without validation. Weather/resource checks pass; battle loads
  native models and reaches the expected battle state.

## Initial full-quality results

All values below come from `results/timing-1/manifest.json`, with 200 warmed
samples per case. These are Linux runtime observations, not Windows acceptance.

| Scene | Median GPU ms | Median host ms | Host p99 ms | Total process seconds |
| --- | ---: | ---: | ---: | ---: |
| Dry noon | 129.089 | 129.479 | 164.453 | 51.54 |
| Wet night | 169.229 | 169.079 | 205.767 | 58.89 |
| Moving wet night | 204.404 | 206.581 | 237.596 | 65.91 |
| Wet battle | 162.618 | 163.373 | 322.175 | 89.16 |

The runtime used GPU 0; GPU 1 remained idle. The only other recorded graphics
processes were the normal GNOME/Xwayland desktop, without a competing game or
inference workload. Peak GPU 0 memory was 1,572 MiB. Active telemetry clocks
ranged from 1,650 to 1,950 MHz (median 1,755 MHz), and temperature reached 86 C.
Automatic clock/temperature variation remains part of these measurements; do not
treat the new host as a fixed-clock lab setup. The cost is not a VRAM-capacity
failure, and combining the two cards' advertised memory would not fix it.

NVIDIA explicitly reported **SW Thermal Slowdown: Active** on GPU 0 during the
comparison (`results/original-timing/thermal-status.txt`). This is a hardware
condition to retain in future comparisons, not a rendering-quality change.

## Original/current comparison and image limits

Both warm cohorts used the same native executable, scene, replay, content and
driver. Only the 24 pre/post-optimization SPIR-V modules changed. Both cases in
both cohorts had a 1,755 MHz median active-process clock and reached 86 C; clocks
were automatic, not locked. The first cold-original dry comparison was less
comparable (1,905 versus 1,755 MHz) and is retained separately, not the headline.

| Warm case | Original median GPU ms | Current median GPU ms | GPU time reduction |
| --- | ---: | ---: | ---: |
| Dry noon | 148.548 | 138.761 | 6.6% |
| Wet night | 201.315 | 173.902 | 13.6% |

The earlier current wet samples were 169.229 and 170.647 ms; dry samples were
129.089 and 138.125 ms as the card heated up. This is an observed improvement on
this host, not a universal speedup or a confidence interval. The 16.67 ms target
still needs much larger savings. The earlier closeout's reflection search and
filtering costs remain the next architectural performance work; no new visual
feature or quality reduction was introduced here.

All ten compared game-state groups match across original/current/repeat captures.
The warm original/current images differ by at most 1/255 per channel (1,330 dry
pixels and 440 wet pixels). The earlier dry pair has one pixel at 2/255; all other
differences are at most 1/255. Unchanged-build repeats also differ by up to 1/255,
so **the strict pixel-identical repeat gate is not met on this Linux run**. This
is explicitly recorded as false in `final-audit.json`; no existing test threshold
was relaxed and these captures do not replace the earlier accepted references.

Windows/Linux comparison is also not pixel-identical despite identical SPIR-V.
Pixels differing by more than 1/255: dry 17, wet 276, orbit 31, battle 7, out of
2,073,600 pixels each. Maximum channel deltas are 3, 10, 89 and 98 respectively;
the largest are sparse. Native GPU contracts passed, but full cross-platform
image acceptance remains separate. Existing strict reflection discrepancies from
the previous visual work remain open.

The first validation capture took 287.8 seconds including cold preparation. The
first original-shader capture took 308.0 seconds including its shader preparation.
Cached current cases took 51.5–89.2 seconds total including measured gameplay;
logged field load was about 0.9 seconds, battle load 6.4 seconds, prewarm 4.3–4.9
seconds, and the first host frame 2.6–2.9 seconds. These are separate scopes, not
an additive startup breakdown. Startup and transition debt remain explicit.

`analysis.json` retains all distributions, clocks and image comparisons.
`final-audit.json` verifies 2,729 canonical source inputs, the 12 portability
changes, 24 identical Windows/Linux SPIR-V modules, both shader restorations and
the 12 measured runs. Prior dirty work is preserved. The worker is left on the
current shader set; no runtime, benchmark or local inference job remains running.

This is a targeted native runtime qualification. It is not a complete Linux
editor, launcher, Wayland, packaging, or release qualification.

## Repeating the measurement

The remote workspace is a frozen copy, not an automatically synchronized checkout.
Update and hash-check its inputs before testing newer local changes. The full
setup and runner scripts are retained beside the local evidence; their matching
copies are in the remote workspace. No Codex or model process runs on the worker.

For another run of this exact build, while the Ubuntu desktop remains logged in:

```powershell
ssh gpu-worker 'python3 USER_HOME/projects/phlosion-benchmark/run.py timing-next > USER_HOME/projects/phlosion-benchmark/logs/timing-next.log 2>&1'
```

Choose a fresh phase name so previous measurements cannot be overwritten. The
runner obtains the current X11 authorization file, checks that the local model
service is stopped, and retains commands, settings, GPU telemetry, screenshots,
resource verification and frame distributions. Do not run another rendering,
inference or build job alongside the timing run. A disconnected graphical login
needs its display session restored before this desktop-surface runner can work.

`run-paired.py` compares the retained pre-optimization Vulkan SPIR-V against the
current modules using the same native executable and prepared content. The
previous performance-only patch changed no Vulkan host C++ code. The comparison
swaps 17 engine and seven game modules in the isolated remote workspace, retains
hashes and backups, and restores all 24 current modules in `finally` before the
final repeat. It does not modify the canonical Windows build or source trees.
