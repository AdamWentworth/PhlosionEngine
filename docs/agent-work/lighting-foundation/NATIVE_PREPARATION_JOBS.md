# Admitted native actor preparation jobs

2026-10-30. Baseline engine 865a2e2, game 1ce101f, ModelSupport da6a969.
All implementation, builds, tests and captures ran on gpu-worker Windows.
controller owns review, integration and commits.

## Behavior

Battle runtime/editor now admit the required native actor and afterimage views
during view planning, then join CPU preparation before any consumer draws.
Each job owns one collected object's view cache and error, preparing that
object's views sequentially. Different objects can prepare concurrently.
Collection snapshots retain pose, geometry and material ownership. Camera/view
inputs and backend instancing capability are copied by the owner thread.
Worker jobs issue no renderer calls.

The prepared views retain both required motion/instancing policies. Draws read
the prepared descriptions with live projection, dimensions, motion, resource
scope and pass state. They cannot silently rebuild a missing view. Planning
closes after preparation; unplanned views, late collection and draws before
the join fail explicitly. Duplicate requests are coalesced. The existing
sixteen-view and 256-object bounds remain; excess requests fail without
publishing readiness. Exceptions join all jobs before returning, and failed
preparation remains retryable. Direct standalone previews retain their direct
path; this is not a user-facing rendering feature opt-in.

Model render ABI advances from 23 to 24. Older providers are rejected rather
than called through an incompatible table. The ordinary runtime/editor and
model/preview modules are rebuilt together.

The character adapter synchronizes first-use lighting probe publication.
Successfully published pixels stay immutable; a missing probe can be supplied
and retried. This was required before making material preparation concurrent.
The engine's ModelLighting contract now documents concurrent calls for distinct
collected objects. Source/profile sampling itself stays in owner-thread collect.

Rain, lighting calculations, assets, sample counts and graphics budgets are
unchanged. All 61 shader/material payload files match the qualified baseline.

## Source mapping and limits

- renderCollector.cpp collection barriers: admitted views feed joined work.
- renderMaterial.cxx PrepareRenderStagePassesRange: material/stage descriptions
  are prepared before consumers instead of lazily during native draw calls.
- ModelCollectedFrame, ModelViewPreparation, PreparedWorldIndexedSubmission and
  ModelRenderProvider own the corresponding Phlosion stages. BattlePresentation
  invokes them on the normal runtime/editor path.

The supplied source snapshot is not authenticated as current retail. Matching
these mechanisms and comparing Phlosion binaries does not prove retail identity.
The broader category still needs reference-style active deformation updates and
joined backend uploads, remaining glTF/backend material stages, and the wider
producer dependency graph. This checkpoint is not 100% category parity. The
90-95% matrix range remains a planning estimate, not a measured completeness
percentage. Linux qualification, C7/C8 and whole-frame 16.67 ms acceptance remain
separate open gates.

## Verification

Sixteen CPU groups/checks pass, including false/throwing callbacks, retry,
no active work after failure, copied inputs, duplicate requests, closed planning,
unadmitted view rejection, view capacity and old ABI rejection. The extra afterimage launcher initially lacked its replay
fixture and failed before launching the game. That input was supplied and only
the unfinished checks were resumed; initial failure records are preserved.
The adapter
test starts eight simultaneous missing-probe loads, then checks successful
retry, common stable pointers and immutable publication.

Thirteen validation-enabled native checks pass across D3D12 and Vulkan.
Eighteen paired scenes cover dry/wet/orbit/battle runtime, wet/battle editor and
all four graphics presets. Two additional comparisons retain four afterimages.
Scene state and allocation checks pass. Accepted image limits and any repeat
controls are explicit in visual-review.json; full scenes are not all
pixel-identical. Six D3D12 pairs exceed the strict one-value threshold.
The wet-field residual remains up to 7/255 against an unchanged-baseline repeat;
that repeat itself differs by up to 3/255. It is not all baseline noise. Bounded
manual review accepted this checkpoint with that unresolved limit. Both battle
hosts are within 1/255, and both four-afterimage pairs are exact.

Battle diagnostics verify ready, preplanned native views and a completed job
join for every sampled draw-bearing collection. A representative frame prepares
six views and twelve submission plans, then services thirty-three native
submissions without a draw-time build. The additional six preparation requests
explain the counter rising from 33 to 39; they are not extra geometry draws.

## Matched optimized cost

EVGA RTX 2060 SUPER GPU1, native 1920x1080 Medium, fixed night/overcast,
RelWithDebInfo, validation off. Wet AB and battle ABBA per API provide twelve
primary runs with 140 steady frames each. Four separate diagnostics retain
phase and mechanism information. Screenshot work occurs after the measured
interval. Paired settings match and thermal/API failure gates apply.

| API / scene | GPU mean ms | CPU frame mean ms | CPU p95 ms |
| --- | ---: | ---: | ---: |
| d3d12 / wet | 16.855 -> 15.825 | 16.956 -> 15.676 | 27.326 -> 16.707 |
| d3d12 / battle | 17.147 -> 17.119 | 19.168 -> 19.105 | 30.466 -> 30.733 |
| vulkan / wet | 18.336 -> 18.422 | 18.175 -> 18.250 | 19.391 -> 19.178 |
| vulkan / battle | 18.121 -> 18.054 | 18.036 -> 17.973 | 19.364 -> 18.958 |


Battle whole-frame results are essentially flat: D3D12 GPU 17.147 ->
17.119 ms and CPU 19.168 -> 19.105 ms; Vulkan GPU 18.121 -> 18.054 ms and
CPU 18.036 -> 17.973 ms. D3D12 battle CPU p95 slightly worsens (30.466 ->
30.733 ms); Vulkan p95 improves (19.364 -> 18.958 ms). These small changes
do not establish a substantial speedup.

The single D3D12 wet-field pair improves 16.855 -> 15.825 ms GPU and
16.956 -> 15.676 ms CPU. Native battle preparation is not exercised in that
field case, so this result is retained without attributing the improvement
to the new actor jobs. Vulkan wet-field GPU/CPU means rise slightly.

Separate diagnostics likewise do not show a reliable whole-frame win.
D3D12 diagnostic CPU increases 18.989 -> 19.364 ms; Vulkan stays about
18.034 ms. Vulkan actors fall 2.028 -> 1.912 ms while preparation is now in
view planning. This change establishes the required producer/consumer stage
boundary and concurrency safety; it is not sold as a large performance gain.

All twelve primary runs match settings and pass API/thermal gates. The
primary screenshot pairs preserve rain state; their largest differences are
recorded in analysis-matched.json. The native-1080p Medium 16.67 ms sustained
whole-frame gate is still open. This is not retail performance parity.

Preparation moved out of the actors scope into view planning. An actors-only
time reduction is not a whole-frame speedup; use the whole-frame measurements
and non-overlapping preparation/draw total. No performance result here is a
comparison against the retail Cyberpunk executable.

Evidence: artifacts/lighting-foundation/actor-preparation-jobs-20261030.
The game roadmap records the final strict dependency and identical-payload check.
