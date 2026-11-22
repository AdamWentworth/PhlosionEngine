# General screen reflection receivers

2026-10-19 implementation, qualification completed 2026-10-20. Visual follow-up to
[DIRECTIONAL_VISIBILITY.md](DIRECTIONAL_VISIBILITY.md).
Retained evidence is under each project's ignored
`artifacts/lighting-foundation/screen-reflections` directory.

## Implementation

`IWorldScreenReflection` brackets a current-view opaque HDR source pass. Both
Windows renderers capture its radiance and depth into bounded, frame-local
resources and clear the view for the final submission. The previous planar
capture now shares the radiance snapshot allocator/copy code. Per-view state is
reset at the beginning of every world color pass; a second screen-source begin
and a screen-source begin during planar capture are rejected. The new
path is optional and not enabled for Autochess or the Colosseum reference.

The game enables it in the detailed station, in exploration and battle, including
dry daylight. It preserves scene assets, material values, light strengths, weather
and camera policy. The source pass excludes blended geometry and recursive screen/
planar reflections. The existing fixture capture overrides omit canopy emission
already represented by analytic direct specular, while signs and other emission
remain. Rain, billows and post-process glow are not captured as opaque geometry.

The shared shader traces the current depth layer with perspective-correct
reciprocal-depth interpolation, a near-plane clip, a bounded 192-step walk and
six-step candidate refinement. The default range is 40 m, thickness .12 m and
origin offset .02 m. A nine-tap source-radiance filter grows with roughness;
nearer foreground taps are rejected while farther background taps soften source
edges. It does not trace a rough reflection cone. Coverage fades from .45 to .75. These are
independent producer choices, not recovered retail constants or algorithms.

Successful hits replace part of the environment radiance before the common
material response and directional visibility. The existing planar source then
retains priority for its wet horizontal receivers. Screen misses retain the
existing environment radiance; this is not a local reflection-probe solution.
The qualified captured border factor fades linearly within five percent of the
screen edge. Research evidence and exclusions are in Cyberpunk2077Research's
`lighting/reflections/SCREEN_REFLECTIONS.md`.

## Acceptance and limits

Native controls use an analytically projected vertical mirror image, with a
separate directly viewed emitter. They cover ordinary/254 material paths, rough
and matte receivers, blocked and masked-away blockers, source-off, view reset,
another camera angle, an offscreen source and a partially clipped bright source.
Existing contact/planar/material
checks, runtime/editor station and battle views, normal launch/scene switching,
and shared Autochess consumers remain required. No acceptance limit is relaxed.

The initial native D3D12 compile rejected a scalar GLSL vector constructor after
HLSL translation; it was corrected and the failed log retained. Both initial
mirror probes then passed with identical reflected-patch response and unchanged
direct-source pixels. A stricter whole-image comparison exposed a one-pixel
reflection shift: the fixture's implicit Vulkan fallback normal was slightly
tilted. The analytic flat-mirror fixture now explicitly disables normal mapping.
The initial bilateral filter also rejected background taps and retained sharp
source edges; foreground-only rejection and an edge-softening assertion repair
that control. The original comparison report is retained.

The final combined native suite passes all 74 cases: 23 screen-reflection
contracts, 50 affected contact/planar/composition checks, and Vulkan submission.
Its Vulkan core/synchronization error scan is empty. Ten screen-reflection API
images are pixel-exact; the rough case differs by 1/255 in three channel samples
(mean absolute difference .000003255/255). This is API agreement in the controlled
fixture, not agreement with the retail game. `native-final.raw.log`,
`native-final.ctest.log` and `native-api-comparison-final.json` retain the results;
earlier runs and failures remain alongside them.

The first game build passes both normal launches/five-visit scene switches and
all eight wet station/battle API/host captures. Its isolated player-reflection
check fails the unchanged exact outside-bounds requirement on D3D12: 350 pixels
change by 1/255, with 698 reflected-player pixels changing by more than 3/255
inside the expected bounds. Vulkan passes with 557 inside and zero outside.
The original `final/scene` captures and `actor-diagnosis.json` are retained.

Review found two unsafe image-edge cases in the new path: a biased ray origin
can project outside the view before entering it, and bilinear color sampling
could touch the uncopied padding of the power-of-two capture texture. Depth
lookups now reject outside coordinates and clamp rounded texels; color lookups
stay inside the copied texel centers. The additional partially clipped bright
source case covers the screen boundary. With those corrections, both game actor
pairs pass the original exact outside-bounds requirement: zero changed outside
pixels, with 698 D3D12 and 557 Vulkan reflected-player pixels changing by more
than 3/255 inside. The two fixes were qualified together; the initial discrepancy
has not been independently attributed to one of them.

The corrected build passes all eight wet field/battle runtime/editor captures
and a normal D3D12 launch. Twenty additional runtime images cover source on/off
on both APIs at the pumps and bar at night, the pumps at noon and sunset, and a
turned camera. Matching on/off scene state passes, as do the unchanged API image
limits (maximum mean absolute difference .337327/255). The turned camera changes
yaw/pitch from 0/.1 to -.24/-.02; this is viewpoint coverage, not continuous-motion
stability evidence. Night pump views contain about 1,900 pixels with changes
greater than 3/255; the rough bar facade changes much less. The improvement is
localized, not a large whole-scene makeover. The Vulkan error scan is empty.

Game evidence is in `qualified/scene`, `qualified/launch` and `review`. The initial
normal launches/five-visit scene switches are retained in `final/switch`; the
later edge corrections affect sampling, not scene selection. All three staged
game DLLs match their final built hashes. Research's 15 tests and the game's two
affected CPU contracts pass. Autochess's four affected CPU contracts, both native
material cohorts and all 17 editor cases pass on both APIs. All 38 consumer images
remain pixel-exact against `cpu-submission`, with no Vulkan validation errors.
`qualification-03.log`, `baseline-comparison.json` and the native/editor reports
retain that evidence. Production Autochess source and ModelSupport source are
unchanged by this pass.

The final editor/plugin pair verification passes against its source and binary
fingerprints. A separate engine manifest verifies 537 shader/source/test/build
inputs; the pair's source fingerprint alone does not include shader assets.

Autochess cache preparation completed its first Vulkan cohort in 494.54 seconds
and its second in 72.05 seconds. D3D12 preparation hit its separate 600-second
watchdog twice; `qualification.log`, `qualification-02.log`, preparation logs
and both `d3d-cache-after-timeout` reports retain the failures and completed
cache entries. A final preparation run reused those entries and completed the
two D3D12 cohorts in 62.24/66.36 seconds, under an 1800-second preparation watchdog.
These are resumed-cache times, not complete cold-start times. The subsequent
native and editor image checks pass their original 180/240-second deadlines.

This implementation requires a perspective camera. One depth layer cannot see
hidden/offscreen objects, back surfaces or view-dependent
source radiance from another viewing direction. This path has no temporal history,
local cubemap fallback or stochastic roughness sampling. The source capture adds
another opaque submission and the forward trace has significant expected cost;
performance optimization remains deferred and its incremental cost is unmeasured.
Cold shader compilation takes minutes; successful runtime qualification uses
warmed caches without relaxing the original test deadlines. Windows D3D12 and
Vulkan are covered; Linux is unverified. These are
concrete coverage improvements, not a claim that the 90% target is achieved.

2026-10-20 follow-up: [local environment reflections](LOCAL_REFLECTIONS.md) now
provide an offscreen static-scene fallback beneath this screen-space path,
including footprint-filtered radiance for rough receivers. The limitations above
describe this screen-only pass; temporal reconstruction and offscreen dynamic
geometry remain open.
