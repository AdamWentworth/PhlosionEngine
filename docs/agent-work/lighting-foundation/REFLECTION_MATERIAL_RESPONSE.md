# Reflection material response

2026-10-20. Scene reflections previously inherited the neutral preview's `.44`
specular scale and `.16` material roughness floor. This pass separates that
preview behavior from the outdoor lighting profile. It follows the receiver
visibility work in [REFLECTION_RECEIVERS.md](REFLECTION_RECEIVERS.md).

## Production change

Outdoor ordinary and built-in PBR paths now apply unit scale to scene-linear
reflection radiance and accept reflection roughness down to `.04`. Vulkan and
D3D12 share the profile decision. The composed material sources use the same
helpers as the built-in paths. Authored material reflection multipliers still
apply. The neutral inspection profile retains its previous scale and floor.

Direct light retains its established `.16` floor. Review caught that lowering
the material floor globally also changes existing direct highlights; the older
point/directional GGX denominator guard is unsuitable for a newly extended
polished-surface domain. This reflection pass therefore supplies direct lighting
with its previous effective roughness. Improving very smooth direct highlights
needs a separate, filtered-source qualification. Four additional native direct
controls check the preserved `.04`/`.08`/`.16` response against a lit `.42` patch.

The existing wet-ground roughness floor is now bounded by dry roughness. Wetting
can no longer turn an already polished `.04` surface into a rougher `.16`
surface. Rough ground retains its existing microstructure approximation; explicit
water draws retain their separate response. Light intensities, exposure, station
assets, authored materials and the rendering ABI are unchanged.

This removes an inherited artistic attenuation; it does not introduce the
material-specific controls recovered from Cyberpunk's deferred shader. Those
controls depend on decoded material fields and weighting. They must not be
replaced by a universal brightness boost.

## Independent native check

`PhlosionReflectionResponseTests` captures a closed, unit-emission enclosure.
Constant incoming radiance removes coverage, source silhouette and filtering as
variables. A 4x4 receiver array covers F0 `.04`, `.5` and `1`, ordinary dielectric
and metal response, and authored roughness `.04`, `.08`, `.16` and `.42`. Both
ordinary and built-in PBR paths are checked at normal and grazing views. Wet
cases rotate the fixture onto a horizontal floor with full surface wetting.

The CPU oracle independently samples the licensed GGX moments and evaluates the
single-scattering response, tone mapping and display encoding. The preselected
limit is less than 2/255 at the sampled patch centers. This is a material-response
check, not a complete physically based rendering or retail-frame comparison.

The eight original dry cases fail against unit response, with worst error
59.8049/255. They instead match the previous `.44`/`.16` response within
.844842/255. After the correction, all sixteen dry/wet cases pass; worst error
is .68107/255. The four direct-light preservation cases also pass within 1/255.
All ten response-fixture API image pairs differ by at most 1/255. Final source
also models the old wet roughness in its diagnostic `legacy-error` column; the
unit oracle remains the acceptance criterion for reflection cases.

All 223 final selected engine checks pass, including 171 GPU cases and 52 CPU checks.
Graphics validation is clean. The separate strict reflection API-image gate
still fails one of 66 pairs: `handoff6` has a maximum difference of 26/255 in
its retained edge outlier. The other 65 pairs satisfy the original 1/255 gate.
Camera movement, source handoff and thin-source position checks retain their
original thresholds and pass. No acceptance threshold was widened.

## Relation to the retail study

The research repository's `lighting/reflections/analyze_engine_response.py`
compares these two scale/floor settings against the retained retail DFG table
and recovered class-0/subtype-0 controls. Each supplied-weight case spans 131,841
angle/roughness samples with unit radiance, F90=1 and open visibility. All twelve
F0/weight cases improve in mean error. For F0=.04, mean linear-response error
falls from .0533871 to .0003478 at zero control weight, and from .1308167 to
.0773252 at weight one. The latter still has a .556936 worst sample: the retail
material adjustments remain substantial at some grazing angles.

Those weights are supplied scenarios, not decoded pixels. Neither these errors
nor the native fixture establish a whole-lighting parity percentage. The engine
uses its existing licensed table; no captured retail shader or table is shipped.

## Integration evidence

Consumer visual qualification is recorded separately under the same artifact
name in each game. Both game builds and their two Cyberpunk/four Autochess CPU
contracts pass. The engine editor and both standalone games embed the current
generated lighting source, and source/binary manifests retain the tested inputs.
The initial trial's consumer captures were superseded by the direct-light
preservation correction. Final qualification uses the `final/` subfolder.
The normal editor launch, eight wet field/battle host/API captures, 28 close
pump/bar, noon/sunset and moving-camera images, and both normal scene-switch
sequences pass. The maximum close-review cross-API mean difference is
.332265/255. The matched before/after images retain identical scene, animation
and weather state; broad station views show a modest, localized improvement.

Reflected-player isolation passes with all other scene pixels unchanged on both
APIs, and the ordinary D3D12 repeat is pixel-identical. This successful run does
not establish a cause or a fix for the previous intermittent one-step difference.
The offscreen-NPC control now clears its original >100-pixel visibility gate:
147 D3D12 and 133 Vulkan pixels exceed 3/255, compared with 16 and 2 in the prior
receiver pass. Maximum difference is 14/255 on each API. That qualifies a faint
NPC-only contribution, not a clear, fully reconstructed character reflection.
The game image analyses and graphics validation pass.

Autochess's two standalone material groups and all seventeen editor cases pass
on both APIs. All 38 images are pixel-identical to the accepted baseline, with
no graphics-validation errors. Its prepared shader inputs, editor/plugin proof
and current binary hashes agree. Cold preparation took about 7.9 minutes for
the first Vulkan group and 17.8 minutes for the first D3D12 group; these are
shader-preparation observations, not frame-performance qualification. The
existing capture deadlines and image thresholds were unchanged.

`final/qualification-summary.json` consolidates the accepted results, including
the retained strict native API comparison failure. All 34 binary/generated
input hashes, nine implementation-file hashes, three embedded lighting sources
and the tracked engine/game source manifests agree with the tested inputs.
The 14 research arithmetic tests and both games' six CPU contracts pass.
This is Windows D3D12/Vulkan visual qualification, not Linux, performance or
RT-enabled retail parity qualification. The working subjective visual estimate
remains around 87%; the modest scene change does not establish the 90% target.

## Remaining visual target

The 2026-10-21 research follow-up completes the exact-event decoder/native
material comparison: 893,289 ordinary pixels pass, with maximum absolute
response error 2.92299e-6 using native DFG/radiance/visibility inputs. It now reads
the stencil and material constants directly at event 7161. The shader round trip
and within-event original outputs are exact; separate original replay validation
and cross-run repeat failures remain. See the research repository's
`lighting/reflections/MATERIAL_NATIVE.md` for scope and retained evidence.

Next, expose neutral-by-default authored reflection controls and qualify a
Cyberpunk ordinary-material profile. Do not substitute a universal brightness
boost or alter neutral previews. Keep very smooth direct-light highlights as a
separate qualification rather than applying the reflection roughness expansion
to the older direct BRDF unchanged.

That authored-control follow-up is now implemented; see
[REFLECTION_CONTROLS.md](REFLECTION_CONTROLS.md) for its separate qualification,
remaining strict failures and next visibility-producer target.

Evidence root in each checkout:
`artifacts/lighting-foundation/reflection-material-20261020`.
The before images and source snapshots are retained, alongside native logs,
the strict API comparison failure and independent response measurements.
Research evidence is in
`Cyberpunk2077Research/data/evidence/reflection-composition/engine-material-20261020`.
