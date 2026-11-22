# Directional reflection visibility

2026-10-19. Follow-up to [CONTACT_VISIBILITY.md](CONTACT_VISIBILITY.md), with
visual correctness still taking priority over performance optimization.

The same depth horizons now produce a bent direction: the normalized first
moment of the visible solid angle. The existing diffuse integral is unchanged.
Its cosine-weighted visibility defines a cone aperture through `sqrt(1-AO)`,
following [Jimenez et al., Equation 18](https://www.iryoku.com/downloads/Practical-Realtime-Strategies-for-Accurate-Indirect-Occlusion.pdf). This is an independent sampling/producer
choice, not a recovered Cyberpunk producer.

The reflected-light response uses the overlap between that visibility cone and
a roughness-dependent reflection cone. Smooth reflections have a narrower
transition than rough reflections. It acts on blended environment/planar
radiance before material response. Direct lights, emission and diffuse
multiscattering are not multiplied by it. Material-authored specular occlusion
remains separate. Both ordinary and composed PBR shaders use the same helper.

The disabled implementation is still compiled out of D3D12's legacy dispatcher;
active contact views use the constant-material pipeline. This preserves the
previous fix for FXC character corruption. No optional pass is enabled for
Autochess, reference locations or the shelved OpenGL backend.

## Evidence and scope

Cyberpunk2077Research's `lighting/reflections/analyze_directional_visibility.py`
executes the retained ordinary consumer's arithmetic/branches with supplied
inputs. For 262,144 cases, analytic cone overlap differs from its polynomial-acos
implementation by at most .000886225; mean error is .000014701 on a 0-1 scale.
With the captured grazing coefficient of -1, maximum is .000875224 and mean
.000014682. This qualifies the response, not its inputs or whole-engine parity.
The production near-open guard (`AO >= .999999`) and native sampling/precision
are separate from that supplied-cone arithmetic comparison.

The captured grazing-angle coefficient was traced through the draw's PS b12
descriptor to its placed resource and draw-time readback. It softens occlusion
at shallow viewing angles. Special-material exceptions are not adopted. The cone producer,
finite .8 m radius, .015 m bias, eight slices/four samples per side and lack of
temporal filtering remain Phlosion approximations. Hidden/offscreen blockers
are absent from depth. The resolved planar image can already contain near-field
occlusion; a single cone cannot represent arbitrary colored reflected objects.
General reflection tracing/probe fallback is still missing.

## Qualification

All 50 native contact, composition, reflection and material-composition checks
pass on Windows D3D12/Vulkan, with zero Vulkan core/synchronization errors. The
final smooth/rough metal cases darken the obstructed patch by 12.84/255 and
7.08/255 respectively on both APIs. The distant patches and the open-reflection
angle's sampled patches remain unchanged. These are localized behavior controls,
not scene-wide or retail-image errors.

Direct-only, emission-only and view-reset images remain exact against the
previous pass. The masked control differs from that baseline by at most 1/255,
within its unchanged 2/255 limit. Across nine native modes, maximum API mean
error is .011998/255; maximum pixel-channel error is 2/255. Final evidence is
`native-final.log`, `native-final-raw.log`, `native-api-final.json` and the empty
`validation-errors-final.txt` in the engine's private artifact directory.

The neutral-grazing game trial was stopped after its D3D12 launch passed, while
Vulkan was still compiling; the corrected coefficient was qualified separately.
The final game build passes both normal launches and five-visit scene switches,
plus all eight wet station/battle runtime/editor content and API checks. Its
12 field/battle/actor captures preserve the prior scene state. The initial strict
actor comparison detects 697/557 qualifying reflected-body pixels on D3D12/Vulkan;
Vulkan is exact outside the body bounds, while D3D12 has nine outside pixels
differing by 1/255. That initial exact-image gate remains recorded as failed;
it is not waived or converted into a looser acceptance limit. A fresh D3D12 pair
passes the unchanged exact-outside-bounds gate in `final/actor-repeat`, alongside
hash-verified copies of the unchanged final Vulkan pair. The initial small
difference did not recur; its cause is not established. The oracle is now
independently runnable with the game's `verify_planar_reflections.py --actors`,
and still passes the previous baseline without changing its checks.

All eight noon/sunset on/off runtime captures pass on both APIs. The four disabled
images are pixel-exact against the previous accepted scalar-contact baseline;
scene state matches within each pair. Enabled-view API mean errors are .312978/255
at noon and .198551/255 at sunset, below the unchanged 1.5/255 guide. The follow-up
validation-error report is empty. Noon, sunset, wet-night field and battle views
were also visually reviewed. Both affected game CPU contracts pass, and the three
staged game/model DLLs match the final builds. Research's 12 relevant arithmetic
and composition tests pass. These controls do not establish retail image parity.

The first Autochess Vulkan cohort exceeded the unchanged 180-second capture
deadline. The already-failing harness was stopped while retaining its second
cohort solely for cache preparation; it completed in 278.32 seconds. Subsequent
D3D12 cache preparation reached its own 600-second cap, retaining completed
shader entries for a second preparation attempt. Its first cohort then completed
in 496.19 seconds and its second in 58.33 seconds. These are cold-start failures
and cache preparation, not successful deadline qualification. Game D3D12 scene
prewarm alone took 563.59 seconds.

After preparation, Autochess passes both native cohorts and all 17 editor cases
on both APIs under the unchanged 180/240-second capture deadlines. All 38 images
are pixel-exact against the previously accepted `cpu-submission` baseline, and
the Vulkan validation-error report is empty. Runtime/editor builds, the paired
editor/plugin source fingerprint and four affected CPU contracts also pass.
`qualification-warm.log`, `baseline-comparison.json`, `cold-trial` and the
preparation logs preserve the distinction between these results. Private
evidence lives under `artifacts/lighting-foundation/directional-visibility` in
each project. The first compile failure (a remaining Vulkan scalar call site)
and corrected build are retained. No image acceptance threshold was weakened.

Matched 1080p heavy-wet-night runtime cost checks, 200 warm samples on an RTX
2060 SUPER, with validation disabled and no competing Cyberpunk/test job:

| Backend | GPU off / on ms | Host off / on ms | GPU added ms |
| --- | ---: | ---: | ---: |
| D3D12 | 19.752 / 35.349 | 19.851 / 34.530 | 15.597 |
| Vulkan | 20.176 / 35.786 | 22.340 / 37.644 | 15.611 |

Both exceed the existing +1.5 ms GPU / +2 ms host review guides. Vulkan's enabled
GPU p99 is 67.696 ms (host p99 76.024 ms), so it is not stable frame-time
qualification either. These measure the entire contact feature, including the
previous diffuse horizon integration, not the isolated cost of this directional
addition. Earlier scalar results were separate runs; subtracting them is not a
controlled incremental benchmark. `final/contact-performance.json` retains all
statistics. Visual implementation and functional checks are complete; performance
and first-use compilation remain open limitations. No performance claim follows.

The 90% visual target is not established by this component result. General
reflection-source coverage, temporal stability and indirect-light updates remain
the principal visual gaps. Linux is not qualified by these Windows checks.

The retained producer study now maps five dispatches: input preparation,
half-resolution directional visibility, spatial filtering, history validation/
clipping, and depth-aware full-resolution reconstruction. See the research
repository's `lighting/reflections/VISIBILITY_PRODUCER.md`. Their actual inputs,
outputs and constants still need matched qualification; the map is not a port.
Its custom constants are now reconstructed from submission-ordered CPU uploads
and copies, with identical hashes at all five dispatches. Native dispatch
readback/output comparisons remain pending. Existing captures support that next
step without another user capture session. This work is uncommitted.
