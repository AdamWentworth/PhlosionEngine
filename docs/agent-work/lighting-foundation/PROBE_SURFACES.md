# Persistent probe surface data and local relighting

Historical producer milestone. The subsequent [placed-probe integration](PLACED_PROBES.md)
connects ordinary game relighting, filtering, placement and quality scheduling.
The no-game-use statements below describe this earlier checkpoint. Linux native
qualification remains a release gate for the connected development path.

Baseline: engine 2eebc440195794fc6473f1036bc4dbb4bdc2929d and game
133f51133b4f6201b3c20364d6eb3b1807634e12. Implementation and qualification
run on gpu-worker; controller owns reviewed integration.

This is a C8 producer/local-consumer milestone. It does **not** replace the
game's six-view radiance capture, complete C8, establish a new parity score or
claim a frame-time improvement. There is no new player-facing toggle.

## Implemented

D3D12 and Vulkan can capture six persistent material/depth faces, retain them
across frames and primary/editor views, then select them for the existing
clustered local-light calculation. Changing lights does not require those
geometry draws again. The existing point/finite-area/LTC and local-shadow
calculation is reused; it is not replaced with an averaged light color.

The cache reserves four probe identities with two banks of six faces each.
A new revision writes the other bank. A completed face is necessary but not
sufficient for publication: all six faces must finish. An aborted face or a
superseded partial revision cannot replace the last complete generation.
Selecting a face restores its captured projection, dimensions, material
planes and depth. Frame-owned primary surfaces cannot overwrite these slots.
Each selection reserves separate per-frame lighting constants/output slots.

Captures use power-of-two face sizes from 16 through 512, bounded by the active
host target. A 128-square probe in a 256-square host is qualified. D3D12 probe
captures own a depth attachment instead of borrowing a frame snapshot; Vulkan
uses its existing surface target depth. Reuse barriers include compute readers.
Release/reassignment is allowed outside a recorded frame, after submitted GPU
work finishes. Resolution changes require release; scene/material changes use
a new revision. Resize/device resource recreation invalidates residency.
Allocated slots remain reusable until renderer teardown rather than growing
with revision numbers.

The stored format currently reuses all six shared-surface planes, including
identity and motion, plus depth: 32 bytes per texel before allocation padding.
A fully populated four-probe/two-bank cache is 96 MiB at 256-square faces
(384 MiB at 512). It is a bounded implementation foundation, **not** the source
packed eight-byte material encoding or a finished shipping memory budget.
No resident probes are allocated by current ordinary gameplay.

Callers must capture dry/static material inputs. Animated actors/wind, weather
material changes and emissive-state treatment are not implicitly made safe by
storage; they need the producer/relighter integration listed below.

## Source-to-implementation mapping

The local research trace is
PROJECTS_ROOT/Research/Cyberpunk2077Research/lighting/reflections/SOURCE_RENDER_CALL_PATH.md.
In the inspected source snapshot, renderReflectionProbeManager.cpp
(CEnvProbeRelighter::RelightProbe, ProcessSingleStage_Relight and
HandleUpdateRequests) consumes resident material probe data, processes bounded
face batches and advances through filtering/publication stages.
reflectionProbe.fx PROBE_RELIT reconstructs positions and evaluates lighting
from stored surface records. The relevant distinction is retained material
inputs with fresh lighting, rather than throttling old fully lit captures.

This change independently implements retained material/depth ownership,
complete material-generation publication and reuse through Phlosion's existing
local-light consumer. It does not claim the source packing, its complete
relighter, runtime policy, or authenticated present-day retail equivalence.

## Qualification

Final optimized RelWithDebInfo engine/editor and game/runtime/plugin builds
passed on Windows gpu-worker, using the EVGA RTX 2060 SUPER selected by LUID
000000000000be8e (UUID GPU_1_UUID).

- Two CPU suites passed: engine contracts and probe cache lifecycle/publication.
- 38 final native GPU checks passed: 16 retained/fresh probe cases, 12 clustered
  lighting cases and 10 shared-surface cases, split equally across D3D12/Vulkan.
- All eight retained-versus-fresh probe image comparisons were pixel-exact.
  Faces have different projections and material colors. Current point and area
  lights change while stored materials remain resident. Both 256-square and
  128-square-in-256-host captures pass material/depth and culling checks.
- The ten-frame replacement fixture draws 13 geometry faces (including one
  aborted writer), compared with 60 for the fresh oracle. The cancelled-only
  fixture draws 10 and retains the older complete generation. This is a
  correctness workload, not an FPS benchmark.
- The flight case suppresses diagnostic waits until the last frame, apart from
  the requested presentation capture at frame 8. It tests reuse and bank
  replacement while earlier work can remain queued.
- Twelve first-revision runtime/editor wet/dry/orbit/battle cases passed against
  the committed shadow baseline. Six final-revision runtime wet/battle and
  editor wet checks passed against those captures. Maximum image difference:
  1 eight-bit channel step(s). No validation errors were detected.
- The game still uses its existing production reflection path. These scene
  checks establish preservation, not game integration of the new relighter.
  Linux qualification and clean performance timing remain open.

Worker evidence: WORKER_EVIDENCE_ROOT/phlosion-probe-relighting-20261027, including
build.json, r2/checks.json, scenes-r1.json, scenes-r2.json, qualified-source.json,
candidate/files.json, source manifests and logs/captures. Superseded build
failures and first-revision results are retained; they are not passing evidence.
The final build includes the projection/in-flight tests and render-pass guards.


## Required next connection

1. Produce dry static material faces at authored probe positions in field/battle;
   define material/emissive/weather inputs and dynamic-object participation.
2. Complete relighting composition: sky coverage, sun/shadows, ambient/neighbor
   specular response, emission and fog, with current weather applied after load.
3. Keep a separate completed radiance generation while relighting a bounded
   face batch; convolve after all faces and publish only the completed result.
4. Select/blend placed probes spatially, manage residency and prioritize updates
   under Low/Medium/High/Ultra budgets. Do not ship stale geometry or freeze
   time-of-day response as a substitute.
5. Replace ordinary per-view radiance geometry capture only once both Windows
   backends and Linux, field/battle/editor, moving lights/actors and weather
   have been qualified. Then measure clean GPU timings.
