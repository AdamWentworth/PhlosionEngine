# Offscreen reflections: fixed O1-O8 completion

**100% estimated mechanism coverage of the fixed supported Windows raster
contract.** This is not retail pixel/FPS identity or completion of other matrix
categories. O1-O8 were recorded before implementation. Final release requires
the exact canonical/locked-payload receipt in the evidence archive; that
receipt is the authority for O8 handoff.

## Fixed requirements

- **O1**: Retained static probe material producer/decoder: bounded compact storage, quantized albedo/normal/metal/roughness/log-depth, empty/sky handling, masked material eligibility, tested independent numerical packing/decode. Authored wetness, emission control and light channels remain explicit extensions.
- **O2**: Current illumination relighting: directional/local light participation and shadows, sky/sun/weather, neighbor radiance, material response, current emission and haze; retained capture survives lighting changes but changes with geometry/material/placement.
- **O3**: Active reference GGX prefilter: Weyl R2 samples, per-level accepted sample counts, PDF mip bias, normalized cosine weights, roughness/LOD inverse and box-domain handling; immutable prepared samples rather than repeated per-texel generation.
- **O4**: Radiance representation and seams: compact HDR radiance, complete roughness levels, face/hemisphere seam handling and no atlas bleed, energy/constant-radiance preservation, safe finite HDR values.
- **O5**: Placed-probe consumers: spatial influence, linear boundary fades, overlapping priorities, projection and fallback, stable authored placement, old/new completed generation blend. Supported ordinary raster material consumers agree.
- **O6**: Existing W5-W7 residency/prewarm/refresh/publication/fencing remain qualified; incomplete or canceled generations never become visible, relocations recapture, quality budgets and paused clocks remain live.
- **O7**: Normal production paths: both Windows D3D12/Vulkan, runtime/editor, field/battle, Low/Medium/High/Ultra, wet/dry and camera motion; no experimental opt-in or source-game dependency.
- **O8**: Independent CPU/native numerical and image checks, native validation, visual review, matched optimized whole-frame and memory measurements, exact tested handoff/dependency pin and committed evidence. No failed thresholds waived.

## Implemented behavior

- Retained static faces use an 8-byte quantized sqrt-albedo, camera-space normal
  XY/sign, metal/roughness and logarithmic view-depth core. Empty faces remain
  sky. An independent exact-depth check caught a D3D12 reciprocal-depth mistake;
  both APIs now obtain view depth from the capture matrix.
- HDR emission and authored wetness/emission/receiver channels remain a separate
  12-byte extension. Total retained payload is **20 bytes/texel**, not eight.
  The primary albedo/normal/material/depth planes are shared capture scratch;
  subsequent primary consumers explicitly reject retained probe storage.
- Current sun, local lights/shadows, neighbors, wetness, emission and haze relight
  retained geometry. An independent sky probe supplies directional filtered
  fallback outside local influence volumes.
- Immutable Weyl-R2 GGX kernels accept 16/32/64/128/128 hemisphere samples with
  PDF-driven source LOD, +1 bias and normalized cosine weights. All six roughness
  levels use the square/square-root inverse mapping.
- The 2048-square R11G11B10F atlas is **16 MiB**, previously 32 MiB.
  Border texels are prepared per probe level; ordinary hardware-filtered reads
  cross cube seams without per-pixel reconstruction or unrelated atlas tiles.
- Four local residents plus a global resident support rotated bounds, linear
  fades, ordered priorities, channel masks, optional projection, intensity and
  complete-generation blending. Eviction preserves global fallback. Reset,
  cancellation, relocation, prewarming, budgets and paused clocks remain live.

## Explicit adapters and scope

The reference cube uses box-scaled capture coordinates and inverse box size
during convolution. Phlosion captures world-oriented 90-degree faces and filters
in the world angular domain; applying inverse box size again would distort the
lobe. Receiver projection intersects the oriented box and forms the direction
from the authored capture origin.

Storage remains a cube atlas, not dual paraboloids. The sample LOD uses its
128-face solid-angle footprint, not the reference's configured 256 paraboloid
footprint. Numerical qualification checks integration and cube edge energy,
not identical source texels. The single-sample material producer does not enable
fractional supersampled sky coverage. Authored HDR emission controls deliberately
replace the reference's coarse luminance-driven emission approximation.

Ambient SH/GI compensation remains in the separate indirect-lighting category.
Linux, RT, unsupported materials and retail pixel/FPS identity are not claimed.
The supplied snapshot is not authenticated as the current retail revision.

## Performance correction

The first correctness-qualified version regressed roughly 4-8 ms in matched
runs. Pass profiles localized most of that cost to per-pixel reflection
composition. Preparing seam borders during probe updates moves that work out
of visible surface shading; local coverage also avoids unused global reads.
The original numerical bounds and quality settings are unchanged. The table
below describes the final candidate. Earlier timings remain in pre-padding
evidence rather than being discarded.

## Qualification

- 48 D3D12/Vulkan native cases: material packing, fresh/retained wet/emission/haze
  relighting, spatial/priority/channel/oriented/global consumers and lifecycle.
- Independent raw HDR oracle: constant radiance changes by at most **one ULP**
  across six levels; directional GGX differs by at most **three ULPs**, within
  the fixed four-ULP bound, including edges. The oracle does not import the
  production kernel generator. All raw/filtered seam borders additionally
  match their adjacent core samples exactly; CPU checks reject overlap.
- Four additional failure-injection cases separately exercise local and global
  capture cancellation on both APIs; a global failure cannot erase locals.
- 24 primary/planar-view regressions preserve masked/wet-emissive composition,
  primary/probe separation, reflected depth and secondary temporal reflections.
  Review caught an overly broad D3D12 compact-probe guard; a failing planar
  composition case reproduced it before the guard was narrowed.
- Eight engine and seven game CPU tests. D3D12 debug-layer and Vulkan native
  validation: no errors.
- 18 runtime/editor baseline/candidate scene pairs: wet/dry, orbit, battle and
  Low/Medium/High/Ultra. Previous visual baselines were reused only after all
  132 payload hashes matched the prior qualified candidate.
- Fresh matched 16-run ABBA timings: native 1920x1080 Medium, EVGA GPU1
  RTX 2060 SUPER (LUID 000000000000be8e), optimized RelWithDebInfo; validation
  and readbacks disabled during measured frames. 280 accepted steady frames
  per API/case/variant; equal weather/rain/settings. Automatic boost remains on.
  Whole-frame CPU tails and cold shader preparation remain separate performance
  work. Startup observations for the final candidate are recorded below;
  these were not controlled cold-start benchmarks. Loading is not qualified
  by the steady-frame result. These are not retail Cyberpunk measurements.

| API / scene | GPU median ms, before -> after | GPU p95 ms | CPU frame median ms | CPU frame p95 ms |
| --- | --- | --- | --- | --- |
| d3d12 / wet | 15.842 -> 15.320 | 16.710 -> 16.932 | 14.911 -> 14.376 | 25.732 -> 24.929 |
| d3d12 / battle | 14.937 -> 15.251 | 16.055 -> 16.371 | 14.608 -> 15.034 | 27.774 -> 27.997 |
| vulkan / wet | 16.047 -> 16.557 | 17.227 -> 17.795 | 15.940 -> 16.457 | 17.237 -> 17.801 |
| vulkan / battle | 15.421 -> 16.011 | 16.521 -> 17.186 | 15.340 -> 15.971 | 16.811 -> 17.720 |

Final GPU median changes range from 0.522 ms faster to 0.589 ms slower.
The earlier large regression is removed; this is not a speedup in every scene.
D3D12 CPU p95 remains approximately 25-28 ms, and Vulkan GPU p95 is approximately
17-18 ms. A locked 60 FPS result is not claimed by this mechanism qualification.

First qualifying runtime launch duration (new variants, including startup/prewarm and 48 validation frames):

- vulkan: 475.8 seconds.
- d3d12: 466.2 seconds.

Maximum observed worker temperature: **63 C**; no thermal-slowdown flags in accepted samples.

Peak measured total GPU memory (includes driver/desktop, not allocation-only):

- d3d12 / wet: 1605 -> 1587 MiB.
- d3d12 / battle: 1743 -> 1721 MiB.
- vulkan / wet: 1823 -> 1812 MiB.
- vulkan / battle: 1959 -> 1945 MiB.

## Evidence and maintenance

Local evidence: artifacts/lighting-foundation/offscreen-completion-20261031/evidence.zip.
Includes the fixed contract, audit anchors/hashes, failed-trial diagnoses, raw
numeric readbacks/oracles, native logs, scene comparisons, telemetry, source
manifests, locked payload comparison and finalization receipt. Raw proprietary
source, builds and extracted assets are excluded.

Regenerate the immutable table with tools/render/generate_probe_kernel.py.
Run PhlosionPlacedProbeTests with API, energy/gradient and output prefix, then
tools/render/check_probe_radiance.py with that prefix and mode. Numpy is used
only by qualification tooling. The additional placed cases are in CTest.
Do not loosen limits or restore stale primary-plane consumption to bypass a fail.

Next related gap: indirect/bounce lighting, including ambient-SH/GI integration
and updates using the retained relit resources.
