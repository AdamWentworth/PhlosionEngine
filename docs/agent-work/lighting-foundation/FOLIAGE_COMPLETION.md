# Foliage completion contract — 2026-11-04

Status: 100% estimated coverage of the fixed F1-F8 ordinary Windows contract.
Final deployment acceptance additionally requires locked-qualification.json and
controller-installation.json; the source commit alone is not the release receipt.
The fixed F1–F8 scope is unchanged. Supported qualification is ordinary Windows
D3D12/Vulkan runtime/editor. Linux remains a release gate. A completed checklist
does not mean retail pixels, renderer-wide identity or all possible plant species.

| Gate | Connected behavior | Final evidence |
| --- | --- | --- |
| F1 | Authored color/alpha/surface mips, RGBA/AO, quantized profiles and gradient tint, normal basis/back-face policy and terrain blending | Asset/native/package checks |
| F2 | Directional/local/contact/GI and prepared reflections; foliage participates in local reflection capture | Native and ordinary material consumers |
| F3 | Shared/branch/ripple deformation; shared-only billboard wind; previous poses and local gust/shelter across consumers | Retained wind oracle, motion/camera/editor checks |
| F4 | Derivative/profile alpha, four-phase noise and complementary LOD coverage in native color/surface/shadow passes | Native alpha/coverage and transition checks |
| F5 | Both shrubs' fourth authored LOD, eight views, source color/normal/depth/roughness, camera-facing geometry and depth-correct material position | Numerical and zoomed distance/angle checks |
| F6 | Conservative wind/billboard bounds, independent view selection and Low/Medium/High/Ultra distance budgets | Private collection and ordinary tier checks |
| F7 | Near/far/moving/backlit/day/night/wet/field/battle/editor and independent native checks on both APIs | Final scene audit |
| F8 | Paired optimized costs, immutable source/game/prepared-shader releases and locked editor/runtime handoff | Final cost and release receipts |

## Implementation and explicit adapters

The source-derived vegetation catalog v2 restores the omitted atriplex billboard
atlases and fourth mesh chunks. Original-source and retained retail shrub meshes
match exactly for all four LODs. Five atlas sets retain source color/alpha and
transmission/depth mip bytes; gloss is converted to roughness. Vertex RGBA is
retained, with profile AO applied to mesh leaves. The existing normal/noise atlas
uses Y-convention conversion, resampling and sRGB compensation; it is a storage
adapter, not a claim of identical source texture sampling.

Profiles follow rounded UNORM8/packed UNORM4 representation. The 128-position
gradient follows linear interpolation, encoded byte truncation and hardware-style
lookup. Instance tint is prepared once using the source spatial hash; float
rounding/random selection is not promised bit-identical across engines.

The billboard retains its authored angular shading frame, rotates its geometry
for each actual camera/light/probe, performs only shared wind, and uses source
depth and roughness channels. The material point remains on the perspective ray.
Depth billboards use a near-depth raster proxy with unchanged clip X/Y/W and
exact reconstructed final depth. This preserves coverage/interpolation and allows
ordinary leaves/materials to retain early depth rejection. Original clip depth
feeds motion. Only the foliage-bearing material profile declares conservative
depth; separate rain programs retain ordinary raster depth. This is a Phlosion
adapter: source forward shading only displaces world position, whereas Phlosion
must agree with the corrected depth restored from its shared surface pass.
Motion retains previous view/deformation inputs; standard reactive coverage and
history rejection handle appearing/disappearing angular panels.

Both back-face policy and terrain blending are per species. Terrain inputs reuse
the immutable prepared ground grid, with dielectric specular 0.04 for this desert.
That is the foliage-to-ground adapter; it does not complete the separate terrain
multilayer/material category. Minimum roughness remains the engine's 0.04.
Primary forward/shared-surface/shadow coverage consistently uses the source
billboard profile threshold; the source's separate near-zero-cutoff forward
variant is not a separate mode here.

The game's 1,146 placements are unchanged. Mesh transitions begin/end at
20/26, 42/54 and 60/72 metres on Medium, scaled by .75/1/1.25/1.5 for the four
normal presets. The last authored representation remains visible without a
short-distance density cutoff. Ephedra retains three mesh LODs, grass one.
The source has no corresponding authored billboard for those two species.
Texture payload is 31,107,748 bytes, below the 32 MiB texture limit; the scene
scope reserves 40 MiB for collection data. No experimental feature switch.

## Evidence

Task root: WORKER_EVIDENCE_ROOT/phlosion-foliage-complete-20261104.
Controller handoff: PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/foliage-complete-20261104.
RelWithDebInfo, warnings as errors, EVGA RTX 2060 SUPER GPU1.

- Original-source versus retained shrub geometry: source-mesh-check/comparison.json.
- Exact authored texture mip and vertex-color checks: asset-oracle.json.
- Native leaf/profile/billboard/tint/terrain/off-axis position cases: 128 cases,
  1,024 returned channels per API. Independent analytic expectations.
- Retained source-wind oracle: 28,200 positions, max error 1.19209e-7 m.
- Six environment/foliage/rain/audio CPU tests pass after explicit private fixture
  staging. Missing initial fixtures are retained as failed evidence, not hidden.
- Production editor/runtime, preset/camera and performance gates are recorded
  separately from cold shader preparation. Initial D3D12 shader declaration and
  test-camera binding failures are retained; only corrected runs qualify.
- 28 current-binary gameplay consumer checks plus two editor scene returns pass: near, zoomed transitions and
  far billboard, angle/backlight/night, wet Low/Medium/High/Ultra, camera motion,
  battle and editor reference-scene return on both APIs. Earlier eight-angle
  geometry checks remain source-identical evidence. Live visible/shadow poses
  agree, motion readbacks are finite and no resource allocation failures occur.
- Native surface participation, leaf record/reflection-weight packing and both
  GI receiver/producer paths: 160 cases per API, alongside 128 leaf cases.
- Engine contracts, totals-only profiling contracts and six affected game CPU
  tests pass on the worker. 88 immutable prepared HLSL programs and
  the two v2 vegetation packages are published and pinned.
- Final locked rebuild and strict prepared-program runtime/editor/native checks
  must pass after adopting the canonical commits. Receipt: locked-qualification.json.
  Controller installation records those revisions; no local GPU test is claimed.

Source anchors and hashes: foliage-source-anchors.json. Research recipe and
interpretations: Cyberpunk2077Research/environment/FOLIAGE_MATERIALS.md.
Private source data and captured binaries are never runtime dependencies.

## Shared material production and composition

Primary leaves now export evaluated color, normal, roughness, transmission and
reflection-response weight once. The compact surface model uses the previously
unused albedo-alpha value 1; dielectric leaves repurpose metal/AO channels for
transmission/response weight, with AO already in color. Standard surface decoding,
local-light classification, GI and reflection consumers recognize this record.
There is no additional material render target. Other views retain qualified
forward evaluation and exact camera/environment admission.

Both APIs consume shared local lighting and prepared near GI. A reverse-normal
GI plane supports two-sided reflection requests without repeating the near-GI
search. The full-resolution RGBA16F receiver gains one plane (about 15.8 MiB at
1080p); this is an explicit memory tradeoff. Primary leaf geometry is not shaded
again after composition. D3D12 uses separate constant standard/leaf composition
programs with an early material-class rejection; Vulkan uses its qualified
branched composition. The FXC composition program remains selected after the
DXC comparison was slower. These are backend scheduling/compiler adapters,
not changes to the leaf equations, plant density or normal quality tiers.

Default leaf reflections keep their own response weight rather than inheriting
the ground's reflection tuning. Differently configured active responses stay
forward when their environment does not match the shared view. Leaf composition
does not accidentally apply the receiving ground's wet-material treatment.

## Qualified cost and delivery

Optimized RelWithDebInfo, warnings as errors, 1080p Medium, worker EVGA RTX 2060
SUPER GPU1. Ordinary before/current pairs use 119 warm frames (121-239), with
validation disabled. The first thermally valid matched-power pair is selected;
attempts/rejections are retained in ordinary-composition-report.json. These are
whole-frame measurements, not a retail FPS comparison.

| API | Weather / power | Before GPU | Completed foliage GPU | Delta |
| --- | --- | ---: | ---: | ---: |
| d3d12 | dry / 175 W | 13.699 ms | 11.561 ms | -2.138 ms |
| d3d12 | wet / 175 W | 18.108 ms | 16.032 ms | -2.077 ms |
| vulkan | dry / 175 W | 14.074 ms | 12.726 ms | -1.348 ms |
| vulkan | wet / 175 W | 18.059 ms | 16.876 ms | -1.183 ms |

The separate fixed-close-camera gate uses the unchanged 1,146 placements:
1,000 frames, samples 300-899, +1.5 ms GPU / +2 ms CPU limits and a 50 ms
maximum warm-frame limit. Both off/on runs use a controlled 125 W limit;
175 W is restored afterward. Full-power attempts triggered thermal slowdown
and remain rejected evidence. No driver, fan or clock settings were changed.

| API | Collection GPU delta | Collection CPU delta | Maximum warm CPU / GPU |
| --- | ---: | ---: | ---: |
| d3d12 | +1.282 ms | +1.297 ms | 36.655 / 13.093 ms |
| vulkan | +1.445 ms | +1.470 ms | 15.300 / 13.714 ms |

Both APIs pass these retained gates. Earlier attempts are preserved, including
the D3D12 CPU stalls with synchronous diagnostic writes enabled. Totals-only
profiling now omits those detailed writes inside timed frames; aggregate
CPU/GPU timings remain buffered and are written at shutdown. Detailed profiling
retains the diagnostic output. This measurement change is not counted as a
normal-game rendering speedup. The independent profile contract verifies that
total timing survives without detailed spans.

Exact source/payload identities and evidence paths: final-foliage-evidence.json.
The source commit is followed by locked dependency rebuilds, native tests and
strict prepared-program runtime/editor checks on both APIs. Qualified artifacts
and all three matching publications are installed together on the controller.
Private source/capture data remain authoring inputs, never launch dependencies.

## Historical first increment

The following was the previously qualified leaf-only checkpoint, before this
profile/billboard/consumer work. Its open-item notes and costs are historical.

## Qualified leaf-material increment

The engine now evaluates the finite sun-disk representative direction, Disney
diffuse, height-correlated GGX/Schlick and roughness-dependent leaf transmission.
Directional visibility multiplies both response terms. The game supplies its
prepared albedo/normal/transmission/converted-roughness maps and consumes the
existing local-light, contact, indirect, haze and prepared reflection services.
Back faces flip the geometric normal basis only; tangent/bitangent and signed
normal-map Z remain intact. Removed the authored 0.65 normal-detail attenuation,
0.45–0.95 roughness clamp, 0.16 backlight scale and power-lobe sun highlight.
The shared surface record and visible shader use the same evaluated normal and
roughness. No extra render pass, instance, source draw or wind deformation.

The ordinary consumer takes the active sun/moon angular radius from the same
sky uniforms as the visible disks. Native fixtures cover zero, average-sun,
authored sun and authored moon radii.
Adapters retained: engine minimum material roughness 0.04, existing normal/noise atlas and authored
coverage-preserving mip package. Source profiles, color/AO/terrain-blend inputs,
temporal alpha policy, additional consumer variants and billboard integration
remain open. Local illumination intentionally follows the standard workflow:
the source's directional transmission lobe is not added to every lamp.

### Verification

gpu-worker Windows, RelWithDebInfo, warnings-as-errors, EVGA RTX 2060 SUPER
LUID 000000000000c1c7. Evidence root:
WORKER_EVIDENCE_ROOT/phlosion-foliage-20261104.

- PhlosionFoliageMaterialTests: 32 independent analytic cases / 256 returned
  channels per API. Front/back, handedness, negative normal Z, roughness,
  transmission and finite sun disk; bounded float32 error against independent double-precision expectations.
- Existing source-wind oracle: 28,200 positions, maximum error 1.19209e-7 m.
  Six affected environment/rain/package CPU checks pass. An initial run lacked
  the private audio-root binding; rerunning with the existing prepared fixture
  passes without changing code or assertions.
- 18 first-revision scene runs, followed by 12 final celestial-input runs: both APIs, baseline/current close-ups, editor local-wind
  contact, heavy rain, battle, Low/Medium/High/Ultra and camera motion.
  API validation has no errors. Runtime checks use 1920x1080; the editor's embedded game viewport is 832x468. Close images reviewed on both APIs.
- Prepared shader publication and final locked runtime/editor checks are
  required before deployment. Their immutable receipts identify final commits.

### Paired cost

1080p Medium, validation off, 240-frame runs; 119 warm frames (121–239).
Median GPU milliseconds from accepted non-throttled samples. Three wet attempts recorded a thermal flag and were rejected. Both wet pairs were rerun. The table selects the first non-throttled attempt per API/weather/version, never the fastest; all accepted/rejected attempts and the selection rule are retained. These are paired samples,
not a new retail comparison or a general performance improvement claim.

| API | Weather | Before | Leaf material | Delta |
| --- | --- | ---: | ---: | ---: |
| d3d12 | dry | 13.255 | 13.813 | +0.558 |
| d3d12 | wet | 17.653 | 18.229 | +0.576 |
| vulkan | dry | 13.601 | 14.068 | +0.467 |
| vulkan | wet | 17.768 | 18.228 | +0.460 |

### Next connected work

The atriplex_bigest_b source has four LOD chunks (1,880 / 1,095 / 192 / 48
vertices), with the fourth billboard chunk containing 96 indices and beginning
at authored 60 m. The published collection retained only the first three.
The inspected extracted texture sets contain no matching billboard textures.
Restore the billboard material/atlas/depth inputs and implement its normal,
depth, roughness, orientation, shared-wind-only deformation and temporal/shadow
consumers together. Do not substitute rotating copies of the current leaf mesh
or declare F5 complete because the existing mesh remains visible at distance.
Research publication work must preserve the unrelated dirty research tree.
