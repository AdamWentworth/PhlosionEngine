# Local environment reflections

2026-10-20. Follow-up to general screen reflections. Windows qualification complete;
see `artifacts/lighting-foundation/local-reflections` for raw logs and images.

The subsequent [captured-surface visibility pass](REFLECTION_VISIBILITY.md)
extends this six-view color capture with six geometry views in unused atlas space.
The description and evidence below record the original color-only implementation.

`IWorldLocalReflection` captures six static-environment views into a frame-owned
linear RGBA16F atlas, before planar/contact/screen passes. A complete capture is
required before sampling. Every main view starts disabled, so scene switches,
resize and time changes cannot reuse a previous scene's capture. This initial
visual implementation captures every frame; it deliberately has no persistent
refresh cache or amortized face updates.

The six faces occupy the first two atlas rows. Five successively smaller image
levels occupy the remaining space. Each level is downsampled per face using
the existing HDR draw/copy path; explicit copy/read barriers separate sampling
from writes. Clamps stay inside populated texel centers. Box projection adjusts
reflection direction for receiver position. A deterministic 64-sample GGX
convolution chooses image levels from sample footprint to avoid separated bright
copies on rough receivers. This replaces the rejected unfiltered attempt.

Local radiance supplies the fallback before SSR and planar blending and before
the common material/DFG response. The source omits analytic rectangle emission
already represented by direct specular lighting. Shadows and scene illumination
remain active. Static buildings, equipment, terrain and signs are captured;
game callbacks omit moving actors, foliage and weather particles. SSR and the
planar path still supply their existing dynamic coverage.

PokemonCyberpunk owns the station capture at (-8,2,-20), in the passage between
the bar and canopy, with a bounded fade around the location. Runtime and editor,
exploration and battle use the same policy. `PHLOSION_DISABLE_LOCAL_REFLECTIONS`
and `Verify-SkyWeather.ps1 -DisableLocalReflections` provide matched controls.
The Colosseum reference and Autochess do not enable this optional feature.

Limits: one box approximates the surrounding geometry, so nearby surfaces can
have parallax errors; this is not per-pixel ray-traced visibility. Offscreen
moving actors, multiple probe blending, temporal reconstruction, and exact
retail probe production are not implemented. The finite-resolution atlas and
deterministic filter remain approximations. Added rendering cost is unmeasured;
performance work is deferred. Linux is unverified. This work alone does not
establish the requested 90% overall lighting target.

Final native qualification passes all 123 selected checks: 31 new local-reflection
cases plus 92 affected screen/contact/planar/composition/submission/haze/HDR checks.
The new cases include an entirely offscreen emitter at its analytic mirror
position, six rotated scene orientations, a face-boundary emitter, a camera
moving over four frames, roughness, emitter removal, incomplete capture/reset,
and influence bounds. All 15 D3D12/Vulkan output pairs differ by at most 1/255;
the rough case is pixel-identical. The complete CTest raw log has no Vulkan
validation or synchronization errors. These controlled checks do not establish
retail full-frame equivalence or all-camera parallax accuracy.

Evidence in `qualified-04`: `engine-tests.log`, `engine-tests-raw.log`,
`native-api-comparison-final.json`, `validation-errors.txt`, and the
541-input `source.json` under the local-reflections artifact root. The
rejected rough filter's images/log are retained separately. Initial integration
also caught a missing CMake include expansion, HLSL `fract` incompatibility,
and a stale/corrupt incremental game object; these were corrected before the
passing builds. No acceptance threshold was loosened.

Game integration also caught a missing D3D12 composed-surface call; both composed
shader paths now have a contract assertion. Repeated full-HD D3D12 captures then
exposed a small preexisting instability (1–2/255) before SSR consumption. A
dedicated sampler and a contact-depth address clamp did not remove it. Keeping
D3D12 depth snapshots per in-flight frame removed the observed differences in
ordinary captures, without adding a normal-rendering fence wait. Diagnostic
trace/source-only hooks are removed; optional `PHLOSION_D3D12_VALIDATION=1`
retains a serial debug-layer path for diagnosis, never used for the final image
comparisons. Original failures and trial results remain in artifacts.

Final game qualification in `qualified-04` passes eight wet field/battle views
across APIs/hosts, exact reflected-player isolation (704 D3D12 / 552 Vulkan
pixels over 3/255, zero changes outside the prescribed bounds), and 28 close
pump/bar, noon/sunset/night and camera-replay images. All 14 on/off pairs preserve
scene state and change 659–43,969 receiver-view pixels by more than 3/255. The
maximum cross-API mean difference is .337475/255, below the existing 1.5 limit.
The three replay samples have distinct, consistently advancing camera angles;
they do not prove stability at every frame or angle. Normal launch and five-visit
station/reference switching pass on both APIs. Vulkan validation is clean.
The game evidence includes 144 source input hashes and matching built/staged
hashes for all three game DLLs. An ordinary final-build D3D12 actor-on capture
is also pixel-identical to the earlier repeated-input control.

Autochess passes both native cohorts and all 17 editor material cases on both
APIs. All 38 images are pixel-identical to its accepted CPU-submission baseline;
four CPU contracts and the final editor/plugin pair verification pass. Its
validation scan is empty. Evidence is in Autochess's
`artifacts/lighting-foundation/local-reflections`: `baseline-comparison.json`,
`qualification-final-03.log`, `editor/report.json`, `native/captures/matrix-report.json`,
and `editor-pair-final/editor_pair_proof.json`. Production Autochess and
ModelSupport source are unchanged by this pass.

The first Autochess native Vulkan attempts exceeded their original 180-second
deadline. Preparation had warmed copied fixture shaders before the harness
refreshed them. The corrected preparation refreshes the fixture first, records
all 15 shader/profile hashes, and renders the actual capture frame (164).
Those hashes remain unchanged during qualification. Passing matrices use warmed
caches and unchanged 180/240-second deadlines. Cold shader compilation remains
slow; this is not a passing cold-start or frame-performance result. Failed
attempts and preparation logs remain alongside the final evidence.
