# Live finite-source lighting

2026-10-17. The independently recovered local-light response is integrated into
the shared surface shader. PokemonCyberpunk opts its six canopy fixtures into
this response. This is direct-light component integration, not a claim that
Phlosion's whole lighting engine matches Cyberpunk 2077.

## Source and material contract

`WorldRectLight::response` selects `Ltc` (the engine default) or `FiniteLine`.
Both describe the same one-sided physical rectangle: perpendicular world-space
half axes, their cross product pointing out of the luminous face, scene-linear
radiance, and a positive range. The response tag occupies previously reserved
`halfU.w`; the GPU packet remains 64 float4s. The C++ layout changes, so model
plugin ABI is 18 and editor-project ABI is 38. Rebuild every consumer/plugin.

The game selects `FiniteLine` without changing the source assets, dimensions,
colors or radiance. `PHLOSION_USE_LTC_RECT_LIGHTS=1` selects the previous response.
The verification helpers expose it as `-UseLtcRectLights` and restore the process
environment afterward.

The rectangle adapter uses an inscribed rounded line: radius is the shorter half
extent; half-length is the longer half extent minus that radius. A square maps
to a sphere. Normal intensity is rectangle area times authored radiance, with
one-sided angular emission toward the nearest point on the line. Distance and
attenuation then use the recovered finite-source calculation. This is an
explicit Phlosion source mapping, not a recovered Cyberpunk authoring convention.
The two representations are approximations and need not produce identical images.

PBR materials supply their actual normal, view, albedo, roughness, metallic and
F0. The adapter preserves custom dielectric reflectance, including water, instead
of forcing the captured decoder's 0.04 dielectric value. Layered character
materials receive diffuse local light while retaining their own specular model.
Roughness is bounded to [0.04,1]. Zero sources, source-plane/back-side receivers,
coincident positions and singular representative-ray geometry have defined finite
responses. Parallel line/ray geometry uses the nearest segment point.

The existing physical rectangle still drives depth-PCF shadows, static diffuse
transport and haze. Reflected geometry runs the same surface response. The game
continues suppressing reflected canopy emission already represented by direct
source specular; signs and screens retain their reflected emission. This pass
does not port moment-map generation, change rain, or add another render pass.

## Numerical evidence

The private matched replay was repeated with the production singularity guards:
`evidence/lighting-matched/gpu/local-production-guards-05` under
`D:/Projects/Research/CyberpunkRainResearch`. All 873,728 active pixels are included.
Aggregate relative L1 error is 0.000065974% diffuse and 0.000029806% specular.
Exactly 2,620,706 / 2,621,184 diffuse channels and 2,620,926 / 2,621,184 specular
channels agree. Four diffuse and two specular shadow-edge outliers remain above
2%, with maxima 28.69% / 36.58%. No pixels are excluded.

The restored original dispatch is bit-exact. Later composition changes 488
half-float channels, by at most two ULPs, within the previously measured ordinary
replay envelope. The strict preservation report still says `requires-review`;
the separate qualification says `qualified-with-numerical-limits`. All 122
original exported files retain their hashes. See [the original GPU
qualification](GPU_LOCAL_RESPONSE.md) for the method and branch coverage.

Public native tests now have 16 independent float64 reference cases plus 16
production/domain cases on each API. They cover area/intensity mapping, source
off/back-side rejection, rotation, linear energy scaling, authored F0, the
roughness floor, metallic response and degenerate geometry. All pass; maximum
reference error remains GL 1.15973e-6 and Vulkan/D3D12 7.47621e-7.

## Live verification

Evidence is retained under `artifacts/lighting-foundation/finite-source` in the
engine, game and Autochess repositories. Build logs retain the initial strict
dependency rejection during development. Final qualification must restore the
strict dependency lock after committing the intended engine revision.

The engine's 129 affected contracts/native checks pass, including local shadows,
diffuse probes, haze, planar reflections, HDR and screen feedback. Six game CPU
contracts pass: outdoor profile, world environment, station layout, exploration,
weather surfaces and weather acoustics.

The six wet station and six rainy battle captures pass on all three APIs in
runtime and editor, with unchanged image/content limits. Maximum station API
MAE is 1.06745/255, maximum runtime/editor MAE is 0.00001285/255, and maximum
battle API/host MAE is 0.56510/255. The seven D3D12 day/dry/off/oblique controls
pass the independent source-reflection footprint, puddle and bloom oracles.
Pink/blue reflected-footprint median changes are 48/96 display levels in front
and 54/98 at the oblique view; 39,012 ground pixels visibly respond to puddles.
These are display-image content measures, not physical-energy or retail parity.

The pre-change and new lights-off images are pixel-identical. The matched LTC
and finite-source images differ by 2.60816/255 mean display RGB, with 110,386
pixels differing by more than three levels. This measures the deliberate response
change, not its quality. Seven additional reflection controls pass wet/front,
oblique and reflected-player content, unchanged sky, and exact dry/legacy gates.
Four isolated off/pink/blue/both captures retain both colors on the character:
1,143 of the same 2,408 reviewed interior pixels respond independently to each
color, with 2,408 responding to blue. As in the earlier emission review, these
thresholded display observations do not measure linear radiance addition.

Cold first-use scene captures take minutes after this shared shader change;
Vulkan runtime/editor each took about four minutes. Those are end-to-end capture
durations, not isolated shader compiler measurements. A staging invocation
mistakenly used `-SkipOpening` as a no-launch option; it actually skips the story
opening, so the owned editor was closed and its failure log retained. Use
`Launch-Editor.ps1 -SkipBuild -Probe` for staging plus a bounded hidden launch.
All three scene-switch probes pass five station/reference visits each, including
resource stabilization. The normal D3D12 launch passes with Hybrid rain, the
existing day/weather cycles, original Cyberpunk audio and finite-response tags
on all six sources. That warm six-frame probe takes approximately 30 seconds
end to end. The three staged game/model DLL hashes match their rebuilt files.

Autochess passes six runtime cohort images and six editor body/eyes and stage
images, including the accepted material baseline and unchanged content/parity
limits. Its editor/plugin pair verifies ABI 38. Neither Autochess nor
PokemonModelSupport has source edits. The first runtime image verification used
PowerShell 7, whose missing System.Drawing reference prevented the legacy C#
analysis modules from loading. Its failed log/report are retained; the same
images pass the unchanged verifier in Windows PowerShell 5.1. The first Vulkan
editor capture hits the 240-second cold-start bound. A retained artifact-only
copy of the same harness allows 600 seconds, resolves its original tools path,
and passes all six editor cases. Image/content limits are unchanged. These cold
start limitations remain; a warm-frame result does not resolve them.

## Warm cost and acceptance

2026-10-18, RTX 2060 SUPER / driver 617.42. Six native runs are sequential, after
builds and other native jobs finish. Each uses the same 1920x1080 wet-night view,
heavy Hybrid rain, shadows, bounce, reflections, haze and bloom, with 200 warm
samples [30,230). The complete recorded state matches between modes after
normalizing only the six response tags. All allocation/error guards pass.

| API | GPU median LTC / finite ms | Host median LTC / finite ms | Finite GPU p99 ms |
| --- | ---: | ---: | ---: |
| OpenGL | 46.147 / 42.882 | 47.911 / 45.084 | 62.261 |
| Vulkan | 22.999 / 21.144 | 55.344 / 54.717 | 26.687 |
| D3D12 | 23.094 / 20.736 | 23.623 / 21.396 | 27.653 |

These paired full-frame observations show lower GPU medians in this review;
they are not isolated BRDF timers, repeated statistical benchmarks, or a 60 FPS
claim. Vulkan remains CPU-limited, GL remains costly, and cold startup remains
slow. Raw frames, matching state, logs and `performance.json` are retained in the
game artifact root. The core hash is byte-identical to the qualified private
replay snapshot (`qualified-core-hash.json` in engine artifacts).

The live integration is accepted with these declared limits. Shared source
changes are committed and the consuming game pins that revision before strict
dependency qualification. The normal project uses the finite-source response;
the LTC review override remains available. No private capture data, shader
binaries or extracted assets are included in source commits.

## Remaining parity targets

The captured distance-moment filter is now independently matched; see
[SHADOW_MOMENTS.md](SHADOW_MOMENTS.md). Next integrate it with engine-owned targets,
compatible radial shadow projection/receivers and qualified texel addressing.
The reproduced two-moment visibility function alone does not make the live
depth-PCF shadows equivalent. Material decoding, other emitter permutations,
reflection transport, indirect probes, fog transport, exposure and display each
need matched-input evidence. The retained retail capture has RT disabled; none
of these results establishes ray-traced or path-traced parity.
