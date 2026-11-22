# Windows GPU cooling retest — 2026-10-24

## Result

Remote Windows access works, and the real 1920x1080 wet-night Outskirt scene
completed both a short Vulkan check and a sustained 3,600-frame run. Existing
weather-clock and resource checks passed; the final image was inspected. No
lighting quality, renderer code, driver, fan curve or power limit was changed.
The existing subjective lighting estimate remains parked at approximately 90%.

The top ASUS RTX 2060 SUPER still thermally limits its speed after repasting.
The case was closed and Adam confirmed the GPU fans were physically spinning.
Room temperature was not measured. The lower EVGA card was idle throughout the
observed run; its memory does not combine with the ASUS card's 8 GB.

| Sustained Vulkan measurement | Result |
| --- | ---: |
| Process duration, including startup | 642.1 s |
| Span between first/last samples at >=90% GPU utilization | 615.9 s |
| Whole loaded-run temperature median / maximum | 86 / 87 C |
| Final 120 seconds temperature median | 86 C |
| Final 120 seconds reported fan speed median | 85% |
| Final 120 seconds graphics clock median | 1,695 MHz |
| Final 120 seconds power median | 116.6 W |
| Final 120 seconds utilization median | 100% |
| Samples reporting software thermal slowdown | 353 |

The earlier pre-repaste Ubuntu warm measurements had 85 C median / 86 C maximum
temperature and 1,755 MHz median graphics clock. These are not a controlled
before/after paste comparison: Windows uses NVIDIA 610.88, Ubuntu used 595.99.02,
and the earlier warm cohort combined shorter dry/wet captures. Current telemetry
does establish that thermal speed reduction remains. A matching side-panel-off
run is the next diagnostic for case airflow; it has not been performed.

The game process exited normally, the manual scheduled task returned to Ready
with result zero, and the ASUS subsequently idled at 55 C and 1% utilization.
There is no benchmark left running.

## Reproducibility and limits

- Windows SSH alias: `windows-gpu-worker`; the existing Ubuntu alias is unchanged.
- GPU: top ASUS, PCI `01:00.0`, UUID
  `GPU_0_UUID`.
- Runtime SHA-256:
  `98886cc7f45404cd4f51c8999e6563a9a35ab25245e2ce5fd117ab8513677f9e`;
  matches the previously qualified Windows executable.
- Remote runner: `GPU_WORKLOADS_ROOT\phlosion-benchmark-windows-20261024`.
  Runtime and runner live on the Windows SSD; prepared private content is reused
  from `GPU_WORKLOADS_ROOT\datasets\phlosion-benchmark-20261024`.
- The interactive scheduled task runs under Adam's logged-in desktop. SSH only
  controls it. The harness has a 20-minute watchdog and an 88 C stop threshold.
  It has no automatic boot/logon trigger. No local AI was used.
- The verified package has 98 files, including ten signature-verified app-local
  Microsoft CRT DLLs; ten tracked weather-audio files were separately supplied
  and hash-verified. Small compatibility paths supply embedded developer shader
  and rain-config paths. This is not portable release packaging.

The existing 200-frame timing window is frames 30–229: median GPU time was
156.667 ms in the short run and 154.783 ms early in the sustained run. These
early windows are **not** measurements after the card reached its final thermal
state, nor an optimization or 60 FPS acceptance result. No new performance gain
is claimed. The current pass primarily qualifies remote access and cooling.

D3D12 initialized and passed its startup parity signature, but its first cold
shader preparation was interrupted before completing the scene. Full D3D12
visual and timing checks remain pending. Initial failed attempts and their
missing-config/audio diagnostics are retained separately from the two passes.

## Evidence and repeat

Local manifests, scripts, telemetry, screenshots and `analysis.json`:
`PROJECTS_ROOT\Games\PokemonCyberpunk\artifacts\lighting-foundation\remote-windows-20261024`.
The successful phases are `smoke-vulkan-complete` and `thermal-vulkan`.
Pre-repaste evidence is alongside them in `remote-linux-20261024`.
`closeout.json` records a four-hour offset between the remote and controller UTC
clocks. Do not align these logs by their absolute timestamp without correcting
that offset; the elapsed run and telemetry intervals use the remote clock alone.

For another authorized run of these frozen inputs, with Windows booted and Adam
logged in, choose a fresh phase name:

```powershell
ssh windows-gpu-worker powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File GPU_WORKLOADS_ROOT/phlosion-benchmark-windows-20261024/start.ps1 -Phase thermal-vulkan-repeat -Api vulkan -Frames 3600
```

Record any physical setup changes before starting; preserve the same scene,
resolution, settings and driver. Inspect `results/<phase>/manifest.json` and
`gpu.csv`, copy results back, then run the retained `analyze.py`. Source changes
are not automatically synchronized to this frozen deployment. Machine access
and rollback are documented in codex-infra's
`docs/qualifications/2026-10-24-windows-gpu-access.md`.
