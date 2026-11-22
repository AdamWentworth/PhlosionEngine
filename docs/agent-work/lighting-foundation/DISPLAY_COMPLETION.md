# HDR display presentation - completion, 2026-11-03

**100% estimated coverage of the fixed N1-N8 ordinary Windows SDR contract.**
This completes the declared neon/HDR-scene presentation technique on D3D12 and
Vulkan. It does not score retail pixel identity, HDR10-monitor support or the
whole engine. The gates below were fixed before implementation.

The reference is the supplied 2021-06-10 renderer snapshot; private
source-manifest.json records the producer/consumer hashes.
[Academy ACES 1.0.3](https://github.com/ampas/aces-dev/tree/v1.0.3) provides the
public RRT/ODT foundation; its license is retained in licenses/ACES-1.0.3.txt.

## Fixed gates

N1. HDR scene input and ordered consumers: temporal/reflection feedback remains
scene-linear, all opaque/sky/transparent content reaches presentation once, and
editor UI remains outside exposure/bloom/tone mapping.

N2. GPU brightness measurement: 256-square metering, center and sky/emissive
weighting, 512 logarithmic bins over [-8,20], fractional-bin fixed-point counts,
and percentile-trimmed mean. No per-frame CPU brightness readback.

N3. Per-view exposure: linear-luminance adaptation, independent brighten/darken
speeds, camera-velocity damping, EV limits/compensation and camera/manual blend.
Cut, first use, resize, generation, retired-view return and zero/stable time are
explicit. Authored game exposure remains a compensation input.

N4. Optical bloom: thresholded nine-tap downsampling, multiresolution tent
upsampling, six default levels, explicit scene/bloom scales and level
normalization. No full-resolution 81-tap replacement or quality opt-in.

N5. Color presentation: logarithmic 48-cube LUT, ordered shadow/midtone/highlight,
lift/gamma/gain/offset, contrast, brightness/saturation/hue, adjusted SDR ACES
RRT/ODT, correct LUT boundaries/interpolation, and final SDR encoding/dither.
The selected game look uses the source default adjusted SDR curve and neutral
grading. Alternate authored tone operators, cinematic lens artifacts and HDR
monitor transports are separate options, not silently counted as completed.

N6. Ordinary integration: D3D12 and Vulkan runtime/editor, exploration and battle,
four normal presets, daylight/night/rain, no double display mapping in feedback,
and prepared shaders. Preserve Hybrid impacts, puddle rings and lighting math.

N7. Independent numerical and state evidence: metering distributions including
outliers, exposure steps and cuts, bloom impulses/edges, neutral/colored/HDR LUT
fixtures, native API comparison, invalid inputs and view lifetime/resize.

N8. Cost and delivery: optimized matched measurements, bounded resources, no
shipping readbacks, shader-cache/preparation compatibility, CPU regression checks,
and tested engine/game dependency integration.

## Explicit representation adapters

Phlosion retains un-pre-exposed scene/history storage. The display producer must
convert to the corresponding previous-exposure domain for bloom and back for
composition; it must not expose stored SSR/temporal history. GPU histogram partials
may store exact low/high 16-bit count halves in float textures. A flattened LUT
must preserve cube interpolation and edge semantics. These are storage adapters,
not replacements for omitted producers or histories.

## Connected implementation and source mapping

| Reference | Implementation and evidence |
| --- | --- |
| renderNode_Histogram, renderHistogram, histogramCompute | WorldDisplayPass.h schedules display_common.glsl stages 0/1/8/2: 256-square meter, fixed-point partial/reduced histogram and retained exposure. Independent HDR/center/sky/emissive/percentile/adaptation fixtures verify actual GPU values. |
| renderNode_BloomAndTonemapping, postfx_bloom, postfx_common | Stages 3/4/5 perform nine-offset threshold/downsample and tent reconstruction in compact HDR, including source mantissa rounding. Six levels remain the default across presets. Independent constant/impulse/edge/color/HDR oracles cover the connected output. |
| renderColorGradingLUT, colorLutGenerate, include_colorGradingOperators | WorldDisplaySettings supplies range/lift/gamma/gain/offset/contrast/hue/saturation/brightness controls. Stage 6 caches the logarithmic 48-cube LUT; stage 7 applies it once. Full-volume neutral/non-neutral oracles and durable reference voxels qualify order and interpolation. |
| include_toneMappingACES, selected SDR environment defaults | display_aces.glsl implements the adjusted SDR RRT/ODT branch, independently evaluated from public primary/chromatic-adaptation/spline mathematics. Final presentation performs SDR encoding/dither. |

Both native backends use the same graph and math; resource barriers and
descriptors stay backend-owned. Opaque/sky/transparent content reaches display
after temporal feedback is saved. Reflection/temporal histories stay
scene-linear. Editor chrome and gameplay UI compose afterward.

The game sets an authored EV interval of 0.75..8 and retains environment exposure
as compensation. This keeps the normalized-radiance night scene dark while
allowing bright views to stop down; engine defaults remain the traced interval.
Field/battle supply simulation delta time, with pause and 60/120 Hz camera damping
qualified. There is no feature opt-in or preset budget cut.

Besides the storage adapters above, emissive metering uses actual shared-surface
emissive radiance rather than a packed EV channel. Default emissive weight is
one; weighted-emission fixtures verify the exposed control. Fixture radiance
units, game EV bounds, neutral grade and authored exposure are explicit scene
adapters, not recovered retail art settings.

## Evidence

- Both APIs pass 18 native cases: first/cut/history/retirement/independent views,
  odd resize, pause, 60/120 Hz damping, manual/partial-manual exposure, EV limits,
  outliers, actual HDR, sky/emissive weighting, center warp, bloom and grading.
  Backend validation reports no errors.
- Independent double-precision checks cover all 110,592 LUT voxels for neutral
  and non-neutral grades. Maximum error: 0.00097660 linear RGB, within half-float
  storage precision. Center-meter maximum: 0.00081258. Connected bloom/display
  maximum: 0.00048769. Native tests retain 14 neutral and 14 graded oracle voxels,
  a bloom-output oracle and state checks.
- GPU CTest registration follows PHLOSION_BUILD_GPU_TESTS; ordinary headless CPU
  CI does not start GPU programs.
- 22 runtime/editor/preset/transition/cycle cases pass, covering four presets,
  exploration/battle, orbit, daylight/night/rain and station/reference/station.
  Ten strict prepared-shader cases include fresh application caches and four
  full accelerated day/weather traces; driver-cache state is separate.
  Final locked integration additionally requires both APIs and hosts.
- Cross-API runtime image MAE is 0.166/0.905/0.845/0.094 out of 255 for
  Low/Medium/High/Ultra. Maximum >8 pixel-error ratio is 0.03351, within the
  existing 3.5/0.07 limits. Full daylight/rain/battle/orbit/editor images were
  reviewed. Existing dark night characters and dense foreground battle rain
  are not newly claimed character/weather art parity.
- Engine temporal/motion/profile/shader-preparation and game environment/
  wet-surface/exploration/arrival/battle/package CPU groups pass. The worker's
  missing rain-audio fixture was transferred before accepting the wet-surface
  group; its initial failure is retained.
- Readback is explicit diagnostics only. Ordinary exposure never reads brightness
  back to the CPU. Display texel payload at 1920x1080/six levels is 24,097,496 bytes
  (22.98 MiB) per retained view, excluding allocation alignment. LUT work runs
  only on grade changes. Resize and retired-view checks pass.
- Prepared HLSL release scene-shaders-v1/2026-11-03-display contains 78 programs,
  3,210,587 bytes including its manifest. Build-prepared Vulkan programs and
  editor ABI58/model ABI30 require matching providers. Shader hashes are
  unchanged by the final CPU-only 120 Hz damping correction.

## Matched GPU cost

EVGA RTX 2060 SUPER worker GPU1, 1920x1080 Medium, optimized RelWithDebInfo,
validation off. Each accepted run uses frames 121..239 (119 samples), valid
GPU totals and no dropped spans. Dry uses the same night/overcast camera with
rain intensity zero, not a cheaper daylight scene.

| API | Dry before | Dry current | Heavy before | Heavy current |
| --- | ---: | ---: | ---: | ---: |
| D3D12 | 11.985 ms | 11.440 ms | 15.624 ms | 14.567 ms |
| Vulkan | 12.568 ms | 11.719 ms | 15.721 ms | 15.126 ms |

Heavy D3D12 values average two run medians: before 15.540/15.708, current
13.996/15.138 ms. Vulkan uses one accepted control at 15.721 and two current
runs at 15.134/15.118 ms. Two other Vulkan controls recorded thermal slowdown
and are excluded. Accepted runs peaked at 69..73 C without sampled thermal
slowdown. These are workload GPU costs, not locked end-to-end 60 FPS or retail
performance claims.

Multiresolution bloom replaces the normal full-resolution 81-tap approximation.
Rain density, impact variety, lighting/shadow/fog budgets, geometry and textures
were not reduced. New display output is qualified against reference calculations,
not required to reproduce the previous approximation's pixels.

## Delivery and retained failures

All editing, builds, processing and GPU checks ran on gpu-worker; controller owns
review/integration/commits. Worker evidence: WORKER_EVIDENCE_ROOT/phlosion-display-20261103.
Controller evidence: PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/display-20261103.

Receipts: qualification.json, oracle-final.json, oracle-bloom.json,
performance.json, timing-rejected.json, handoff-manifest.json,
controller-package-install.json, integration-pin.json and integration-runtime.json.
The final two must pass for the committed engine/game heads before release.

The independent checks caught and fixed D3D12 depth-bucket addressing in the
sky meter and Vulkan delivering bloom strength after display. Earlier
fixture/oracle mistakes and the editor harness's PowerShell variable collision
remain in evidence; failed/superseded attempts are not counted as passes.
Alternate tone operators, cinematic lens artifacts, HDR monitor transport,
Linux qualification and retail image/FPS identity remain outside this fixed
ordinary SDR category. Other matrix categories are unchanged.
