# Captured local-light response: GPU qualification

2026-10-17. This qualifies an independent local-light implementation against one
Cyberpunk 2077 2.31 captured shader dispatch. It does not measure a percentage of
the whole lighting engine. The captured rendering mode has ray tracing disabled.
The normal Phlosion game/editor lighting path was unchanged at this isolated
checkpoint. The subsequent production binding, expanded domain tests and guarded
replay are documented in [LIVE_FINITE_LIGHTS.md](LIVE_FINITE_LIGHTS.md).

## What is implemented

`assets/shaders/vulkan/world_finite_light.glsl` contains portable finite-source
distance and falloff, representative-point line/sphere response, roughness-aware
diffuse, correlated GGX specular and two-moment shadow visibility. The private
HLSL adapter independently reconstructs the captured surface/material inputs,
walks the captured light lists, applies shape/angular/channel/occlusion controls,
samples the supplied shadow moments and writes separate diffuse/specular HDR.
It does not call or interpret retail bytecode to compute the port's output.

The original retail shader is the comparison oracle. Both versions use the same
GPU resources in the same replay, with directional-light RGB temporarily zeroed
in both. No source intensity, material or color was fitted. Both comparisons are
after captured exposure and half-float output storage; float32 internal equality
is not claimed. Input resources and original shader binaries remain private.

## Results

All 3,413 dispatched 16x16 tiles were compared: 873,728 pixels and 2,621,184 RGB
channels per lobe. Aggregate relative L1 is `sum(abs(port-reference)) /
sum(abs(reference))`; the table expresses that error as a percentage.

| Measurement | Diffuse | Specular |
| --- | ---: | ---: |
| Aggregate relative L1 error | 0.00006486% | 0.00002898% |
| Exactly equal channels | 2,620,704 | 2,620,907 |
| Exactly equal among nonzero reference channels | 99.96983% | 99.98232% |
| Illuminated pixels | 541,554 | 542,323 |
| Maximum absolute HDR error | 0.00289154 | 0.000432968 |
| Pixels above 2% relative error | 4 | 2 |
| Maximum per-pixel relative error | 28.6904% | 36.5834% |

Combined, 99.98556% of RGB channels are bit-identical. The nonzero-only figures
above prevent black background from obscuring the meaning of that result.
Per-pixel error is maximum RGB absolute error divided by maximum reference RGB,
with a denominator floor of 0.001. The 99.9th-percentile value is zero for both
lobes. Declared acceptance before the first run was per-lobe aggregate error at
most 0.1%, 99.9th-percentile pixel error at most 2%, and finite active outputs.
Those limits pass; they do not assert that every pixel is within 2%.

The four larger errors occur at (168,665), (135,681), (183,688), (114,718), with
the latter two also affecting specular. Diagnostic evaluation associates these
with a shadowed light. A shadow-ray precision experiment changed the outliers
but increased overall error; it was rejected. Projection/filter precision is a
remaining investigation, not a proven exact match. No pixels were excluded.

## Replay preservation

The captured local-light outputs after restoring the probe are bit-identical to
the ordinary replay. Sixteen static input buffers/textures are byte-identical
between runs. GPU-written allocation addresses and tile order differ; semantic
comparison verifies all 58,880 ordered cluster light lists, head flags and all
3,413 dispatched tile identities match.

The later composed frame is not bit-exact. Against the baseline, the port run
changes 446 half-float channels by one or two ULPs. A no-port control changes 567
by one or two ULPs, and an independent current-GPU PIX export changes 246 by one
or two ULPs. The original strict `case.json` result remains `requires-review`.
`qualification-v2.json` separately classifies it as qualified with numerical
limits, using the ordinary control/PIX envelope. No image threshold was changed
to conceal this distinction. The first qualification report is retained; v2
avoids doing arithmetic on unexecuted tiles' undefined allocation contents.

## Public engine tests

`PhlosionFiniteLightTests` dispatches the same core in standalone native compute
fixtures on OpenGL, Vulkan and D3D12. Sixteen synthetic cases / 128 channels per
API compare against independent float64 expected values, with absolute tolerance
2e-6 plus 0.2% of the expected value. They exercise point, line and sphere response,
roughness extremes, metal/dielectric response, back-facing normals, radius-zero
specular, distance cutoff and dim-light threshold, attenuation cap, shadow moment
visibility/opacity, rotation and endpoint reversal. All pass. Maximum absolute
error is 1.15973e-6 on GL and 7.47621e-7 on Vulkan/D3D12.

These are shader portability/response tests, not a game material, editor, shadow
generation or performance test. Inputs use normalized normals/views, roughness
in [0.04,1], nonnegative colors and consistent source lengths. Singular cases
(receiver at emitter/endpoints or a parallel representative-ray denominator)
need a defined production contract before live integration.

From the engine repository in PowerShell:

```powershell
cmake --build build-presentation --config Release --target PhlosionFiniteLightTests --parallel 4
ctest --test-dir build-presentation -C Release -R '^PhlosionFiniteLight-' --output-on-failure -j 1
```

Private reproduction uses `tools/lighting_matched/prepare.py`, the existing owned
replay CMake build, `run_gpu.py <fresh-case>`, then
`qualify_gpu.py <fresh-case> --control control-current-02`. The case directories
retain exact commands, shader/compiler/replay hashes and all output hashes.
The final case is `evidence/lighting-matched/gpu/local-o3-final-04` in
`D:/Projects/Research/CyberpunkRainResearch`. The independently evaluated public
goldens can be reproduced there with `synthetic_reference.py`; it reads no capture
data. Engine logs and a source manifest are under
`artifacts/lighting-foundation/captured-local-gpu`.

## Path to lighting-engine parity

| Component | Evidence now | Next gate |
| --- | --- | --- |
| Local source-to-surface response | Independent CPU and GPU comparison; GPU core works on all three APIs | Explicit finite-source parameters, intensity units, singular-input behavior and live material binding |
| Material inputs | Captured deferred material decoding reproduced in private adapter | Map Phlosion authored materials without assuming captured packing or exposure |
| Shadows | Captured moment visibility reproduced, with four edge outliers | Recover moment generation/filtering and source projection; existing depth-PCF is a different path |
| Reflections | Phlosion planar reflection behavior verified previously | Recover and compare the captured reflection path; no SSR/RT parity established |
| Indirect light | Phlosion static diffuse probes verified previously | Compare captured probe sampling, visibility and material response |
| Volume lighting | Phlosion rectangle scattering verified previously | Recover and compare the captured fog-light transport |
| Exposure / display | Phlosion HDR, bloom and display transform verified previously | Separate captured exposure, bloom and final mapping comparisons |

First integrate a generic finite-source contract into the engine, keeping
rectangular radiance/LTC sources available. Captured intensity parameters must
not silently reuse `WorldRectLight::radiance`: source area, angular distribution
and units need an explicit mapping. The game then binds canopy fixtures to that
contract while preserving authored geometry and emission. Shadow, haze, bounce
and reflected-view consumers must receive a coherent source description.

Production acceptance requires the existing runtime/editor/API/shared-model
matrix, source-off/day/night/oblique/wet controls, ABI/plugin rebuilds if layouts
change, and GPU cost after correctness. More captures are not needed for this
immediate step. Profile textures, other shader permutations/frames and ray/path
traced modes remain outside the current measured coverage.
