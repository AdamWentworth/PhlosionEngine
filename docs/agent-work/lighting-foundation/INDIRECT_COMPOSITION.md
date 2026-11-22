# Reflection and indirect-light composition

Historical checkpoint. The replacement ordinary indirect pipeline and its current
qualification status are tracked in [INDIRECT_COMPLETION.md](INDIRECT_COMPLETION.md).

2026-10-19. Production changes are in the generic engine surfaces and shared
lighting functions. Research arithmetic and private captured inputs remain in
Cyberpunk2077Research; no retail shader or table payload enters the engine.

`worldPlanarRadiance` blends scene-linear reflection radiance with the environment
fallback before material response. Both sources use the same licensed GGX DFG
moments on outdoor surfaces. Diffuse multiscattering remains throughout the blend.
Previously a different material response was applied to each source, and the
planar fade also removed multiscattering. Coverage, nine-tap filtering, reflection
resolution and source selection remain the existing planar approximation.

Outdoor DFG evaluation uses the visible hemisphere for two-sided world meshes,
matching the planar receiver and local direct-light path. The first integration
used the unflipped normal, over-brightening reversed floor meshes. The existing
reflected-glyph gap/edge test caught it; initial images remain under
`artifacts/lighting-foundation/indirect-composition/initial-reflection-images`.

`environmentDiffuseOcclusion` applies the albedo-dependent near-field energy fit
from Jimenez et al., [Practical Realtime Strategies for Accurate Indirect Occlusion,
Equation 10](https://www.activision.com/cdn/research/PracticalRealtimeStrategiesTRfinal.pdf).
Material visibility affects ambient diffuse, diffuse environment lighting and
static local-probe bounce. Direct lighting and emission are unaffected. The old
seven-argument local-light helper remains compatible with callers having no AO
input. Inspection lighting retains its scalar AO and neutral-environment response.

The captured cubic matches the independent float32 formula across 66,049 CPU
samples before our upper clamp. Rounded coefficients overshoot one by .0001;
the engine bounds this to one to preserve the open-surface endpoint. This is a
near-field energy approximation, not new simulated transport. AO generation,
bent normals, dynamic rebaking and GTSO remain outside this pass. The existing
specular-occlusion approximation is a separate gap.

The current published station GLB has 28 materials and zero occlusion textures.
Its neutral AO input therefore gives no new crevice darkening in this pass.
The new response is verified with synthetic material AO, and ready for consumers
that supply it. Producing actual station contact visibility (baked or screen-space)
is still required; do not describe this as a complete contact-occlusion solution.

The capture establishes exposure conversion before SSR/environment blending.
Our planar texture already contains scene-linear radiance, so extra exposure
scaling would be incorrect. Research's 65,536 synthetic HDR samples reproduce
the blend arithmetic within 1.42e-5 of float64. These are component checks, not
full-frame or whole-renderer parity. SSR confidence/probe selection is not ported.

`PhlosionIndirectCompositionTests` renders 16 material/visibility patches per API
in separate bounce, direct-only and emission-only modes. An independent expanded
polynomial, Lambert response and ACES/sRGB calculation predicts each sampled
output within 2/255. Initial direct-only fixtures had reversed winding; correcting
it keeps the analytic threshold unchanged. Failed logs/images are retained.

Qualification results and frame-cost measurements are recorded in
[CHECKPOINT.md](CHECKPOINT.md), including engine, game and shared-consumer checks
and the unresolved Vulkan host-time/first-compilation limitations.

## Actor reflection oracle correction

The old game oracle compared the whole planar pass on/off near the mirrored
player anchor. Its baseline changed 1,451 pixels by over 3/255 there, but the
image showed broad receiver darkening, not an isolated character silhouette.
Correct radiance composition removed that darkening and exposed the false
positive. Failed images and measurements remain under the game's
`indirect-composition/actor-diagnosis.json` and `actor-delta-*.png`.

The replacement pair removes only Wes from the reflection draw, gated by game
automation. Main-view actors, shadows, lighting and scene state stay identical.
A grazing camera puts the body reflection in an existing authored puddle;
rain and bloom are disabled for isolation. Both APIs must change over 100 pixels
by more than 3/255 within independently projected mirrored body bounds, with
pixel-exact output outside those bounds. General front/oblique, precipitation-free,
dry and legacy controls remain. The numeric signal threshold is unchanged; the
control now identifies the actor rather than a whole-pass lighting difference.

## Cache retention discovered during qualification

The Vulkan disk cache was already 66,354,841 payload bytes, just under its former
64 MiB cap. Adding programs could exceed that cap; storage silently returned,
leaving later launches to recompile those programs. The bounded file/payload limit
is now 256 MiB, with an explicit diagnostic if it is exceeded. Native header,
vendor/device/driver/UUID, length and checksum checks remain unchanged. The CPU
contract now round-trips a 65 MiB payload through the same validated envelope.
This adds no GPU resource, but allows a larger optional disk/CPU cache. It does
not prune historical driver entries or remove actual cold compilation cost.
Native save/reload and launch durations are recorded with qualification results.
