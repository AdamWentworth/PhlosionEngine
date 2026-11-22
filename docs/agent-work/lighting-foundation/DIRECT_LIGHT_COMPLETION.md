# Direct-light completion contract

2026-11-01. Status: D1-D8 qualified for the supported ordinary Windows path.
Coverage: 8/8 gates, **100% estimated within the stated unprofiled local-light boundary**.

This category covers the ordinary non-RT Windows vertical slice's direct
surface lighting. Completion requires every D gate below on D3D12 and Vulkan.
It does not assert matching retail images, all REDengine shaders, or performance.
The source snapshot and earlier captured shaders are different reference versions:
native sources use the snapshot's fixed 0.0002 attenuation tail; the historical
captured FiniteLine helper retains its intensity-dependent tail.

| Gate | Required evidence |
|---|---|
| D1 Producer | Explicit source radius, capsule length/axis, direction, cone, linear intensity and roughness bias; validated packing, fixture identity and physical alignment. |
| D2 Attenuation | Point distance and finite segment distance; inverse-square/linear falloff, optional cap, fixed cutoff, cone softness and directional variant. |
| D3 Opaque response | Actual normal/view/albedo/metal/F0, energy-corrected diffuse, correlated GGX, finite representative point and lobe normalization. |
| D4 Transparent response | Separate Lambert/approximate specular variant, same source shape and energy; finite degenerate cases. |
| D5 Dispatch | Same direct lobes in forward, clustered and reflected surfaces; conservative finite-source bounds; channels, participation, diffuse/specular and shadow visibility applied once. |
| D6 Fixture integration | Normal station uses explicit capsule sources; emission and gameplay gain stay synchronized; no extra lights, quality cap, or experimental renderer switch. |
| D7 Material integration | Ordinary metals/dielectrics/wet surfaces, transparent surfaces and existing layered-character diffuse adapter; the adapter must remain explicit. |
| D8 Qualification | CPU contract tests, independent numerical references, native shaders, ordinary dry/wet/orbit/battle/editor coverage, refreshed prepared shaders and clean validation. |

Scope boundaries are recorded before acceptance: arbitrary IES/projector texture
producers, specialized CP2077 skin/hair/eye BRDFs, volumetric light integration,
global sun/sky shading, GI and shadow generation/scheduling are separate work.
Unprofiled lighting means the neutral unit IES sample, not a claim that an IES
atlas exists. Existing Pokemon character materials intentionally preserve their
authored specular model. These boundaries remain visible in the matrix; D1-D8
completion must not be presented as 100% of every Cyberpunk direct-light feature.

Reference anchors (private source is not redistributed):
tiledDeferredHelpers.cpp::PackPointLight/PackSpotLight/PackAreaLight;
include_lightingHelpers.fx::CalcLightAttenuationAndShadow,
CalculateLightingComponents_Standard; include_lightingModel.fx::BRDF,
BRDF_Transparents; active clustered sphere/capsule packing in
clusteredDeferred.cpp. Source hashes and inspected ranges accompany evidence.


## Connected implementation

WorldLocalLightSource carries a unit axis, explicit sphere radius/capsule
length, roughness bias, optional attenuation cap, inner/outer cone angles,
softness, intensity scale, inverse-square/linear and directional controls.
Producer validation rejects non-finite, degenerate and out-of-range inputs.
Game fixture validation also requires the capsule to lie in and fit both axes
of its luminous rectangle. The published station now authors those inputs.

The active reference clustered packer selects sphere/capsule sources rather than
its optional LTC branch. Phlosion's normal station now selects the explicit
finite-source response, leaving historical LTC/captured-FiniteLine adapters
available for old profiles. The current native cutoff follows the source
snapshot, not an average of incompatible versions.

Point/spot and sphere/capsule inputs share the source-native attenuation and
material response. Opaque surfaces use the energy-corrected diffuse and correlated
GGX lobes; transparent consumers use their distinct Lambert/approximate-GGX
variant. Standard F0, metallic response, roughness bias, channels, lobe masks and
participation remain live. Forward and clustered dispatch use the same evaluator.
Probe/reflected surface paths inherit it through the shared direct-light include.
Layered Pokemon materials retain their documented diffuse-light adapter and
authored specular; this is not CP2077 skin/hair/eye material equivalence.

The physical rectangle still owns fixture geometry, facing, transport and shadow
projection. Source dimensions do not silently resize the mesh. The existing
representative shadow projection now reads native capsule length, preserving its
previous matrix when dimensions are unchanged. Shadow filtering/caster coverage
is a separate category. Native intensity scale also reaches the existing diffuse
bounce producer, avoiding a direct/bounce brightness mismatch.

Six station luminous meshes were independently checked from GLB vertex positions
and world transforms. Their source extents match the actual faces within the
intentional approximately 1 mm source-plane offset. Fixture colors/emission,
health/flicker state, geometry and gameplay schedules are unchanged. The normal
profile converts its established face-radiance units using emitting area as
intensity scale; the generic engine accepts producer-scaled linear intensity.
No new samples, light counts or quality reductions substitute for the math.

## Representation and compatibility

Point packets grow from 8 to 20 float4 rows and area packets from 64 to 112.
Original payload rows retain their ordering; native metadata follows them.
Clustered/indirect-receiver packets and all GLSL/HLSL/Vulkan view offsets are
updated together. The extra world-view uniform storage is 960 bytes.
Editor ABI50/model ABI28 reject stale binary consumers. PokemonModelSupport
requires a rebuild against the new engine headers but has no task source edits.
OpenGL declarations are maintained only; OpenGL and Linux are not qualified here.

The ordinary renderer remains the default; Low/Medium/High/Ultra retain their
existing budgets. Prepared shader publication must be regenerated after this
shader/interface change. Normal launches never read the reference source or
compile from the research depot.

## Numerical acceptance

The new compute fixture uses independent float64 aligned-point references for
32 opaque/transparent, metal/dielectric, inverse-square/linear, cap, cone and
roughness-bias cases per API. The existing finite fixture retains its 16 numeric
and 16 degenerate-domain cases. The existing mixed direct fixture retains 4,096
frozen reference cases and light-reordering checks; old packet rows explicitly
select their historical response so that the oracle is not silently redefined.

At the smooth GGX peak, two float32 normalization ULPs can move the result
appreciably. Acceptance retains the float64 ideal and an analytically bounded
input-rounding envelope, plus 0.3% and 2e-5 arithmetic tolerance; it does not
replace expected values with observed GPU results. Absolute and relative errors
are retained in final native logs. Legacy mixed-light normalized error is below
1.1e-7 on D3D12.

Native forward/cluster images cover opaque and transparent receivers, channels,
diffuse-only, specular-only and disabled sources on both APIs, under the existing
one-byte/exact-image tolerances. CPU coverage includes last-slot packing,
disabled/invalid metadata, source-to-shadow projection, direct/bounce scale and
cache invalidation after source mutation. Normal host acceptance closes D8 below; synthetic results alone do not establish
scene or performance parity.

## Remaining differences

Neutral unprofiled IES, explicit point/capsule representation, fixture radiance
conversion and the Pokemon layered-material adapter are intentional boundary
choices, not recovered arbitrary-projector or specialized-character systems.
The source snapshot is not authenticated as the current retail revision.
No paired retail imagery or matched retail FPS benchmark is available.
The last whole-frame target (16.67 ms), GI cost, night character readability,
sun/sky, wet-material evolution, volumes and broader material systems retain
their separate roadmap entries.


## Qualification and handoff

All execution was remote on gpu-worker, EVGA RTX 2060 SUPER selected by LUID
000000000000be8e, Windows, optimized RelWithDebInfo (optimized shaders).
Canonical source was reviewed/integrated on controller. No local GPU fallback,
local inference, driver changes or retail source compilation was used.

| Gates | Evidence |
|---|---|
| D1 producer and fixture mapping | source-unit/packing hashes; fixture-geometry.json; authored-source-check.json; native/game CPU tests; updated canopy exporter |
| D2-D4 attenuation and material response | final-native-numerical logs; native-results-r5 finite and 4096-case frozen direct fixtures; native transparent image pairs |
| D5 dispatch and consumers | native-results-r5 forward/cluster images across six modes per API; cache mutation and bounce-scale checks |
| D6-D7 normal fixtures/materials | final-content.json checks all six native metadata records in 12 field cases, and fixture gains/weather in four battle cases; rendered content and review |
| D8 qualification/startup | final-build.json, final-cpu.json, asset-unit.json, final-qualification.json, strict prepared-program host and scene-switch checks |

The final matrix includes 16 ordinary cases: runtime and editor on D3D12/Vulkan,
each at wet Medium, dry Low, orbit High and battle Ultra. Two additional strict
station/reference/station transitions pass. They require prepared shaders and
report zero runtime HLSL compilation misses and no API validation errors.
The battle handshake omits rectangle rows; it does not provide independent
packet readback there. Shared source-producer CPU tests, recorded fixture gains
and rendered battle content qualify that consumer. The initial 16-host capture
suite is retained separately; final host acceptance
uses the later shadow-projection and bounce-scale fixes.

The prepared package contains 62 programs. It and the explicit-source
station profile are independently hash-pinned publications. The unchanged
station GLB is not regenerated. Asset package tests and offline hash verification
pass. Final-content.json records preserved rain/wetness/puddle state and startup
observations; visual-review.json records inspected frames and limitations.

The first uncached preparation of the changed shaders took substantial developer
time (about 756 seconds to first frame on D3D12 in the initial capture run).
The refreshed publication removes normal HLSL compilation. Vulkan also incurred
first-use driver preparation for the changed program; those costs are retained
in the initial logs. Final strict application-cache checks occur after driver
warmup. No driver-cold startup or steady-FPS improvement is claimed. The 16.67 ms
whole-frame target is still open.

Unsuccessful build/fixture attempts are retained, including a stale flag
expectation, HLSL fixture global storage binding, glossy-peak float32 rounding
analysis, a mistyped build target and the worker's linked-data containment check.
The latter was resolved through the established isolated package host without
weakening package validation. None is counted as passing evidence.
The final numerical fixtures also exercise range-cutoff continuity, capsule
distance, nearest-segment cones and directional overrides.

Evidence is retained under
artifacts/lighting-foundation/direct-lighting-20261101/evidence.zip.
Source receipts contain hashes/function locations, never reference source text.
