# Finite-source shadow projection

2026-10-18. Follow-up to the live distance-moment integration. This step recovers
the relationship between a finite light, its caster projection, the moment
filter's depth reconstruction, and the receiver's normalized radial distance.
It does not establish the renderer's complete shadow or lighting parity.

## Captured evidence

An owned readback-only replay of the retained retail 2.31 RT-disabled capture
records the caster camera constants alongside the existing filter inputs and
the cached moment array at events 6768, 15068, 23318 and 31591. Original exported
sources and resource data remain read-only in the private research depot.

For the updated layer, source length is 1.5 m, source range is 8 m, and the
projection scale is cot(60 degrees). The virtual viewpoint is 0.433012694 m
behind the source. The near plane equals that shift and the far plane is
8.433012694 m. The source plane therefore clips its housing. Depth reconstruction
uses far/near and 1-far/near; the receiver divides distance from that same
viewpoint by far. Adding the source dimensions again to far is incorrect for
this contract. The caster's camera-relative origin uses 1/131072 m fixed-point
units; its rounding is retained in the comparison.

The capture also contains shadowed light records with 150- and 160-degree
coverage. These are per-light projection inputs, not evidence of a single
universal angle. Their authoring/selection policy is not recovered here.
Only the 120-degree light's caster matrix is matched here. The other angles are
covered by public synthetic geometry tests, not additional captured caster maps.

The actual engine projection helper is compiled into a private probe and tested
against the captured matrix, filter constants and receiver equation at 10,000
fixed-seed points. Maximum absolute differences are:

| Quantity | Difference |
| --- | ---: |
| Caster matrix component | 0.0000013263 |
| Shadow UV | 0.000001101 (0.00113 texel at 1024) |
| Normalized clip depth | 0.000002348 |
| Reconstructed radial distance | 0.000000939 |
| Receiver radial distance | 0.000000182 |

These are coordinate/distance comparisons, not final image percentages or
bit-exact shadow rasterization. The original shadow array remains bit-exact;
later composition remains within the previously measured two-half-ULP replay
envelope. All 122 original exported source files retain their hashes.

## Engine integration

`rectLightShadowView` now uses the same inscribed line length as finite direct
lighting. For `FiniteLine`, offset = halfLength / tan(fov/2) and far = offset +
range. The existing 0.05 m near-plane floor defines very short lines and squares;
that guard is Phlosion policy, not recovered retail authoring. LTC retains its
rectangle projection policy. PCF and distance moments use the same projection
for a given source; changing the filter does not change the viewpoint.

The helper accepts a coverage angle, defaulting to the existing 150 degrees.
No per-light layout, GPU packet, plugin ABI, source geometry, color, radiance,
weather or moment-filter kernel changes. This adds no render pass or target allocation.
The game uses the change through its existing finite-source selection in
exploration and battle. The rectangle-to-line conversion and chosen coverage
remain explicit Phlosion choices, separate from the recovered projection rules.

Public CPU tests compare UV and radial distance with an independent geometric
oracle under rotation at 120/150/160 degrees, including the near-floor sphere
case, source-plane clipping, filter-independent matrices and invalid angles.
An initial fixed radial tolerance exposed cancellation for the 0.05 m near
plane at long distances. The final test bounds reconstruction error using its
depth derivative and float precision, and separately checks receiver distance
against geometry at a strict 3e-6 tolerance. The initial failure is retained.

The live receiver also now clamps radial distance to [0,1], as the captured
receiver does. A long source can illuminate a point near its end whose radial
distance from the virtual viewpoint exceeds the shadow range. An empty UNORM
map stores one; comparing an unclamped distance with it creates false occlusion.
The new `range-edge` native test reproduces this failure before the fix and checks
lit end regions against an unshadowed source control on each API. Bloom is off;
the lit-side ratio accounts for the display transform's channel mixing. The
initial assumption of equal displayed red/blue values was rejected because the
existing ACES matrices mix channels. Both initial fixture logs and the controlled
pre-fix range failure are retained. Existing image acceptance limits are unchanged.

## Static-cache evidence and remaining work

All four sampled 40 MiB cached arrays and caster constant blocks are byte-identical.
The captured commands/descriptors contain 33 direct references to that cache,
all read-only SRVs, with no command-list writes or transitions. The already
qualified filter merges the nearer cached and dynamic distances before filtering.
This establishes reuse of a populated cache in this interval.

The older single-frame export was also audited. Its candidate RG16 arrays have
descriptor bindings but no direct command-list resource references that reveal
cache writes. That audit is retained as `baseline-cache-audit.json`; it does not
provide the missing creation or refresh event.

The interval does not observe cache creation, eviction, invalidation, update
scheduling, or whether its initial data was baked versus rendered earlier.
Phlosion still renders its local casters every frame and supplies an empty cache
operand. That remains an open implementation and performance gap. To recover
retail policy, capture a local shadow layer being created or refreshed (for
example on entering a newly lit area), including its preceding writes. An
ordinary stationary capture with an already populated array cannot answer it.
Any engine-owned invalidation policy must be labeled as such rather than
presented as recovered Cyberpunk behavior.

The next implementation can use the observed static/dynamic merge with an
explicit Phlosion cache contract: retain only eligible rigid casters; invalidate
on projection, caster geometry/placement or alpha-material changes; render
skinned, wind-deformed and other dynamic casters every frame. Require cached vs
uncached image equivalence, moving-blocker/removal controls, scene and source
switches, and measured savings on all three APIs. Those invalidation rules would
be engine correctness requirements, not a recovered retail scheduling policy.

## Reproduction and live checks

Private scripts: `tools/shadow_matched/prepare_projection.py`,
`projection_probe/`, and `analyze_projection.py` in
`D:/Projects/Research/CyberpunkRainResearch`. Evidence is under
`evidence/shadow-projection`, including `qualification.json`, readbacks, source
hashes and build/replay logs. No retail bytecode or extracted assets enter this
repository. The public header hash and probe executable hash bind the numerical
comparison to the implementation.

Engine/game live evidence is under `artifacts/lighting-foundation/shadow-projection`.
The stable build passes 171 affected engine checks and six game CPU contracts.
All six station and six battle captures pass on OpenGL, Vulkan and D3D12 in
runtime and editor, together with the normal editor launch, seven scene controls,
seven reflection controls and four isolated-color captures. Existing acceptance
limits are unchanged. Maximum station API MAE is 1.077203/255; maximum
runtime/editor MAE is 0.00001370/255. Source-off pixels are identical to the prior
checkpoint, and scene environment, import and exploration inputs are unchanged.
Within the retained character regions, 1,271 pixels respond to each color.

The station image changes are small: D3D12 runtime MAE from the preceding
checkpoint is 0.039525/255. This is a projection correctness step, not a broad
artistic relighting. The explicit long-source fixture demonstrates the receiver
fix more clearly: its end region changes from RGB (0,0,228.1) to
(248.04,41.64,228.3). Vulkan and D3D12 are identical; OpenGL differs by at most
one 8-bit level. These are display-space checks, not light-energy measurements.

The first scene batch used a stale staged game DLL and was stopped; it is retained
under `native` and is not accepted evidence. Pre-clamp diagnostics are retained
under `accepted`; only `final` is the final game qualification. The rerun checks
built/staged DLL hash equality before capture. The generated receiver shader is
identical in all four engine/game build trees. Shared-consumer qualification and
remaining performance limits are recorded in the checkpoint.

Autochess's two runtime cohorts and two editor material cases pass on all three
APIs (six images per host), including the established expected-content and
baseline guards. Eight shared-material CPU contracts pass. The final strengthened
`range-edge` assertion also passes in a separate three-API rerun after moving its
sample fully into the pre-fix false-shadow region. No acceptance limits were
relaxed. First-use shader compilation still takes minutes; no new performance
measurement or speedup is claimed for this projection change.
