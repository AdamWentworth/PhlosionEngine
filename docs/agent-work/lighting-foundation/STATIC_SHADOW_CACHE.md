# Static local-shadow cache

The optional `IWorldLocalShadowCache` capability supplies rigid-caster caching on
D3D12 and Vulkan. Existing interfaces, receiver packets and plugin ABI numbers
are unchanged. PCF and renderers without the capability retain all-caster draws.

An owner submits a nonzero scene identity and revision through
`drawWorldRectShadowsCached`. A miss draws only eligible fixed geometry into the
existing depth tile, then stores unfiltered radial distance and its square in
RG16_UNORM. Every frame draws fresh casters into a cleared depth tile. The
horizontal filter takes the nearer of fresh radial distance and cached distance
**before** forming/filtering the moments. The existing vertical filter and
surface/haze/reflection receivers are unchanged. Filtering already blurred
static moments together with dynamic moments would be incorrect.

Cache entries match the scene/revision, light index, atlas slot, projection,
resolution and distance coefficients. Atlas clearing resets published receivers
and pending reads without discarding completed cache images. Source removal and
slot compaction cannot silently reuse another light. Owners must advance the
revision for changes to any cached caster's geometry, placement, membership or
alpha coverage, including deletions. Scene identities must not be recycled.
Animated/skinned/wind-deformed casters stay fresh. An interrupted static draw
invalidates the cache. Explicit invalidation is available for reload/reset.

PokemonCyberpunk caches only its immutable, unskinned, unanimated field snapshot
in exploration and battle. Its key includes the resource scope, asset generation,
all field node transforms and excluded nodes. Characters, foliage, boundary
props and story actors stay fresh; interiors and calibration keep the prior path.
The asset generation covers reimported geometry and texture/alpha changes.
Future per-frame field-material overrides must participate in invalidation.
`PHLOSION_DISABLE_STATIC_SHADOW_CACHE=1` (or the game capture helper's
`-DisableStaticShadowCache`) retains the all-fresh comparison.
`PHLOSION_REFRESH_STATIC_SHADOW_CACHE=1` / `-RefreshStaticShadowCache` rebuilds
the raw cache every frame. This isolates temporal reuse from the additional
RG16 quantization. Reuse must be pixel-identical to this forced-refresh control;
differences from the old all-fresh filter are reported separately, never hidden
by relaxing the existing exact-image optimization guard.

The additional 2048 x 4096 RG16_UNORM image costs 32 MiB on D3D12. Vulkan uses
one separately fenced cache bank per frame in flight: 64 MiB with two frames.
Banks warm independently. Allocation occurs during local-shadow preparation;
the per-frame path allocates no targets. D3D12 transitions and Vulkan render-pass
dependencies order cache writes before filtering. The cache is renderer-owned,
bounded to eight tiles, and does not retain CPU meshes or released scene owners.

This implements the observed raw writer and minimum-before-filter operation.
The independent research writer matched 3,145,728 captured RG16 channels exactly.
Phlosion's eligibility, invalidation, replacement and scheduling policies are
conservative engine policy, **not recovered retail policy**. Live maps remain
1024-square and use the previously qualified integer addressing adapter. Neither
this integration nor the research component result establishes whole-renderer
or final-frame parity with Cyberpunk 2077.

## Verification commands

Build `PhlosionStaticShadowCacheTests` in `build-presentation`. Its CTest cases
cover CPU identity/invalidation and native cached/fresh steady, moved, removed,
dynamic, scene, projection, source-off, masked and reset cases on D3D12/Vulkan.
Each case also runs with forced refresh; these images must exactly match reuse.
For full image comparisons, retain each native run's stdout beside its PNG as
`<api>-<case>-<cached|fresh>.log`, then run:

```powershell
py -3.13 tests/verify_static_shadow_cache.py artifacts/lighting-foundation/static-shadow-cache/final-fixtures
```

The complete-image guard permits at most two display-channel steps and .01/255
mean difference for RG16 cache quantization; it also requires visibly effective
invalidation controls, fresh submissions every frame and actual warm cache reuse.
All 18 pairs are pixel-identical, as are all 18 forced-refresh pairs. Existing
moment, finite light, live atlas and haze tests remain unchanged. Eight real wet
station/battle reuse/refresh pairs also match exactly. The new quantization's
difference from the old all-fresh path, matched 1080p timings and an unresolved
one-step D3D12 variation in the first timing attempt are recorded in CHECKPOINT.md.
Retained repeats satisfy exact reuse/refresh comparison without changing limits.
