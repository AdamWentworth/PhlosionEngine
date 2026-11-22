# Shared wind completion contract

Status: implementation and pre-integration qualification complete.
**100% estimated coverage of fixed G1-G8 for ordinary Windows** is accepted only
when the locked-source, publication and controller-installation receipts below
pass. This does not claim identical retail pixels, random sequences or FPS.

Scope: ordinary Windows D3D12/Vulkan, runtime/editor, Low/Medium/High/Ultra.
The checklist is fixed before implementation; milestones do not close the category.

- G1: one simulation-owned prevailing flow, independent weather/storm controls,
  smooth strength/direction changes, seeded gust rise/hold/fall, accumulated travel.
- G2: integrated scalar and directional phase for captured foliage curves; current
  and previous snapshots, coherent rain/cloud movement, pause/reset/interpolation.
- G3: bounded local sphere/capsule disturbances, source lifetimes/clustering,
  persistent advected velocity/density volume, translated camera volume and edges.
- G4: the ordinary plant path consumes local disturbances with anchored bending;
  no duplicate simulation for shadow/probe views. Shared sampling contract for
  future character/hair and dust consumers; their actual rendering is separate.
- G5: authored wind shelter and local control integration. Distinguish this adapter
  from the recovered volume, which does not trace building occlusion.
- G6: independent numerical and lifecycle checks, frame-rate/camera changes,
  overlapping/expired disturbances, invalid input and bounded work/storage.
- G7: ordinary runtime/editor and both APIs, all presets, pause/weather/scene
  changes, deformed shadows and motion history, prepared shader paths.
- G8: measured optimized CPU/GPU costs, final dependency locks, publication and
  controller integration verified. Do not trade away existing Hybrid impacts.

Reference trace (private source stays outside these repositories):
- renderData windParameters: slow condition-direction adaptation, strength
  override, integrated cloud travel.
- renderer speedTreeWindSimulation: normalized strength /20, registered per-asset
  simulation once per tick, current/previous state, views never tick the producer.
- SpeedTree v8 state manager: strength/direction response, gust envelopes, 20-point
  authored curves, scalar AND directional noise phase accumulated using dt.
  The older engine speedTreeWind.cpp is not the active v8 path.
- renderer windImpulseCollector/renderNode_WindImpulseVolumeUpdate and
  shaders windImpulseVolumeUpdate/include_windImpulse: 64 cubed over 32 metres,
  0.5 metre cells, camera +8m forward/+4m up, snapped scrolling origin, 4 cubed
  clusters, 16 shapes of each type/cluster, up to 255 spheres and 255 capsules, Euler backtrace, density
  gradient/velocity divergence/viscosity, linear decay, bounded density/velocity,
  four-metre fourth-power edge fade. Mirrors/subscenes do not advance simulation.
- The inspected source currently executes one fluid step per frame despite a
  commented fixed-step loop. A deterministic simulation-clock correction must
  be documented as an adapter, not represented as identical scheduling.
- This work independently implements the mechanisms. It does not import the
  proprietary foliage middleware or claim retail pixel/frame-time identity.

Not this category: complete foliage material/LOD shading, actual sandstorm
particles/audio, replacement human hair authoring/physics, Linux qualification.


## Implementation and adapters

WorldWindController owns the simulation clock, prevailing velocity, seeded
rise/hold/fall gusts, slow heading changes, accumulated travel and the 20 scalar
and directional curve integrals. Foliage evaluates its existing authored curves
against those integrals. Rain and clouds consume the same flow; weather and rain
intensity do not implicitly select wind strength. Settings can transition without
restarting phase, including transitions between fractional simulation ticks.
Pause preserves phase; resets and large offline discontinuities invalidate history.

The native field has current, previous and scratch 64-cubed volumes, one update
before ordinary geometry, and immutable bindings shared by camera, shadow and
probe consumers. CPU admission limits both sphere and capsule types independently
to 255 shapes globally and 16 of each type per four-cubed-cell cluster. Shapes,
cluster lists, scope residency and catch-up work are bounded.

Explicit differences from the inspected reference:
- RGBA32 structured-buffer storage and manual trilinear sampling replace the
  reference RGBA16F 3D texture. Three volumes use about 12 MiB; input uploads and
  allocation overhead are additional. This is a storage/API adapter.
- Fluid steps follow the simulation clock at 0.016 s with at most eight catch-up
  steps; a larger discontinuity resets history. The inspected source executes
  one fixed fluid step per rendered frame. Our scheduling avoids frame-rate
  dependent evolution and does not pretend to be identical scheduling.
- Ambient gust timing uses our deterministic seed sequence with the recovered
  smooth envelope/response mechanisms. It is not the proprietary middleware's
  exact random sequence, asset state or byte-for-byte runtime.
- Ordinary plants use an anchored, bounded height-weighted local bend with normal
  and tangent adjustment. The complete original foliage consumer graph has not
  been recovered; this is a consumer adapter. Wider leaf shading/LOD remains its
  own category.
- Game-owned shelter bounds are derived from the imported closed bar shell.
  Upstream ray/box attenuation and an eight-metre wake are authored behavior.
  The original local volume does not itself trace buildings. The open canopy
  is deliberately not treated as a sealed building.
- A persistent player capsule adds local displacement; its size, anchor and
  intensity are game tuning. The shared API accepts other sphere/capsule sources.
  No replacement hair or Pokemon deformation is claimed.
- Normal Low/Medium/High/Ultra all use the bounded field. Their existing foliage
  visibility/LOD budgets remain in force; no user-facing experimental opt-in exists.

D3D12 freezes the field root SRV with each prepared draw packet. Vulkan uses a
dedicated immutable eighth descriptor set: changing the field cannot invalidate
earlier world/prewarm commands that used another set. Scope retirement waits for
GPU completion before reusing that scope's resources and descriptors.

## Numerical and lifecycle evidence

PhlosionWorldWindTests covers deterministic gusts, integrated scalar/directional
phase, 15/30/60/144 Hz flow, pause/reset, fractional transition continuity,
invalid inputs, independent shape budgets, clustering and bounded updates.
The native test independently checks sphere/capsule equations, transient ages,
overlap, velocity/density evolution, previous-state preservation, scrolling,
paused updates, teleport reset, all quality values, duplicate-producer rejection
and scope retirement. Both APIs pass with validation enabled.
Frozen-binding command-recording tests include the new D3D12 root and Vulkan set.

The game CPU suite exposed old test fixture assumptions: an obsolete editor ABI,
OpenGL camera-harness default and a battle recorder missing normal weather/
lighting capabilities. These fixtures were updated while retaining their existing
flow/pause/order assertions. Four audio checks require the prepared Rain Dance
fixture; one optional cooked-model check remains skipped because its fixture is
not installed on the worker. Final passing reruns and original failures are retained.




## Ordinary qualification and measured cost

The 22 runtime/editor/API/preset/cycle/scene-return checks pass: D3D12 matrix
and the corrected Vulkan matrix2. The earlier Vulkan matrix exposed mutable
descriptor invalidation; its failed receipts are retained and superseded by
matrix2, not counted as passes. Four corrected close-plant views (Vulkan close2 and D3D12 close3) exercise
the ordinary consumer and motion readback. Four strict checks require prepared
programs with fresh application caches across both hosts and APIs.

The game suite has 53 passing tests across the original run and corrective
reruns; one optional cooked-model fixture is skipped. Five focused engine CPU
groups, the final continuity check and frozen draw-recording checks pass.
Native-final-d3d12 and native-immutable-vulkan retain the independent field
oracles and validation logs.

Optimized RelWithDebInfo, EVGA GPU1, 1920x1080 Medium. Each timing uses frames
121..239 (119 samples). Heavy results average two medians in alternating
before/candidate/candidate/before order; dry results are paired medians.
API validation/readback runs are separate from timing runs.
| API | Dry before | Dry current | Heavy before | Heavy current |
| --- | ---: | ---: | ---: | ---: |
| d3d12 | 13.737 ms | 13.924 ms | 17.548 ms | 17.451 ms |
| vulkan | 14.416 ms | 14.573 ms | 17.668 ms | 17.684 ms |

The later same-clock reset correction changes only CPU initialization/history
identity. Its new regression and final locked-host checks qualify that edge case;
these timing runs retain the preceding payload with the same steady-frame path.

These numbers measure the completed wind addition, not a promise of improved
frame time or proof of retail performance parity. Raw phase profiles separate
the wind producer from other passes. No rejected/dropped timestamps or sampled
thermal-slowdown runs are accepted in this table. Hybrid surface-specific
splashes and actual puddle rings remain selected.

## Locked release acceptance

Editor ABI61 and model ABI33 require matching engine and game providers.
The matching shader publication is scene-shaders-v1/2026-11-04-wind.
Its exact program count, bytes and publication hash are in shader-publication.json.

Final acceptance requires:
1. Reviewed worker delta integrated into the unchanged controller baseline.
2. Game lock pinned to the canonical engine commit; clean matching worker source,
   locked dependencies and warnings-as-errors enabled.
3. Final optimized builds and runtime/editor D3D12/Vulkan checks with prepared
   programs required and no API validation errors.
4. Clean-source editor publication and shader package, with verified hashes when
   installed on the controller.

The worker receipts are locked-qualification.json, editor-publication.json and
shader-publication.json in WORKER_EVIDENCE_ROOT/phlosion-wind-20261104. The controller
installation receipt is in the corresponding PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/wind-20261104.
The completed category is conditional on these receipts; missing or failed
release receipts must not be represented as accepted.
