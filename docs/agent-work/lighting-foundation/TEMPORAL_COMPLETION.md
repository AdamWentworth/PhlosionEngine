# Motion and temporal reconstruction — 2026-10-31

**100% estimated coverage of the fixed T1-T8 contract**, verified for the supported normal Windows D3D12/Vulkan path.

## Fixed boundary

The T1–T8 acceptance contract was recorded before implementation in
acceptance-contract.json. It covers the ordinary supported Windows D3D12/Vulkan
field, battle and editor producers and C1–C4 temporal consumers. It does not
assert entire retail-engine identity, matched retail frame time, Linux qualification,
hardware RT, DLSS/upscaling, motion blur/DOF, new hair/vehicle gameplay or unimplemented
source material families. The supplied source snapshot is not authenticated as
the current retail revision.

## Connected changes

- Motion history owns previous CPU-deformed positions as well as palettes, wind
  parameters and rigid transforms. Explicit instances use authored identities;
  reorder, culling, topology, resource and temporal-class changes cannot borrow
  a different object's history. Positions have a per-view vertex budget.
- The D3D12 serial instancing fallback now draws anonymous geometry and selects
  each instance's previous record. Cached native instancing remains the normal
  fast path. A single-instance batch uses the same palette policy for its
  motion record and actual geometry.
- Temporal material policy is separate from exact object identity, material BRDF
  and receiver light channels: geometry, character, vehicle, reduced history and sky.
  Surface identity keeps its two integer words; its second word holds light
  channels in the low byte and temporal class above it. Lighting masks that byte.
- Both APIs carry camera/depth/sky constants in one 128-byte temporal packet.
  Actual jittered camera matrices drive sky rotation/projection motion;
  camera translation cancels for an infinitely distant sky. Native battle
  models now share glTF's primary projection jitter, without applying it to
  auxiliary views.
- Ordinary camera-enrolled views use nearest-depth motion, local linear-depth
  ranges, previous class, stronger vacated-character/vehicle rejection, and
  4×4 invalidation dilation. Exact IDs remain for ownership and consumers, while
  camera-less diagnostics retain their conservative exact-ID/depth rule.
- Reduced-history surfaces add the reference current-frame weight. Coverage
  tracks current and departing transparent effects. Previous stored coverage
  is already scaled and is no longer scaled a second time. Empty texels do
  not reject the whole effect card.
- Held and flying Poké Balls join the existing collected geometry family once
  per rendered frame, with authored track identities and fixed placements.
  Admitted primary/shadow/reflection views consume the collection; particles
  remain transparent. Dynamic-shadow content includes these prop records.
- Reconstruction retains neighborhood bounds, cubic history sampling,
  time/luminance/motion weighting, exposure compensation, HDR compression,
  displayed composition and filtered half-resolution N−1 reflection feedback.

## Source correspondence and adapters

source-audit.json records SHA-256 and line anchors for the inspected
render-frame, motion initialization, temporal invalidation and TXAA call paths.
Phlosion code is independent; source assets or headers are not dependencies.

The active ordinary class/depth heuristic is connected, rather than replaced
by strict instance equality. Conventional Z, top-left UVs, actual jittered
matrices, exact integer identity, RGBA32F depth metadata, half-float color/masks
and API-specific bindings are explicit adapters. The source driving-only
depth/velocity heuristic is not active for this game's non-driving views.
The source anti-aliasing flag maps to the generic reduced-history material.
Existing alpha/additive billboards and trails deliberately use reactive
coverage instead of overwriting opaque motion. No opaque billboard producer
is currently authored. ModelInstance's defensive CPU-skin fallback can now
supply owned positions; ordinary supported skeletal models use GPU palettes.

Temporal class adds one integer word to each half-resolution history identity
image. At 1920×1080 the complete temporal/feedback texel payload is 71,883,648
bytes (68.55 MiB), up from 67,736,444 bytes (64.60 MiB): +4,147,204 bytes.
This excludes allocator alignment, shared surfaces, SSR and readback.
CPU-deformed positions are capped at 1,048,576 vertices per view, with at most
eight views; current authored GPU-skinned/rigid producers allocate no position arrays.

## Qualification

| Gate | Implementation and verification |
| --- | --- |
| T1 | WorldMotionHistory/WorldMotionGpu own rigid, palette, wind and CPU-position history with authored instance identities. WorldMotionHistory CPU and WorldMotionProducersNative verify reordering, missing/changed producers, CPU topology, cached/uncached instances and one-instance palette semantics. Collected held/flying balls retain authored identities and join the normal view family. Native project motion covers skinning and wind. |
| T2 | WorldTemporalCameraConstants and ModelRenderProvider share actual jittered primary projection. CPU matrix and native sky rotation/translation/jitter, native model projection, missing/cut/resize/behind-camera fixtures verify the conventions. Auxiliary views do not promote history or inherit primary jitter. |
| T3 | WorldTemporalMaterial is independent of exact identity, surface BRDF and low-eight-bit light channels. Producer/native policy cases verify geometry, character, vehicle, reduced-history and sky rules; material changes invalidate ownership. Both clustered consumers mask light channels explicitly. |
| T4 | The shared temporal shader connects nearest-depth velocity, ordinary linear-depth range/class rejection and 4x4 dilation. Both native policy suites independently verify near/far rejection, character/vehicle boost, local depth ranges and a moving foreground silhouette with isolated disocclusion and nearest velocity. |
| T5 | Native analytic alpha-card, empty-texel, first-appearance/removal, exposure/reset and HDR tests pass. Both APIs preserve four battle afterimages; wet scene/preset pairs and reviewed arrival captures cover actual effects. Previous reactive coverage is no longer scaled twice. |
| T6 | WorldTemporalNative and TemporalPolicyNative verify numerical history/current weighting, reduced-history policy, neighborhood/cubic reconstruction, HDR/exposure, displayed composition and filtered N-1 reflection feedback. Normal scenes retain previous-frame reflection input without fresh source geometry. |
| T7 | Both normal Windows hosts pass field/battle scenes; runtime also dry/orbit/Low/Medium/High/Ultra. Native prepared-view/resource retirement and Vulkan submission regressions preserve fenced ownership and prior P/W behavior. Temporal views and CPU positions remain bounded; no renderer opt-in added. |
| T8 | 23 CPU groups plus private foliage arithmetic, 17 final temporal/lifetime native executions, 52 shadow native executions and four screen-feedback executions, 18 scene pairs, two four-afterimage pairs and eight runtime/editor arrival captures pass. Visual review, 16 optimized matched cost runs, memory accounting and exact payload manifests are retained with failed attempts. |


All eighteen baseline/candidate scene pairs and both four-afterimage pairs were visually reviewed. Differences follow temporal edge reconstruction, moving rain and reflected detail; no missing scene geometry, lighting discontinuity, opaque trail rectangles or obvious persistent smearing was seen. Native wet/dry/orbit and editor/battle views retain the authored station, actors, lighting colors, rain and puddle detail. Required native analytic tests supply numerical rejection/coverage checks; screenshots alone do not establish temporal correctness. Eight final runtime/editor held-ball and airborne-ball captures were also reviewed: the ball is present at the expected placement, without an obvious duplicate silhouette or opaque effect-card artifact. All complete openings pass API validation after the shadow-view and Vulkan feedback ownership fixes.

Failed and superseded attempts are retained. A two-pixel alpha texture initially sampled its bilinear
transition rather than the intended solid/empty interiors; the final analytic
test samples interiors of an eight-pixel card without changing tolerances.
A one-pixel depth fixture initially collapsed to one depth at half resolution;
two-pixel bands preserve the intended independent depth range.
The staging helper's old task root was corrected before scene qualification;
the preceding immutable payload was hash-verified and its mutable staging
copy/metadata restored. This did not affect canonical source.

Evidence root: WORKER_EVIDENCE_ROOT/phlosion-temporal-completion-20261031 on gpu-worker.
The canonical handoff retains the tested payload and final locked dependency evidence.

The full battle-opening capture exposed a receiver-view boundary bug: the field
snapshot and first battle view share a host frame, leaving local-shadow allocation
state from the first view. Shadow admission now resets allocation/read state for
the next receiver while retaining completed maps. CPU sequential-view/scene tests
and fresh-versus-scheduled native multi-view images pass; the 48 existing shadow
regressions were repeated. Actual held/flying ball openings are checked in both
hosts and APIs. No phase guard was removed.

The replay file and prepared SV ball were initially absent from the benchmark
fixture. Their failed runs are retained. The corrected opening fixture uses
byte-identical scene/opening/ball manifests from the complete worker checkout
and verifies the model publication hash; it does not alter canonical assets.

Vulkan validation also caught same-frame screen-feedback descriptor reuse during
the field-to-battle transition. Each presentation now owns one bounded descriptor
pair until the host-frame fence completes; no update-after-bind relaxation is
used. Single-view and seed-plus-presentation native feedback checks pass on both
APIs, followed by the complete runtime/editor opening captures. The original
validation failure remains in the evidence archive.

## Matched optimized cost

RelWithDebInfo, native 1920x1080 Medium, EVGA GPU1 on gpu-worker. Sixteen ABBA
runs provide 140 steady frames per run; validation/readback are disabled for
timing. Paired renderer/weather settings, precipitation state and telemetry are
checked. These are baseline/candidate Phlosion measurements on the same GPU.

| API / scene | GPU mean ms | CPU frame mean ms | Active CPU mean ms |
| --- | ---: | ---: | ---: |
| d3d12 / wet | 15.894 -> 16.161 | 15.807 -> 16.095 | 8.730 -> 8.910 |
| d3d12 / battle | 16.358 -> 16.822 | 18.402 -> 18.578 | 15.693 -> 15.715 |
| vulkan / wet | 17.511 -> 17.420 | 17.323 -> 17.235 | 4.884 -> 4.830 |
| vulkan / battle | 17.102 -> 17.051 | 17.046 -> 17.107 | 8.779 -> 10.126 |

The final timing images were visually reviewed and preserve the same scene, lighting and precipitation. D3D12 GPU mean increased by 0.267 ms for wet field and 0.464 ms for battle; Vulkan GPU mean changed by -0.090 and -0.050 ms respectively. Vulkan changes are small and are not presented as a demonstrated speedup. D3D12 CPU frame means were 16.095/18.578 ms and Vulkan 17.235/17.107 ms for wet/battle. Vulkan battle active-CPU accounting rose by 1.347 ms while its complete CPU-frame mean rose by 0.061 ms; this remains a performance follow-up, not an attributed cause. All 16 runs passed matched settings, precipitation, profiling and telemetry checks; peak selected-GPU temperatures were 61-64 C with no thermal-throttle flags. The completion is accepted for the fixed technique scope, with these costs retained; sustained 16.67 ms whole-frame delivery and cold shader preparation are not closed.

The source snapshot defines technique correspondence, not measured retail
pixel/frame-time identity. The fixed scope excludes Linux release qualification,
new material/character systems and separate rendering categories. Sustained
16.67 ms and cold shader preparation remain separate acceptance targets.
