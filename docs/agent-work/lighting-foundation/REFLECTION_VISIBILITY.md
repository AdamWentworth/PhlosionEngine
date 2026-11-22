# Captured-surface reflection visibility

2026-10-20. Implementation and targeted Windows qualification complete.
This follows [local reflections](LOCAL_REFLECTIONS.md). Performance is deferred.

The station reflection now intersects captured surface planes instead of
assuming every source sits on the influence box. The first six views capture
linear radiance; six more capture geometric normals and radial distance. Geometry
occupies previously unused parts of the existing atlas, with no extra texture,
descriptor, or uniform allocation. Only a complete 12-view generation is sampled.
All game callers use `worldLocalReflectionViewCount`. Each new main view still
invalidates the capture; no previous-scene or previous-frame history is consumed.

Geometry alpha stores distance + 1; clear/sky alpha 1 means empty. Alpha testing
runs before geometry output, so masked holes do not become blockers. Normals
come from screen derivatives of world position, not normal maps or smoothed
vertex normals. Color downsampling never touches geometry cells. The atlas
contract checks that all faces and filtered levels stay in bounds without overlap.

A bounded ray traverses the capture, intersects candidate surface planes, and
checks the hit against the geometry at its reprojected direction. The nearest
validated intersection supplies radiance to the existing material response.
The roughness footprint is converted from receiver distance to probe angular
size. Receivers hidden from the capture fall back to the existing environment;
a rough lobe whose central ray misses retains the broad box-filter approximation.
SSR and planar reflections retain their existing priority and dynamic coverage.

This is an independent implementation, not recovered Cyberpunk probe code.
[McGuire et al. 2017](https://research.nvidia.com/publication/2017-02_real-time-global-illumination-using-precomputed-light-field-probes)
describes geometry-bearing light-field probes; this pass does not implement its
complete multi-probe system. Retail capture/update policy remains unqualified.

## Evidence and failed trials

Evidence root: `artifacts/lighting-foundation/reflection-visibility` in the engine
and the consuming games. The original box-only build fails the blocker control
(163.84/255 mean green contribution remains behind the blocker) and the interior
source control (no visible reflection at its analytic position). The alpha-only
trial fixed blocking but still misplaced shallow-angle sources; `initial-03`
retains those images. Surface-plane intersection replaces that radial shell.

FXC removed the alpha write when a helper wrapped the large pixel evaluator's
return expression. The retained disassembly has no capture-position access.
Explicit writes in the shader entry point produce matching D3D12/Vulkan distance
outputs. Diagnostic color hooks are removed. No diagnostic frame wait is enabled.

The initial interior-source centroid fixture clipped the source at the finite
mirror edge. Its mirror now covers the full analytic reflection. The first motion
fixture also moved the source from x=4 to x=4.5. The corrected motion control keeps
the same x=4 source and moves only the camera from x=-.8 to 0, leaving the source
entirely offscreen. Its original 2.5-pixel centroid limit remains unchanged.
Initial images/logs remain available; they are not represented as passing tests.

## Qualification

The final Windows renderer suite passes all 147 checks: 55 local-reflection
controls and 92 affected existing checks. All 27 D3D12/Vulkan image pairs differ
by at most 1/255. The nine camera positions on both APIs move monotonically;
the largest measured error from the analytic reflection center is 1.366 pixels
at 640x480, below the unchanged 2.5-pixel limit. Vulkan validation is clean.
See `engine-tests.log`, `native-comparison.json` and `validation-errors.txt`.
PokemonCyberpunk builds and both affected CPU contracts pass. Eight wet
exploration/battle views pass across both APIs and hosts. Reflected-player
isolation changes 710 D3D12 / 552 Vulkan pixels by more than 3/255, with zero
changes outside the prescribed bounds. All 28 close pump/bar, noon/sunset/night
and camera-replay images pass. Their 14 on/off pairs preserve scene state and
change 757–20,124 pixels by more than 3/255; the largest cross-API mean difference
is .337326/255, below the unchanged 1.5 limit. The three orbit samples have
consistently advancing camera angles; this is not every-frame temporal coverage.

Normal launch and five-visit station/reference scene switching pass on both APIs.
An ordinary repeated D3D12 actor-on capture is pixel-identical, without the
diagnostic frame wait. The game validation scan is empty. All three staged game
DLLs match their built inputs; 541 engine and 144 game source hashes remain
unchanged from the qualified builds. `binary-provenance.json` retains the final
editor/plugin proof and executable hashes. No game material, fixture-strength or
weather tuning was changed in this pass.

Autochess builds, four CPU contracts and its editor/plugin pair proof pass.
Both native material groups and all 17 editor cases pass on both APIs. All 38
images are pixel-identical to the accepted CPU-submission baseline, and the
final Vulkan validation scan is empty. Production Autochess and ModelSupport
source are unchanged by this pass.

The first Autochess cache-preparation captures lacked the fixture's environment
and script links. Those images are diagnostic only. The missing content was
attached before native acceptance, and the preparation helper now creates the
same links as the native harness. The final full-scene native/editor checks keep
their original 180/240-second deadlines and expected-content guards. See the
consumer's `preparation-notes.txt`, `fixture-content-links.json`,
`baseline-comparison.json` and `final-validation-scan.txt`. The engine's
`qualification-summary.json` consolidates the passing results and their scope.

## Limits

This still has one capture position and one geometry layer. It cannot reconstruct
surfaces hidden from that position, offscreen moving objects, or geometry smaller
than captured texels. The retained x=4.5 grazing-source case remains visible but
has about 9 pixels of centroid error at 640x480; do not claim all-angle positional
accuracy. Finite ray steps can miss thin surfaces, and rough-lobe filtering can
cross silhouettes. There is no temporal reconstruction or dynamic diffuse bounce
update in this pass. Twelve captures increase work; frame cost is unmeasured.
Cold shader compilation remains slow: the first normal D3D12 editor probe in
this pass took about 19 minutes while preparing new shader variants. Subsequent
image checks reuse the prepared cache; they do not qualify cold-start time.
Linux is unverified.

The approximately 85% assessment discussed with the user is a subjective visual
engineering estimate. These component checks do not establish a measured whole
lighting percentage or achievement of the 90% target.
