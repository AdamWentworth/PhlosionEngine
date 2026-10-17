# Static diffuse transport, live fixture colors

Functionally verified, 2026-10-17; performance limits are recorded below.
This is a bounded single-bounce implementation, not
a Cyberpunk shader port or a numerical parity claim. The retained retail capture
establishes probe resources and composition, but not its complete bake/update
algorithm. The previous shadow/HDR/culling evidence remains the baseline.

The engine bakes static glTF geometry into a small regular irradiance probe grid.
Deterministic rays hit actual triangles, sample linear material reflectance, and
test visibility to samples on each one-sided rectangle. The bake stores separate
unit-white transport coefficients for each source. Runtime mixes them with the
current linear source radiance, so switching off every source produces exactly
zero indirect light. Position, area, range and scene identity changes invalidate
the bake; color/intensity changes do not. No camera-dependent fill lights.

Nine real spherical harmonics coefficients retain directional diffuse response.
The cosine convolution is applied once, and receiving materials apply their
diffuse BRDF once. Metals receive no added diffuse term. Interpolation includes
static visibility and bounded volume fade; shadowed and wall-separated controls
must expose leaking rather than conceal it with extra intensity. Static materials
and geometry determine transport. Moving actors receive it but do not update it;
wetness does not rebake it, and additional bounces, sunlight/point-light transport,
glossy scene reflections and real-time geometry updates are outside this pass.

Probe upload is per view and resets between views. Disabled consumers retain
their previous output. Generic math, bake and renderer code live in the engine;
the game's volume and source selection live in PokemonCyberpunk. The canonical
Blender asset is unchanged. Before-state copies are under
`artifacts/lighting-foundation/bounce/before`.

Acceptance: CPU analytic/source-off/additivity/material/visibility/validation
contracts; native pink, blue, combined, off and bounce-disabled controls;
OpenGL/Vulkan/D3D12 runtime/editor station and battle; scene switching and normal
launch; affected Autochess material smoke; measured incremental GPU/host cost.
Do not relax earlier thresholds or claim completion before native checks.

Math reference: Ramamoorthi and Hanrahan, An Efficient Representation for
Irradiance Environment Maps, https://graphics.stanford.edu/papers/envmap/envmap.pdf.
Directional distance moments follow the visibility-weighting idea described in
https://arxiv.org/abs/2009.10796, with a deliberately smaller static scope here.

## Implementation and current evidence

Engine `WorldDiffuseProbes` owns the optional view interface, SH packing,
interpolation and cache validation. `GltfDiffuseProbeBake` uses a CPU triangle
BVH with deterministic sphere rays and rectangle area samples. It includes
opaque and masked static geometry, with two-sided normals matching world draws;
blended and skinned geometry do not enter the bake. Unlit surfaces block but do
not reflect diffuse energy. Base-color textures are decoded before bilinear
filtering; metallic reflectance contributes no diffuse. This geometric-normal
approximation does not reproduce normal-map microdetail. Six directional distance
moment bins suppress interpolation through blockers, with a normalization floor.
This coarse visibility is not exact per-pixel indirect occlusion or DDGI.

The game volume has 4x2x4 probes, covering X=-24..-8, Y=.45..3.75,
Z=-34..-14 with a 3m exterior fade. Each probe samples 2048 directions and
each of six canopy rectangles uses 32x2 area samples. All 32 probes are valid.
The offline basis excludes point lights, sun, emissive signage and distant terrain
transport. Live canopy radiance includes the existing day/condition gains.
`Prepare-Outskirt-Bounce.ps1` writes private cache data beside station.glb;
normal launch prepares it after builds. The generic bake executable takes a
glTF scene, a JSON request and a new output path. Moved lights or changed scene
identity fail explicitly; changing fixture colors does not require a bake.

The packet is 483 vec4 / 7728 bytes. OpenGL uploads on packet/program change.
Vulkan includes it in the existing cached per-view UBO (10864 bytes, below the
16KB minimum limit). D3D12 uses a separate fenced per-frame upload slice, up to
64 distinct packets. Adding its CBV exposed the previous 64-DWORD root limit;
the 56 material root constants now use a 224-byte CBV, with separate 256-byte
main/outline slots in the existing per-draw ring. No 7.7KB per-draw duplication.
The first failed D3D12 editor launch fell back to OpenGL and was rejected on
inspection. The launch probe now checks actual API/host before reporting PASS.

Four engine CPU contracts and five game contracts pass. The public irradiance
matrix has 28 native captures: four modes on all APIs/hosts and four additional
D3D12 controls. The uniform diffuse receiver matches the independent irradiance,
Lambert, exposure, ACES and sRGB calculation (both RGB 110.865/28.313/125.482,
observed median 111/28/125). Maximum backend difference is one byte. Metals,
zero sources, blocked visibility and volume exclusion pass. Station, wet/battle
and source controls also pass. Scene switching passes on all three APIs, five
visits each: the station restores 32 probes/six sources and the Colosseum reference
clears them. The final normal D3D12 editor probe passes with the station, 120s day
cycle and 180s Hybrid/CP-audio weather setup. Staged DLL hashes match the rebuilt
game outputs. Evidence: game
`artifacts/lighting-foundation/bounce/rtx2060/public-verification.json`.

The RTX 2060 SUPER run contains 64 scene captures: public 28, dry under-canopy
station/editor plus bounce-off 9, isolated source/bounce controls 8, wet station
6, rainy battle 6 and existing lighting/weather controls 7. Existing thresholds
are unchanged. Maximum dry station API MAE is .56644/255; wet station 1.31433/255;
battle .62487/255. Wet runtime/editor MAE is at most .00001713/255. No allocation
failures or API fallbacks. The existing front/oblique emitter-reflection, wet/dry,
puddle and localized-bloom oracles pass. All-off bounce/on-vs-off is pixel-exact;
pink and blue each add visible indirect light. Nine native HDR ramp/bloom/feedback
tests pass as well. Reviewed dry bounce on/off and wet station images.

The Autochess standalone target was explicitly rebuilt after the paired
editor/plugin build (that helper does not link the game executable). Its two
character cohorts pass on all three APIs with the existing content and parity
limits. The two editor body/eyes/stage cases also pass on all three APIs. Five
editor images are pixel-identical to the earlier HDR baseline; the sixth differs
by one byte in one channel. Reviewed the D3D12 runtime and body/eyes preview.
An initial run against the older executable was rejected and retained
separately as `runtime/stale-binary-captures`; it is not acceptance evidence.

## Incremental performance, RTX 2060 SUPER

Cyberpunk was closed for these sequential 1920x1080 clear-night under-canopy
runtime runs. Both controls retain HDR, bloom, direct lights and all shadows;
only diffuse bounce is switched. Every API has 200 valid warm GPU/host samples
(frames/ticks 30 through 229). Scene, camera, sources, clock and wetness match.
These compare enabled/disabled execution of the current shader, not the total
cost of its compiled code versus a previous binary.

| API | GPU off/on ms | Host off/on ms | Added GPU ms |
| --- | ---: | ---: | ---: |
| OpenGL | 26.336 / 27.602 | 28.636 / 29.841 | 1.266 |
| Vulkan | 14.666 / 14.970 | 49.956 / 48.739 | .304 |
| D3D12 | 10.363 / 11.931 | 10.689 / 11.939 | 1.569 |

These are medians. Existing review limits are +1.5ms GPU / +2ms host. The first
D3D12 sample exceeds the GPU guide by .069ms. The reverse-order repeat measures
GPU 10.474 / 11.683ms and host 10.773 / 11.962ms (off/on), or +1.209ms GPU and
+1.189ms host. Retain both results: the observed D3D12 incremental range is
1.21-1.57ms, near the guide, rather than an unconditional budget pass. The first
flag remains in `performance-verification.json`; the repeat is under
`performance-repeat/`. No threshold was changed. Negative Vulkan host delta is
sampling variation, not an optimization.
GPU p99 with bounce is 38.158 / 18.593 / 13.017ms; host p99 is
40.988 / 72.596 / 26.050ms (GL/Vulkan/D3D12). This is not a sustained 60 FPS
qualification; GL throughput, Vulkan host cost and frame spikes remain concerns.
Hardware identity and 1s utilization/clock samples are retained with the runs;
GPU 0 rendered them and GPU 1 remained idle. No pre-swap timings are compared.

## Vulkan startup and new hardware

The machine changed from GTX 1070 to two RTX 2060 SUPER cards during this pass.
Do not compare their frame timings as an optimization result. The fresh hardware
evidence is under game `artifacts/lighting-foundation/bounce/rtx2060`.
Its 28 public captures pass the same analytic/color and parity limits.

Vulkan now shares one pipeline cache across related blend/depth variants and
swapchain/material rebuilds. At shutdown it stores a bounded cache under the
existing data-root `cache/shaders/vulkan` convention. Loads check a versioned
envelope, payload length/checksum, native Vulkan header, vendor/device/driver and
pipeline-cache UUID before passing bytes to the driver. Failed native cache loads
retry with an empty cache. Stores use a temporary file and atomic replacement;
cache failures are optional. The CPU contract rejects truncation, corruption and
incompatible identities. `PHLOSION_DISABLE_VULKAN_PIPELINE_CACHE=1` disables disk
load/store while retaining per-instance pipeline reuse.

The first new-hardware material launch was slow (166s editor with in-memory
reuse, following a competing-game run that is not a valid timing baseline).
After disk-cache integration, the cold-file and warm-file editor logs span 31.8s
and 34.0s respectively; the complete warm launch script took 36.9s. These are
observations, not an isolated attribution of the improvement to disk caching:
the driver cache was also warming. The successful cache contains ~10.4MiB.
First compilation after shader/GPU/driver changes can still be expensive.
Fresh custom-material Autochess captures also encountered long first compiles,
including roughly four minutes for the first D3D12 runtime capture. All six
runtime and six editor captures completed; disk caches do not eliminate the
initial compilation cost.

The canonical Blender SHA256 remains
`6015f9de3eb4228c1310f86462095051e9ebdc1d5236cfe8e1ae4df7b0d71918`,
and station.glb remains
`6a1c0e493007eced0bef5058dae033b06fd55ba05fd450f48294145630e5a007`.
The before-state audit retains 422 files across four repositories: 387 are
byte-identical and 35 are intentional overlaps in engine/game code and docs.
There are 14 new source/documentation files. No PokemonModelSupport or Autochess
source changed in this pass. See `preservation.json`, `current-pass-files.json`
and `current-pass.diff`. The user authorized checkpoint commits on 2026-10-17.
