# Reflection detail and continuity

2026-10-20. Thin-source, continuity and shared-consumer checks pass. Offscreen
character visibility in the station remains unqualified.
This follows [captured-surface visibility](REFLECTION_VISIBILITY.md). Frame-cost
qualification remains deferred at the user's request.

## Changes

Screen reflection traversal now uses pixel-sized steps up to 4096 samples instead
of stretching 192 samples across the view. At 1080p the previous implementation
lost a thin illuminated strip at all five tested subpixel placements. Candidate
intersections solve perspective depth before reprojecting and validating the
sample. Binary refinement could jump from the thin source to background depth.
The existing thickness tolerance now supplies a smooth confidence factor, so
uncertain silhouette hits blend into local radiance instead of widening the
reflected source at full strength. The captured five-percent screen-border fade,
roughness response and material composition remain unchanged.
Approaching rays retain a candidate while seeking the actual surface crossing,
instead of accepting the faint entry into the tolerance band. If a subpixel
source disappears before that crossing, the best candidate remains the fallback.

The local capture can originate outside its receiver influence bounds. Capture
position must be finite and influence bounds must be finite and strictly ordered;
they no longer have to contain that position. This expands the supported input
domain without changing the uniform layout. PokemonCyberpunk follows its current
camera while keeping the station's receiver bounds and fade fixed. This avoids
the old fixed position's nearly edge-on sampling of some sources, and avoids a
capture-position jump when the camera crosses the influence boundary. Capture
resolution and the twelve-view count are unchanged.

Exploration and battle captures include current character poses. Native Pokemon
models use the same cube-face position, up vector, 90-degree projection, near/far
planes and extent as field geometry. A CPU oracle compares all twelve matrices.
Pose clocks retain the main view's sample time. Foliage, weather particles,
translucent motion trails and arrival-effect attachments are excluded from this
capture. Existing planar and screen passes keep their roles.

## Evidence

Evidence lives in `artifacts/lighting-foundation/reflection-continuity` in the
engine and both games. `commits.json` records fourteen organized checkpoint and
workspace commits made before this pass. No repositories were pushed.

The retained fixed-capture grazing case still reproduces its 9.160-pixel centroid
error at 640x480. Capturing from the camera reduces that same case to about .700
pixels on both Windows APIs. The additional nine-view sweep keeps the original
2.5-pixel position and 650--1550-pixel footprint gates. Capture quality below a
texel is still a finite-resolution limit, not a universally solved problem.

The first increased-sample-budget trial restored four of five thin strips. The
perspective-intersection correction restores the remaining strip. An initial
confidence trial improved the first handoff's position but made larger sources
too faint: four existing camera/edge cases failed in the 197-case run. Its
193 passing cases do not qualify that revision; it is retained under
`regression-confidence`. Seeking the actual crossing fixes those cases, while
retaining the best tolerance-band candidate preserves all five thin strips.
The nine handoff samples retain the unchanged 2.5-pixel limit and
must move monotonically and avoid a brightness discontinuity. Moving and removed
sources must match a fresh render of the final pose exactly; comparing with an
entirely disabled reflection pass incorrectly counted ordinary reflected light
as stale history in the first diagnostic.

`revised/d3d12-thin*` predates relinking the screen-test executable for the
intersection correction and is diagnostic only. Both test executables are
rebuilt for final acceptance and delete each requested screenshot before capture.
Likewise, `approach-fix` used the preceding embedded shader; `approach-rebuilt`
is the rebuilt trial that recovered camera/edge brightness but lost `thin2`.
The retained-sample revision is `band-fix`, followed by the final full run.
The interrupted 179-case run is retained under `interrupted-179`; it was stopped
to add the camera-capture fix and is not an acceptance result. The final suite
contains 197 cases, including all 147 earlier checks. A paired build also correctly
rejected source changes during preparation; the stable build supersedes it.

The final engine run passes all 197 cases with no Vulkan validation errors.
All ten thin-source images retain the source at its analytic reflected position.
Both APIs' nine handoff and nine grazing samples move monotonically; maximum
position errors are 1.772 and 1.965 pixels respectively, within the unchanged
2.5-pixel bound. The largest adjacent handoff brightness change remains below
10/255. Moving/removed-source final-pose controls are exact.

The separate strict cross-API image comparison is **not entirely passing**:
62 of 63 pairs are within 1/255, while `grazing-view4` has 25 pixels above that
limit, a maximum difference of 4/255 and mean .00015191/255. Both images pass
the same analytic placement and footprint gates. `api-diagnostics.json` retains
the affected pixels; `native-comparison.json` reports every pair, and its checker
still exits unsuccessfully for this outlier. The threshold was not widened.
This small edge precision difference remains open; it is not an identical-image
claim or evidence of recovered retail filtering behavior.

The initial full-HD D3D12 actor-isolation check failed its exact outside-region
requirement: 333 pixels differed by 1/255 outside the actor region; none differed
by more. Vulkan isolation was exact. An ordinary repeated D3D12 capture also
changed 355 pixels by at most 1/255. These failures remain under `actor-isolation`
and in the original scene outputs; their thresholds were not changed.

Diagnostic GPU waits and debug-layer-only runs produced a different but stable
image. Hashes of the final frame's constant, probe, skin, vertex and instance
uploads were identical. Rebuilding the previous screen shader retained the
one-step discrepancy. A complete previous-game control (previous shader,
fixed-position capture and static-only capture population) reproduced 389
one-step differences between normal and waited rendering; its normal image is
pixel-identical to the previously accepted `reflection-visibility` capture.
Thus this timing-sensitive behavior predates this pass. Resource reuse was an
initial hypothesis, not an established cause or a claimed fix. Temporary wait,
upload-hash and debug controls were removed; the 541 engine and 144 game source
hashes matched the final pre-diagnostic manifests after restoration. There is no
new per-frame GPU wait. A focused fresh-cache rerun also reproduces the separate
25-pixel grazing-view4 cross-API outlier.

Station qualification passes the normal editor launch, all eight wet field/battle
host/API views, exact planar actor isolation, all 28 close/daylight/camera-replay
images and both five-visit scene-switch sequences. The largest full-image API
mean difference is .352679/255. The final ordinary D3D12 repeat is pixel-exact.
Game CPU contracts pass 2/2; Autochess CPU contracts pass 4/4. The qualified editor
pair and all three staged Cyberpunk plugin DLL hashes match their built inputs.

Final explicit CMake configuration refreshed the standalone game's embedded
lighting program after diagnostic source restoration. Correct source hashes had
not established that the generated program was current. Nineteen D3D12 runtime
captures were repeated under the game's `final-runtime` artifact directory:
fourteen close/daylight/orbit views, two wet field/battle views, two isolated
player views and an exact repeat. They pass with unchanged thresholds. Earlier
D3D12 runtime captures remain historical evidence; unchanged Vulkan/editor inputs
are retained with copy hashes. The runtime's 50,315-byte embedded lighting block
matches its generated source and the qualified editor. The three game plugin
relinks have identical PE sections; their whole-file differences are metadata.

**Offscreen NPC visibility is still open.** The matched night control removes
only Willie from local capture, with planar/screen reflections and bloom disabled.
Willie is behind the camera, and scene, environment and animation state match.
Both APIs change only two pixels by more than 3/255, below the original greater-
than-100-pixel visibility gate. Daylight pump/bar and nearer placements do not
pass either (0--37 pixels above 3/255). One attempted alignment is unusable because
camera collision pushes the view behind the pump; the other five positions are
unobstructed. `npc-diagnostics.json` records each result and the original failed
checker and captures remain available. Neither material/light strengths nor the
gate were changed. Current-pose population is implemented and controlled engine
moving/removed-source checks pass, but those do not establish a convincing
offscreen animated-character reflection in the actual station. The next step is
to inspect the captured character radiance/geometry and receiver contribution to
separate missed hits from rough-surface attenuation, rather than add more blind
viewpoint trials. Do not count this part as closed parity work.

Autochess's two native material groups and all seventeen editor cases pass on
both APIs. All 38 images are pixel-identical to the accepted `cpu-submission`
baseline. The supplemental native/editor validation scan, including VUID checks,
finds no errors. `baseline-comparison.json` and `validation-errors-final.txt`
retain those results. This consumer compatibility result does not close the
station NPC visibility or strict native grazing-edge comparisons above.
The relinked Autochess plugin also passes four character/Gastly stage captures,
pixel-identical to the accepted baseline. Final binary payload checks retain all
bytes except the PE build timestamp and checksum, preventing later pin/configure
steps from silently changing the validated code or embedded programs.

Preparation exposed a saturated Vulkan driver cache: the field hosts loaded
266,203,520 bytes and could not save their approximately 277.8 MB updated payload
under the existing 256 MiB limit. The old file was moved, with a verified hash,
to `cache-before-refresh`. A fresh battle runtime saved a roughly 17 MB cache. The subsequent full-HD
actor captures saved and reloaded 33,576,476 bytes successfully.
This removes obsolete shader-history accumulation for this qualification without
changing engine cache limits, shader inputs or image thresholds. It is cache
preparation evidence, not a frame-performance result.

Autochess's first cache-preparation cohort takes 627.720 seconds on Vulkan and
1621.763 seconds on D3D12, within the existing 1800-second preparation watchdog.
These long first-compilation times remain a startup limitation. The later native
and editor image checks still use their original 180/240-second deadlines;
prepared-cache success must not be presented as cold-start or frame-cost parity.

## Limits and provenance

This is independent production implementation, not recovered Cyberpunk 2077
probe-generation or temporal-reconstruction code. It still captures one geometry
layer at one position. Surfaces hidden from that position, subtexel features,
transparency and full dynamic indirect bounce remain limitations. There is no
temporal reflection history or hardware ray tracing in this change. More screen
trace samples and captured actors add work; no performance or Linux claim is made.
Component image checks do not establish a measured whole-renderer percentage.
