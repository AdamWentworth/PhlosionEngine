# Shared wet-surface reflections

Status: five shared-puddle gates qualified on the supported Windows paths; broader wet-weather category remains incomplete.

## Acceptance fixed before GPU qualification

1. Authored puddle coverage, height/slope rejection and impact queries agree.
   Disabled/dry materials cannot retain water; field and battle share the producer.
2. Actual ground base color, wet normal, roughness and dielectric response enter
   the evaluated surface buffer. Preserve geometry identity, motion and depth.
3. Ordinary prepared SSR uses those attributes and N-1 scene color; placed probes
   provide the existing fallback. Dry, covered and wrong-height receivers reject
   puddles. No full-scene mirrored capture or separately lit transparent ground
   puddle is submitted by normal field/battle presentation.
4. Both Windows APIs and hosts retain their normal four presets, transitions,
   Hybrid surface-specific splashes and real-puddle rings. Changed shader and
   editor ABI inputs must be prepared/staged consistently.
5. Review images and record matched optimized timings separately from correctness;
   no quality-budget cut, local controller GPU test or unmeasured retail-FPS claim.

## Scope and reference

The supplied Mainline source's postfx_screenSpaceRain.fx:132-184 changes evaluated
base color, roughness and normal before lighting. include/include_rain.fx:210-258
shapes puddle material response; renderNode_ScreenSpaceReflections.cpp:422-544
uses receiver depth and existing scene depth/N-1 image feedback for its special
planar route rather than a mirrored full-scene draw. These are architectural
references, not proof of the current retail version or its measured frame cost.

This pass uses Phlosion's existing ordinary surface SSR and placed probes.
Authored analytical footprints replace the previous puddle mask quads. They are
a bounded input adapter, not the complete source rain-map/top-down vehicle map,
texture-driven puddle/porosity, leaking or flow simulation. The current wet/dry
curves and micro-ripples remain Phlosion's own treatment. This does not complete
the whole wet-surfaces category or the source special-planar algorithm.

Ordinary opaque receivers already carried mapped materials; adding puddles to
that producer removes their old alpha-blended redraw and the scene mirror used
only by those quads. Transparent/deep-water and legacy planar engine APIs remain
separate. The authored shallow water normal/roughness now controls screen rays,
shared direct/indirect lighting and reflection response consistently.

## Implementation and evidence

- Engine WorldPuddle.h defines the bounded 16-region receiving-ground mask;
  WorldEnvironmentPacketCache prepares it once with view lighting.
- D3D12 appends the packet to b4 without moving previous fields; Vulkan appends
  it to WorldViewState. The editor plugin ABI is 51.
- world_atmosphere.glsl modifies the actual ground material before surface
  export. The geometric slope gate is independent of texture normals; ground
  albedo remains visible, while shallow water receives the existing ripple
  normal, smoothness and dielectric response.
- Field and battle publish the same accepted regions to material evaluation and
  rain impacts. Their separate mirrored scene and alpha-blended puddle draw are
  removed. Generic engine planar support remains for other consumers.
- Existing authored masks, shelter and drying remain explicit adapters. CPU
  mask/impact tests and native GPU tests do not claim source texture/flow parity.

Optimized CPU contracts pass: environment packet/cache, engine suite, contact
visibility, indirect lighting, and six game contracts including weather surfaces,
profile, environment, exploration and battle. The original game-contract attempt
caught the expected ABI assertion and a missing test audio path; corrected checks
use ABI 51 and the already prepared worker audio assets.

Actual native GPU material/SSR tests pass with validation on both D3D12 and
Vulkan. Each exports 40,320 wet receiver pixels and 20,919 strongly reflected red
receiver pixels; wrong-height, dry, disabled and sheltered controls have zero
screen hits. Existing history, tier, exposure, missing-motion, camera cut,
roughness and odd-size numerical tests pass alongside them.

## Qualified result - 2026-11-01

The five bounded shared-puddle acceptance gates above pass. This closes the
duplicate mirrored-scene/transparent-puddle submission gap, not the entire
wet-weather category. No rain, shadow, reflection or preset budget was reduced.

20 strict normal-host checks pass: runtime and editor, four presets on both
Windows APIs, two additional wet Low probe-only cases and two station/reference/
station transitions. All 62 prepared HLSL programs are published and verified;
strict launches perform no runtime HLSL compilation. Native numerical/material
tests, CPU contracts and ten reviewed rendered images accompany these checks.
ABI 51 keeps the public environment layout consistent across engine and plugins.

All timings below are GPU milliseconds on gpu-worker's EVGA RTX 2060 SUPER,
1920x1080 Medium, optimized RelWithDebInfo, matched night/overcast/camera/time.
There are 239 accepted frames per run, two wet repeats per revision/API (ABBA)
and one dry control per revision/API. The table uses pooled medians. Validation
and nested GPU timestamps are disabled for totals; pass diagnostics are separate.

| API | Dry before | Dry after | Heavy rain before | Heavy rain after | Rain overhead before → after |
|---|---:|---:|---:|---:|---:|
| d3d12 | 13.777 | 13.356 | 19.206 | 16.303 | 5.429 → 2.947 |
| vulkan | 14.033 | 14.047 | 20.647 | 16.864 | 6.614 → 2.818 |

d3d12: wet GPU reduction 2.903 ms (15.1%). Wet per-run medians: [19.486048, 19.054848] before; [16.306528, 16.297984] after. Host whole-frame medians: [19.0342, 19.0326] before; [15.9471, 15.8667] after.

vulkan: wet GPU reduction 3.783 ms (18.3%). Wet per-run medians: [20.611872, 20.662496] before; [16.860096, 16.865088] after. Host whole-frame medians: [20.844, 20.7836] before; [16.9287, 16.9933] after.

Accepted telemetry peaks at 66 C.
2 telemetry-flagged timing attempt(s) are excluded and retained in
the evidence. Dynamic clocks and the small repeat count limit precision; this
is a matched scene comparison, not a guarantee for every view/preset or retail
Cyberpunk performance parity. Neither API submits the old field-view/reflections
or field-view/surfaces passes in the accepted separate GPU diagnostics.

The observed image change is intentional: shallow water now modifies the ground
material and uses ordinary motion-aware SSR plus placed probes, instead of
alpha-blending a separately lit mirror overlay. Irregular water, colored lights,
character reflection, rain density, surface-specific splashes and real-puddle
rings remain. Low retains probe reflections with SSR disabled; dry weather removes
water. Screenshot/orbit endpoints and numerical history tests were reviewed;
a manual free-roaming play session is not claimed.

Remaining wet-system work: source rain-map/porosity and texture-mask producers,
leaking/flow, full wet/dry material transitions and broader metallic/deep-water
cases. Existing authored placement, thin-film response and micro-ripples remain
explicit adapters. The source's separate planar receiver/tracing variant has
not been ported; the existing ordinary SSR/probe route handles these ground patches.

Evidence: artifacts/lighting-foundation/wet-reflections-20261101/RESULTS.txt,
performance-analysis.json, visual-review.json, source-evidence.json and evidence.zip.
Raw evidence preserves corrected fixture/asset setup failures and excluded timing
attempts. Worker mounted research assets were left untouched; shader restoration
used the unchanged selected shader-package entry and all normal hash/path checks.
