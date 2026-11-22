# Live contact visibility

2026-10-21 update: perspective views now use the buffered directional path
described in [CHECKPOINT.md](CHECKPOINT.md#current-pass-buffered-directional-contact-visibility).
It captures unmapped geometric normals with depth, searches 15 directions with
10 samples at half resolution, filters by depth/normal, reprojects and clips
previous visibility, then reconstructs at full resolution. Diffuse and reflected
light consume the resulting direction/cone separately. The earlier inline search
below remains the orthographic fallback and diagnostic legacy path.

`PHLOSION_LEGACY_CONTACT_VISIBILITY=1` selects that fallback;
`PHLOSION_DISABLE_CONTACT_HISTORY=1` isolates the new spatial result.
Camera history rejects scene/view changes, discontinuous frames, resizing,
projection changes and camera cuts. Object motion vectors and hidden/off-screen
geometry remain unsupported. Production uses FP32 arithmetic and FP16 snapshots;
the independent research replay's byte-level agreement is not a production claim.
Current qualification and retained failures are in the checkpoint. The following
sections record the original 2026-10-19 implementation and its qualification.

2026-10-19. Native and PokemonCyberpunk visual qualification passed; GPU cost
exceeds the existing review guides. Detailed results and limitations follow below.
The user prioritized visual lighting agreement before further hardware-specific
frame-time optimization. This is the contact-shading part of that work.

The optional `IWorldContactVisibility` interface captures current opaque/masked
depth before the main color pass. The callback uses the same geometry and poses;
the shader excludes blended effects and skips full PBR lighting in the prepass.
Depth is copied to the existing immutable snapshot target, then the main depth
buffer is cleared. Both Direct3D 12 and Vulkan bind the snapshot independently
of material projection, planar radiance and shadow maps. All view boundaries
reset the state. OpenGL remains shelved and does not implement this interface.

Eight screen-oriented slices, with four samples on each side, search nearby
horizons. The visibility integral follows section 4 of Jimenez et al.,
[Practical Realtime Strategies for Accurate Indirect Occlusion](https://www.activision.com/cdn/research/PracticalRealtimeStrategiesTRfinal.pdf).
The sample pattern, last-20%-of-radius fade, geometric normals and lack of
temporal reconstruction are Phlosion choices. Texture normal detail is excluded
from this geometric visibility calculation to avoid self-darkening flat terrain.
There is no claim of recovering Cyberpunk's full visibility producer.

The game enables this for the authored station in exploration and battle, with
a 0.8 m radius and 0.015 m bias, independently of whether its lights are on.
The Colosseum reference and unrelated consumers leave it disabled. Battle effect
composition must not clear the arena depth during this prepass. A diagnostic
`PHLOSION_DISABLE_CONTACT_VISIBILITY` / `Verify-SkyWeather.ps1 -DisableContactVisibility`
provides an otherwise identical off control.

Visibility combines with authored AO by minimum, then enters the already
qualified albedo-dependent diffuse-occlusion response. It affects environment
diffuse, ambient diffuse and static-probe bounce. Direct lights and emission
retain their previous response. Material AO still supplies existing specular
occlusion: scalar diffuse visibility is not indiscriminately applied to the
resolved planar reflection. The reflected view itself does not run this prepass.

Research confirmed a directional input at retail composition event 7161/t4
(resource 19916). Its bent direction/cone supports distinct diffuse and specular
responses. See Cyberpunk2077Research's
`lighting/reflections/CONTACT_VISIBILITY.md`. Bent-direction production, the
captured specular-cone response, general SSR/probe fallback and temporal
reconstruction remain separate gaps. Screen-space contact visibility misses
off-screen/hidden blockers and approximates thin geometry by the visible depth
layer. This implementation is not whole-engine parity or a measured percentage.

New native checks cover a wall/floor contact, finite influence, masked-away
blocker, ordinary and mode-254 PBR, direct-only and emission-only invariance,
and view reset. Existing indirect composition and planar blocker/off-screen/
resize/reset checks remain required. Private development logs and retained
initial trials live under each project's
`artifacts/lighting-foundation/contact-visibility`.

## Qualification

The 30 focused native contact/composition/planar checks pass on Windows D3D12
and Vulkan. Contact darkening is about 15.4/255 in the wall/floor patch, with
zero change in the distant patch. Direct-only, emission-only and reset controls
are pixel-exact. Vulkan core/synchronization validation reports no errors.
The subsequent test-only non-Windows environment-variable setup also rebuilt and
passed its CPU contract; Linux itself has not been run. RTK wrapper attempts
failed, so the retained native CTest log is the acceptance evidence.

The eight station/battle runtime/editor captures pass existing content, fixture,
allocation and API/host image guards. Station API MAE is .48287/255 or less;
runtime/editor MAE is below .000016/255. The independent neon/puddle/bloom and
front/oblique/dry/legacy reflection controls pass.

The original low-resolution actor oracle had essentially no margin: Vulkan
went from 101 to exactly 100 pixels above its 3/255 cutoff, with at most 1/255
change in the reflected-player difference. A closer camera reduced the reflection
because the view angle changed, so that trial was rejected. The original 14 m
view is now captured at 1920x1080, at the same gameplay frame and material values.
D3D12/Vulkan resolve 698/556 qualifying pixels. The >100 requirement and
pixel-exact result outside independently projected mirrored body bounds are
unchanged. Both failed trials, their scene states and diagnosis remain in the
game's `qualified/scene`; no production reflection strength was adjusted.

Full editor validation subsequently exposed two existing surface-lifecycle
problems that the native world views did not exercise. Display-format editor
images were attached to the HDR render pass. They now use a display-format pass
compatible with ordinary pipelines, ending in sampled-image layout with an
explicit write-to-sample barrier. `PhlosionVulkanSubmission` now alternates editor
HDR/display views through resize, vsync changes and teardown; that extension and
the native contact case pass with zero core/synchronization errors.

The editor also removed ImGui texture descriptors before waiting for the GPU
during replacement. Descriptor removal now waits on replacement/teardown only,
not normal frames. The failed full-editor trials are retained under the game's
`qualified/switch` and `qualified/editor-target-fix`; final qualification uses
`qualified/editor-lifetime-fix`. Shader arithmetic and reflection strength were
unchanged by these lifecycle repairs.

After the descriptor-retirement repair, the normal Vulkan launch, five-visit station/reference switch and wet
station/battle views pass with zero core/synchronization errors. Both affected
runtime/editor scene images and their scene state are exact against the images
before the editor repairs. That runtime pair is reused with source/hash
receipts because the descriptor-retirement fix only changed `engine_editor`.
Both APIs' contact-disabled night images exactly match the preceding accepted
`cpu-submission` baseline. Matched noon/sunset/night pairs retain identical scene
state and show local geometry contact changes; all three staged game/model DLLs
match their build hashes.

The shared Autochess check subsequently found a D3D12 material-dispatcher
regression: contact visibility was disabled, but compiling the full horizon
search into the legacy character dispatcher produced speckled model surfaces.
The cohort-1 broad comparison passed despite the corruption; cohort 2's authored
wing guard failed. Retained captures and a close comparison are under Autochess's
`dispatcher-failure` and `native/cohort1-d3d12-baseline-comparison.png`.

The legacy dispatcher now excludes contact code at compile time, and active
contact views select the existing constant-material pipeline factory. The
focused character reproduction matches the previous accepted image exactly after
this change (`dispatcher-probe-comparison.json`). This preserves contact support
for views without fixtures or bounce probes rather than silently disabling it
with the legacy path. Cold compilation remains slow even after this correction.
All 23 affected D3D12 contact/composition/reflection and CPU checks pass after
the correction; the 12 native contact on/off images are exact against their
pre-correction versions. Autochess's affected D3D12 material-constant contract
also passes. The final source-bound editor/plugin pair is recorded under
Autochess's `editor-pair-dispatcher-fix`.

All 38 shared-consumer images then matched the old baseline, but enabling Vulkan
validation also exposed overlay errors: debug/sprite pipelines used the display
format inside HDR views, and sprites updated vertex-only constants against a
layout range shared with the fragment stage. HDR views now select compatible
debug/sprite pipelines; sprite updates include both declared stages. The native
submission test draws both overlay types in HDR/display editor targets and the
main window through resize and teardown. It passes with zero core/synchronization
errors. The failing 38-image run is retained under Autochess's `pre-overlay-fix`;
the corrected source-bound editor/plugin build is in `editor-pair-overlay-fix`.

Final Autochess qualification passes both native material cohorts and all 17
editor cases on D3D12 and Vulkan. All 38 images are pixel-exact against the prior
accepted `cpu-submission` baseline, and core/synchronization validation reports
zero errors. The original 180-second native and 240-second editor deadlines and
content/image thresholds are unchanged. Evidence is `baseline-comparison.json`,
`native/captures/matrix-report.json`, `editor/report.json`, `consumer-final.log`
and the empty `validation-errors.txt` in Autochess's `contact-visibility` artifacts.

Final PokemonCyberpunk evidence is in `qualified/final`. Both normal launches and
five-visit station/reference switches pass. All eight wet station/battle captures
pass content, fixture, shelter and renderer/host checks, with zero Vulkan
core/synchronization errors. All 12 field/battle/reflected-player images and their
recorded scene state are exact against the earlier contact-pass results. The three
staged DLLs match the final build hashes. A private verification invocation initially
used the wrong group name; the retained captures were renamed to the expected
`rain-field` and verified without recapture or altered thresholds. Its failed log
and the successful `final-continuation.log` are retained.

The extra exact-image diagnostic passes 15 of 16 final images. The D3D12 contact-on
1080p timing image differs in 482 of 2,073,600 pixels, by at most 2/255; mean RGB
absolute difference is 0.0000825/255. All scene states match. The strict exact-image
assertion remains failing and is not relaxed; the normal API/content guards pass.
This small timing-image difference was visually reviewed and is recorded in
`final/image-preservation.json` and `final/performance-image-review.json`.

## Cost: visual prototype, not performance-qualified

One RTX 2060 SUPER, 1920x1080 heavy wet night, 200 warm frames (30-229),
validation disabled, matched scene state:

| API | GPU off/on | Added GPU time | Host off/on |
| --- | --- | --- | --- |
| D3D12 | 20.317 / 32.209 ms | 11.891 ms | 21.234 / 31.836 ms |
| Vulkan | 20.100 / 34.454 ms | 14.354 ms | 22.579 / 36.397 ms |

Both exceed the existing +1.5 ms GPU / +2 ms host review guides. These limits were
not relaxed, and this is not a performance pass or a 60 FPS result. The user
prioritized visuals; the current on/off comparison is the visual reference for
subsequent optimization. The current implementation evaluates the horizon search
inside forward material shading and also redraws opaque/masked geometry for
depth. A separate reusable visibility buffer is a candidate for reducing that
cost while preserving the contact result; this improvement has not been built
or measured. The optional interface permits changing that implementation without
moving the rendering policy into the game or character adapter.

Evidence: game `qualified/contact-pairs.json`, `qualified/final/contact-performance.json`
and `qualified/final`; the shared full-frame timing parser was reused
for the cost report. These are warm-cache Windows measurements, not cold startup
or hardware-general performance. First-use shader compilation remains expensive.

Autochess's image harness requires Windows PowerShell 5 for System.Drawing;
the paired source/binary proof is verified in PowerShell 7, the host that created
its fingerprint. Attempts in the wrong hosts are retained separately and do not
count as passes. First-use native captures exceeded their unchanged 180-second
deadline. Separate cache preparation took 227.16 seconds for the initial Vulkan
cohort and an additional 402.38 seconds for the first D3D12 cohort after its
timeout; the second D3D12 cohort took 60.34 seconds. These are preparation runs
with partially populated caches, not controlled cold-start benchmarks or deadline
qualification. They establish a real startup limitation. Successful warmed
captures must not be presented as resolving it. Logs and cache provenance are
retained in Autochess's `contact-visibility` artifacts, including `cold-trial` and
`consumer-cache-preparation.json`.
The corrected D3D12 dispatcher reproduction also required 271.99 seconds with
new shader variants (`dispatcher-probe.json`); the material repair did not resolve
first-use compilation cost.
