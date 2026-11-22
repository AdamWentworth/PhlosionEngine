# Lighting foundation checkpoint

2026-10-19. Fixture emission, local shadows, HDR, static shadow culling and colored
diffuse bounce are functionally verified, including native scene, shared-consumer
and normal-launch checks. Performance limitations remain below. Earlier phase
notes are historical. Checkpoint commits were authorized on 2026-10-17. Measured
component agreement below is not a whole-lighting-engine parity percentage.

## Current direction: visual work parked; remote runtime timing available

2026-10-25 current performance restart: [GPU phase profile](GPU_PHASE_PROFILE.md)
records the committed prepared-reflection baseline, per-stage GPU/CPU timings,
image repeatability limits and the next reflection-search target. Use this report
ahead of the older whole-frame-only measurements below. The 16.67 ms goal remains
unmet; do not interpret the subjective visual checkpoint as measured retail FPS.


2026-10-25 workflow: controller owns the canonical source, review, integration,
commits and authorized pushes. Implementation edits, builds, CPU/GPU checks,
game/editor launches, captures and benchmarks run on gpu-worker (WORKER_LAN_ADDRESS).
Windows uses windows-gpu-worker and task copies under PROJECTS_ROOT. Record and
verify the task baseline, then return reviewed/approved deltas to controller.
Check for intervening edits before applying; preserve unrelated work and retest
remotely if integration changes the tested source. No silent local execution
fallback. Keep cloud reasoning; no local model inference.
See codex-infra/docs/qualifications/2026-10-25-remote-development.md for details.
The 2026-10-24/25 [screen-reflection performance pass](SCREEN_REFLECTION_PERFORMANCE.md)
adds conservative depth-tile skipping on Vulkan and D3D12. It preserves the
current visual target and records remote Windows comparisons, including thermal
and clock limitations. Use that report as the latest performance restart point.

The bounded shader optimizations and correctness review are complete; see
[the closeout report](PERFORMANCE_CLOSEOUT.md) for verification, 1080p timing
observations and remaining costs. The user deferred clean performance timing
on Windows after other applications were found using the same GPU. The
2026-10-24 [remote Linux runtime qualification](REMOTE_GPU_BENCHMARK.md) now
records full-quality 1080p samples on `gpu-worker`, without a competing game or
local model. Nine CPU and six native GPU checks pass. The GPU reaches 86 C and
reports software thermal slowdown; clock variation is retained in the evidence.
Windows D3D12 and full Linux editor/release qualification remain separate.
No 60 FPS acceptance is claimed; the scene remains far over budget.

2026-10-21 user decision: retain **about 90% visual parity for the tested non-RT
local-lighting foundation** and pause further visual-parity work. This remains
the working subjective estimate below, not a measured whole-renderer percentage.
Keep the accepted appearance and existing image failures visible while profiling;
performance improvements do not raise that visual estimate.

Timing review: roughly 65 recorded task hours went into dedicated lighting work
on October 15-21, including research, builds and validation but excluding idle
gaps and unrelated repository/folder organization. The last visual pass took 2h32m.
The discussed planning estimate for pursuing roughly 99% in defined comparison
scenes was another 60-120 hours (around 90 hours for planning), approximately
one to two weeks at that pace. This is uncertain and deferred, not a commitment.

The agreed bounded closeout scope below is retained for the deferred clean
measurement. Do not reopen visual-parity work while finishing it:

- Measure the current normal configuration at 1080p on Windows D3D12 and Vulkan,
  using the existing performance tools and sequential runs. Record the selected
  GPU, driver, resolution and settings; separate cold shader/startup delays from
  warmed gameplay. Review CPU/GPU frame time and p95/p99 spikes in dry daylight,
  wet night, camera movement, battle and transitions, with editor/runtime checks.
- Use matched feature controls to identify the largest costs, then fix avoidable
  work while preserving lights, materials, shadows, reflections and weather.
  Validate matching images and affected shared consumers. Do not hide costs by
  silently reducing quality or relaxing existing acceptance limits.
- Report measured gains, remaining bottlenecks and whether the existing 60 FPS
  target (16.67 ms/frame) is met on the tested configuration. Other hardware and
  Linux remain separate qualifications; do not infer them from this machine.
- Close out after the useful, verified optimizations and an explicit budget
  report. Record any larger architectural work as deferred, then move to other
  game/environment priorities instead of reopening the visual-parity campaign.

The remaining visual backlog is preserved below: object motion/history, hidden
and multilayer visibility, reflected-character backend consistency and strict
image differences. Revisit it after higher-impact game work or when a concrete
gameplay view exposes a problem. The closeout report distinguishes implemented
optimizations and correctness evidence from the deferred performance acceptance.

## Last visual pass: buffered directional contact visibility

2026-10-21. The complete independent current-frame research chain now passes its
original limits: prepared fields/raw output are byte-exact, and final visibility
differs in one channel value by one byte across 921,600 RGBA pixels. All eight
stage outputs repeat exactly; 38 research tests pass. Previous history is captured
native input. The original 1,011 replay validation messages and composed-color
equality failures remain; they are not waived by component agreement.

Production now has an FP32 half-resolution directional search, depth/normal
spatial filtering, camera-reprojected history with neighborhood clipping, and
depth-aware reconstruction on D3D12/Vulkan. Dedicated filter pipelines operate
before scene color; immutable frame-owned snapshots preserve previous depth and
visibility. The existing game contact pass enables this path automatically.
Diffuse visibility uses the recovered angular-cone consumer; direct light and
emission retain their existing behavior. Reference-only FP64 is not required.

This is an engine adaptation, not a byte-identical retail port: it uses geometric
prepass normals, nearest half-depth samples, engine radius/bias and phase policy,
FP16 storage, camera-only reprojection and depth rejection instead of object motion
vectors. Prepass normals bypass display/fog coloring. Background samples are
excluded from reconstruction, preventing dark silhouette borders. History resets
on view/scene changes, skipped frames, resizing, projection changes and camera cuts.
Orthographic and diagnostic legacy paths retain the previous inline integral.

Engine evidence: `artifacts/lighting-foundation/contact-filter-20261021`.
`native-04` passes all 24 native contact cases and graphics validation. Direct,
emission, explicit reset and camera-cut controls are exact; empty/masked and
disoccluded floors differ from the open-floor control by at most 1/255. Four CPU
contracts and sixteen neighboring reflection checks pass. Of 24 cross-API image
pairs, 23 stay within 1/255; the away-facing reflection case retains nine pixels
at 2/255. This remains a strict one-byte comparison failure, not an exact-API claim.
Earlier failed trials are retained; `native-03` was interrupted after its Vulkan
checks because its silhouette failure was already known. The camera-cut trial
initially captured before the cut; the final case captures the cut frame.

Research evidence: Cyberpunk2077Research
`data/evidence/reflection-composition/visibility-chain-20261021/run-01`.
Final consumer evidence is under each repository's
`artifacts/lighting-foundation/contact-filter-20261021/final`. Normal game launch,
eight wet field/battle captures, 28 close/daylight/orbit captures and both APIs'
scene-switch sequences pass their content checks. The complete 53-log game scan
and actor capture scan are clean. Completed launch/switch evidence was written
under the engine by the launcher's relative-path resolution, then copied into the
game evidence directory with hash receipts; originals remain intact.

The D3D12 reflected-player isolation and ordinary repeat still fail exact equality:
each now differs at four pixels by 1/255, versus 394/419 pixels up to 2/255 in the
preceding pass. Vulkan isolation is exact. The reflected character is dimmer on
Vulkan in the grazing review view; overall API MAE is .24664/255 and mirrored-body
region MAE is 1.60204/255. Keep this response-consistency gap separate from the
small exact-repeat failures. No original image gate was relaxed. The consolidated
report retains a failed strict-production-image qualification status.

Autochess's two native groups and seventeen editor cases pass on both APIs; all
38 images are pixel-identical to the accepted baseline. Its four CPU contracts
and the game's two CPU contracts pass. Final editor/plugin source proof, all three
staged game/model DLLs, embedded lighting source in all three renderer hosts, and
the twelve recorded production inputs agree. First shader preparation is slow
(retained preparation timings), but warm qualification deadlines were unchanged.
This pass makes no new performance or Linux-runtime claim.

Working visual estimate: **about 90% for the tested non-RT local-lighting
foundation**, a subjective assessment rather than a measured whole-frame score.
The basis is the recovered directional producer/filter chain now integrated into
production, softer local contacts and reflection visibility, and reviewed wet,
daylight, sunset, night and moving-camera views. Lamp colors, gains, weather and
scene state remain matched. This is a modest visible refinement, not a claim that
the whole game's appearance matches Cyberpunk. Remaining work includes per-object
motion vectors, more demanding motion, hidden/off-screen and multilayer visibility,
reflected-character API consistency and the retained strict image discrepancies.
See `final/qualification-summary.json` in the engine evidence directory.

## Previous research: independent raw visibility qualified

2026-10-21. The remaining directional search discrepancy is closed in the
retained dispatch. The independent HLSL passes all 230,400 RGBA pixels at the
original 1/255 gate, maximum .002204865. Its actual stored UNORM output is
byte-identical to native. Substituting it before native spatial/history/upsampling
also gives byte-identical full-resolution visibility across 921,600 RGBA pixels.
Both raw and filtered outputs repeat exactly. The previous five-pixel raw and
eleven-pixel downstream failures are closed for this experiment.

The fix preserves the floating direction counter and far-radius arithmetic and
holds probe-depth products/subtraction in FP64 until the final FP32 conversion.
This is an independent reference adaptation: the captured shader itself uses
FP32. Engine backend capability/cost and production implementation are still
unqualified; no production source, binaries or game plugins change in this pass.
An additional-UAV native trace preserves the complete native output while showing
the depth-cancellation error that the earlier geometry-only diagnostic obscured.

All 38 reflection tests pass. Six replay/repeat runs retain the same 1,011 existing
validation messages with none added/removed/discarded. Native inputs, round trip,
handoffs and constants remain intact. Full-replay validation and composed-color
equality still fail (maximum .0001220703125). Storage is checked with zero byte
error against the previously qualified observed model; the disproved ideal
half-step hypothesis remains a failed diagnostic. Image limits were not widened.

Evidence: Cyberpunk2077Research's `lighting/reflections/VISIBILITY_PRODUCER.md`
and `data/evidence/reflection-composition/directional-coordinates-20261021`,
especially `candidate-32`, `chain-33` and `final-34`. Next is the complete
independent filtering/history chain, motion/reset qualification and production
integration. Native downstream filters are still used in this experiment.
The subjective production visual estimate remains around 87%; no new game image,
whole-renderer parity or performance claim follows from this research milestone.

## Previous research: visibility storage and coordinate isolation

2026-10-21. The research replay's stored-value discrepancy is explained by an
observed conversion model: clamp, truncate to 12 fractional bits, nearest UNORM8
with ties down. It predicts all 921,600 values exactly in each of three sets:
native output, independent substituted output and controlled synthetic ramps.
This is device-qualified behavior, not a universal API rule. The old ideal
half-step hypothesis remains failed; no comparison tolerance was widened.

Supplying only native per-direction UV steps makes the independent visibility
search pass all 230,400 RGBA pixels at the original 1/255 gate (maximum .002205342).
Its gathers, horizons and integration remain independent. The fully independent
version still fails at the same five pixels, so the component gate stays failed.
Rejected rounding experiments are retained. The supplied-coordinate diagnostic
is not a runtime solution or a higher visual-parity estimate.

The corrected diagnostic preserves the native stages and final visibility
byte-exactly; its outputs repeat exactly. Four runs retain the same 1,011 existing
validation messages with none added/removed/discarded. The first diagnostic's
descriptor-copy error was corrected in a fresh build. Full-replay validation
and composed-color equality still fail (maximum .000244140625 in this run).
All 38 reflection research tests pass. Evidence and reproduction are in
Cyberpunk2077Research's `lighting/reflections/VISIBILITY_PRODUCER.md` and
`data/evidence/reflection-composition/directional-precision-20261021`, especially
`storage-03`, `coords-10` and `final-11`.

Next: independent coordinate generation, then full filtering/history-chain and
production integration. No engine/game production source or binaries changed
in this research pass. The subjective visual estimate remains around 87%; all
production qualification failures below remain open. No performance claim.

## Previous research: independent directional search

2026-10-21. Cyberpunk2077Research now implements the captured 15-direction,
ten-sample raw visibility search in independent HLSL. Depth-derived normals are
byte-exact; radius/phase/material policy passes its original normalized limit.
The full raw image still fails at 13 channel values in five of 230,400 pixels,
maximum error .100000335 versus 1/255. Per-direction diagnostics localize these
to sample-boundary precision; they have not been excluded or declared harmless.

A separate experiment feeds the independent raw search into the native filters.
The full-resolution visibility differs in 66 pixels; eleven exceed the one-step
UNORM8 limit by reaching two steps. Raw and filtered substituted outputs repeat
byte-exactly. This measures downstream impact, not independent complete-chain
qualification. A separate ideal half-step storage check also fails. All 34
reflection research tests pass. Original replay validation errors remain 1,011
with no added/removed/discarded messages; native final-color repeat differences
remain explicit failures.

Evidence: the research repository's `lighting/reflections/VISIBILITY_PRODUCER.md`
and `data/evidence/reflection-composition/directional-native-20261021`, especially
`probe-07`, `port-12` and `chain-13`. Next are the remaining boundary/storage
discrepancies and an independently generated/filtered history chain before
production integration. The current engine scalar uses a different weighting,
so dropping the raw search into it without the corresponding filters/consumer
contract would be incorrect.

No engine/game production source, binaries or staged plugins change in this
research pass. The subjective visual estimate stays around 87%; production image
failures and qualification below remain unchanged. No performance claim.

## Previous research: directional-visibility filtering

2026-10-21. Cyberpunk2077Research now has exact-event buffers for all five native
visibility dispatches. Independent preparation, spatial filtering and upsampling
models meet their original limits. An independent HLSL history filter passes all
230,400 RGBA pixels at the native temporal event, maximum error .00220487 below
1/255. The original temporal dispatch and final visibility buffer are byte-exact
with diagnostics enabled, and all 28 reflection research tests pass.

This is a research milestone, not an engine/game visual change. The raw
directional search remains to be reconstructed, followed by complete-chain and
production integration checks. The ideal CPU history sampling model still fails
at edges; native-sample math and the full independent GPU filter pass separately.
The research replay retains 1,011 baseline validation errors and tiny final-color
repeat differences. These are not production validation results. See the
research repository's `lighting/reflections/VISIBILITY_PRODUCER.md` and
`data/evidence/reflection-composition/visibility-native-20261021`.

No production source, binaries or staged plugins changed in this research pass.
The subjective visual estimate stays around 87%; the production image failures
and qualification status below remain unchanged. No performance claim.

## Current production result: authored reflection response

2026-10-21. The ordinary-material controls recovered from the native study are
implemented as opt-in renderer parameters on D3D12 and Vulkan. PokemonCyberpunk's
static field/battle surfaces use the recovered ordinary profile; explicit water,
characters, foliage and neutral model previews retain their previous response.
The adjustment changes reflected radiance response with material and viewing
angle, without raising lamp intensity or exposure. See
[REFLECTION_CONTROLS.md](REFLECTION_CONTROLS.md) for the formula and evidence.

All 260 selected engine cases pass, including 56 reflection-response GPU cases.
Worst response error is .824096/255; all 28 response API pairs meet the original
1/255 limit and all 20 neutral images are pixel-identical to previous production.
Normal game launch, eight wet host/API views, 28 close/daylight/orbit views and
both scene-switch sequences pass their content/API checks. The visual change
is localized and modest. The supplied-native-input study reduces mean linear
RGB material-response error from .0781305 to .000322112; that component result
does not establish whole-frame or whole-lighting parity.

Two areas remain unresolved in strict image checks. The native `handoff6` edge outlier
still fails the 1/255 API limit. The ordinary D3D12 repeat differs in 419 pixels,
maximum 2/255, and the reflected-player isolation check fails outside its region
on D3D12; Vulkan isolation passes exactly. Disabling the authored controls gives
an exact D3D12 repeat in this run, so the failure cannot be declared independent
of this change. Original failures and neutral diagnostics are retained separately;
no tolerance was widened. Graphics validation is clean. Consumer qualification
and final provenance are recorded in the implementation report.

Autochess's two standalone groups and seventeen editor cases pass on both APIs;
all 38 images are pixel-identical to its accepted baseline. The two game CPU
contracts, four Autochess CPU contracts and twenty reflection-research tests
pass. Source, binary, embedded-shader and staging checks agree. Evidence is under
`artifacts/lighting-foundation/reflection-controls-20261021/final` in the engine
and both games. The consolidated report retains an overall failed qualification
status for the strict image failures above. No new Linux or performance claim.

The working subjective estimate remains around 87%; this bounded improvement
does not justify claiming 90%. Next is the captured directional-visibility
producer, particularly filtering/history and reflection edge/detail behavior.
Existing captures are sufficient to start. Keep the D3D12 repeat failure visible
and investigate its cause rather than replacing its exact-equality gate.

## Previous result: native material-response study

2026-10-21. The private research follow-up now matches independently decoded
ordinary-material reflection response against 893,289 native event-7161 pixels.
Maximum absolute error is 2.92299e-6 using native DFG/radiance/visibility inputs;
the original 3e-5 normalized limit passes with no discarded worst samples.
The unmodified shader round trip and both within-event original outputs are
byte-identical. The captured material controls have a substantial effect that
cannot be represented by a universal light-intensity multiplier.

This turn changes research tools/documentation, not runtime shaders or game
binaries. The visual estimate remains about 87%. Next is a generic authored
reflection-response control path with neutral defaults and a qualified Cyberpunk
ordinary-material profile. Preserve the preview and Autochess behavior.

The research replay is not fully clean: baseline and probe both retain 1,011
identical validation errors, and a baseline repeat reproduces tiny variations in
one incoming texture and one output. Within-event output preservation and the
material calculation pass independently; full-replay/cross-run failures remain
failures. See `Cyberpunk2077Research/lighting/reflections/MATERIAL_NATIVE.md` and
its `material-native-20261021/probe-05` evidence. All 20 reflection research tests
pass. No new renderer, game, performance or whole-frame qualification is claimed.

## Previous production result: reflection material response

2026-10-20 implementation, 2026-10-21 integration checks. Outdoor reflections now
preserve the captured scene's radiance instead of inheriting the model preview's
`.44` attenuation, and accept authored reflection roughness down to `.04`.
Wetting no longer makes an already polished material rougher. Direct-light lobes
and neutral model previews retain their previous behavior. All 223 selected
engine cases pass, including 16 independent unit-response cases and four direct-
highlight preservation controls. The worst unit-response error is .68107/255;
the original dry path's worst error was 59.8049/255. The separate strict API image
gate still has one retained `handoff6` edge outlier among 66 pairs. No threshold
was relaxed. See [REFLECTION_MATERIAL_RESPONSE.md](REFLECTION_MATERIAL_RESPONSE.md)
for the integration evidence and scope.

Normal launch, eight wet field/battle views, all 28 close/daylight/orbit images
and both scene-switch sequences pass. Reflected-player isolation passes on both
APIs and the ordinary D3D12 repeat is exact in this run; the previous intermittent
one-step discrepancy is not considered fixed without a cause. The faint
offscreen-NPC contribution now clears its original visibility gate on both APIs
with matched before/after scene state. The strict native edge outlier remains.
Autochess passes both standalone groups and all seventeen editor cases on both
APIs; all 38 images are pixel-identical to its accepted baseline. Graphics
validation is clean, and final binary/staging/source checks agree. The final
evidence and consolidated result are under
`artifacts/lighting-foundation/reflection-material-20261020/final` in the engine
and both games. No new Linux, frame-performance or RT-enabled qualification.

The station's matched views show a modest material-response improvement. The
working subjective visual estimate remains around 87%; this foundation correction
does not establish a visible jump to 90%. The retail material adjustments still
need decoded, event-matched inputs and native output comparison. Very smooth
direct highlights also remain outside the newly qualified reflection domain.
Existing captures contain the material bindings; a new user recording is not
required for that next investigation.

## Previous result: grazing reflection receivers

2026-10-20. Receiver visibility now projects the captured surface plane onto the
receiver's own ray. The isolated low-resolution grazing fixture improves from
100/400 missing samples to 0/400 on both APIs, retaining the existing hidden-
receiver rejection tolerance. All 203 selected engine cases pass; after removing
temporary readback instrumentation from the linked binaries, 20 focused cases
pass again with all 44 images unchanged. The normal editor launch, eight wet
station/battle views, 28 close review images and both scene-switch sequences pass
their content/current-API checks. Autochess's two native groups and 17 editor
cases pass on both APIs; all 38 images remain pixel-identical to the accepted
baseline. Graphics validation is clean and final binary/staging hashes agree.
The strict D3D12 actor-isolation/repeat check has reproduced a small one-step
image discrepancy; it remains a failure, not a relaxed threshold. The two strict
native API edge outliers, historical Vulkan disabled-control differences and
weak offscreen-NPC reflection also remain open. See
[REFLECTION_RECEIVERS.md](REFLECTION_RECEIVERS.md).

The research repository also now has an independently checked ordinary
reflection-material formula and reconstructed b6 controls from the existing
capture. Selection also agrees over 82,240 supplied material-field combinations;
decoded material inputs and complete native output remain the next visual target.
The working subjective visual estimate stays around 87%; neither test counts nor
these supplied-input arithmetic comparisons establish the 90% target.

## Previous result: reflection detail and continuity

2026-10-20. Pixel-sized screen traversal, perspective intersections, confidence
blending, camera-following local capture and current-pose character capture are
implemented. The retained grazing-source error falls from 9.160 to about .700
pixels on both APIs. All 197 engine cases and the existing Cyberpunk host/view,
actor-isolation, scene-switch and repeat checks pass. The new offscreen NPC
visibility control remains below its original threshold on both APIs; including
current poses in capture is not yet a qualified visible improvement in the game.
All 38 Autochess native/editor images remain pixel-identical to its accepted
baseline; Vulkan validation is clean. The strict API image comparison retains one small grazing-edge
outlier (25 pixels above 1/255, maximum 4/255); the other 62 pairs pass.
Final D3D12 runtime checks were repeated after explicitly refreshing the embedded
lighting program; they pass with an exact repeat. Source hashes alone had not
established that the generated shader was current after diagnostic restoration.
See [REFLECTION_CONTINUITY.md](REFLECTION_CONTINUITY.md).

## Previous result: captured-surface reflection visibility

2026-10-20 follow-up. Local captures now retain geometric normals and distance
in unused atlas cells. Twelve views replace six; both game hosts use the named
capture count. Reflection rays intersect and validate captured surface planes,
with roughness footprints adjusted for the receiver/probe distances. The blocker,
masked-hole and analytic-position controls pass in focused testing, including
camera movement. All 147 affected renderer checks pass; 27 API image pairs differ
by at most 1/255, and the 18 analytic camera samples have at most 1.366 pixels of
centroid error. Game and Autochess builds, CPU contracts and the editor/plugin
pair proof pass. Normal D3D12 launch passes after about 19 minutes of first-run
shader preparation. Eight wet station/battle views and exact reflected-player
isolation pass on both APIs. All 28 close-view/daylight/camera-replay images pass,
with maximum cross-API mean difference .337326/255. Both scene-switch sequences
pass, the ordinary D3D12 repeat is pixel-identical, and game Vulkan validation is
clean. Autochess's two native material groups and 17 editor cases pass on both
APIs; all 38 images remain pixel-identical to its accepted baseline. Its initial
cache-preparation fixture omitted environment/script links; those were attached
before acceptance and the helper is corrected. Incomplete preparation images
remain diagnostic only. Full-scene checks retain their original deadlines and
guards, and Vulkan validation is clean. Production Autochess and ModelSupport
source are unchanged by this pass. Source manifests, the consolidated
`qualification-summary.json`, and failed trials are in
`artifacts/lighting-foundation/reflection-visibility`.
See [REFLECTION_VISIBILITY.md](REFLECTION_VISIBILITY.md), including the retained
grazing-source sampling limitation. This phase was committed in `b372f6c` before
the continuity follow-up. No 90% claim or new performance result.

## Previous result: local environment reflections

2026-10-20. The offscreen static-environment fallback is implemented on D3D12
and Vulkan, with a face-isolated image pyramid and footprint-filtered GGX
sampling. The original unfiltered rough test failed visibly and was replaced.
All 123 final native/CPU renderer checks pass; the 15 new API comparison images
differ by at most 1/255, and the rough case is exact. Vulkan validation is clean.
PokemonCyberpunk builds and its two CPU contracts pass. Integration caught a
missing D3D12 composed-surface call (now covered by the composition contract),
and a small repeated-capture instability. Frame-owned D3D12 depth snapshots
remove the observed instability without serializing normal rendering. Both APIs
pass exact reflected-player isolation after the final rebuild, and all eight
wet field/battle host captures pass. All 28 close-view/daylight/camera-replay
images and both normal scene-switch sequences pass; the game validation scan is
clean. Evidence is in the game's
`artifacts/lighting-foundation/local-reflections/qualified-04`.
The final engine run includes affected haze/HDR checks and has no Vulkan
validation errors. Autochess image qualification is complete. Its first native
Vulkan attempts hit the unchanged 180-second deadline. The failed captures/logs
are retained. Cache preparation now refreshes copied fixture shaders first,
records their hashes and renders the actual capture frame (164); qualification
still uses the original deadlines. The corrected retry passes both native
cohorts and all 17 editor cases on both APIs with identical prepared shader
hashes. All 38 images remain pixel-exact against the accepted baseline, and
the validation scan is empty (`baseline-comparison.json` and
`qualification-final-03.log` in Autochess's artifact root). Its builds, four CPU
contracts and final editor/plugin pair verification pass. Cold shader compilation
remains slow; warmed-cache image checks do not qualify cold-start time or frame
performance. Production Autochess and ModelSupport source are unchanged by this
pass. No commit is
requested this turn. Diagnostic failures and rejected trials remain in artifacts.
See [LOCAL_REFLECTIONS.md](LOCAL_REFLECTIONS.md) for scope and limitations.

## Previous result: general screen reflections

2026-10-19 follow-up, Windows qualification completed 2026-10-20. Detailed
Outskirt exploration and battle now capture opaque
current-view radiance/depth and trace reflections on eligible receivers beyond
the existing horizontal wet plane. The optional engine path is implemented on
Windows D3D12 and Vulkan; Autochess and the Colosseum reference leave it disabled.
Normal game launch enables it without changing assets, material values, fixture
strengths or weather. See [SCREEN_REFLECTIONS.md](SCREEN_REFLECTIONS.md).

The controlled 74-case native suite passes, including vertical mirrors,
rough/matte surfaces, blockers, source removal, changed viewpoints, clipped and
offscreen sources, and existing contact/planar/composition coverage. Ten of eleven
screen-reflection API images are pixel-exact; the rough case differs by at most
1/255 in three channel samples. Game validation passes eight wet field/battle
runtime/editor views, exact reflected-player isolation, and twenty pump/bar,
noon/sunset and changed-viewpoint source-on/off runtime images. Final Vulkan
validation scans are empty. The improvement is localized on the station's rough
materials. Autochess passes both native cohorts and all 17 editor cases on both
APIs; all 38 images remain pixel-exact against its accepted baseline. The two
game CPU contracts, four Autochess CPU contracts, 15 research tests and staged
game DLL hashes pass. Production Autochess and ModelSupport source are unchanged
by this pass.

The captured ordinary SSR border factor is qualified independently over 270,400
UV inputs; maximum float32/float64 difference is 4.768372e-7. The native raymarch
and nine-tap roughness filter remain independent approximations, not a recovered
retail producer. General offscreen coverage, rough reflection filtering,
continuous-motion stability and indirect-light updates remain visual targets.
There is no new whole-engine parity percentage or claim that 90% is achieved.
Performance work is deferred; this pass's incremental cost is unmeasured. Cold
shader compilation remains very slow: Autochess D3D12 cache preparation hit its
600-second watchdog twice before a resumed run completed. The passing image
matrices use warmed caches and their original 180/240-second deadlines. Both
preparation failures are retained; this is not a passing cold-start result.
Linux is unverified. This pass is uncommitted.

## Prior result: directional reflection visibility

2026-10-19 follow-up. The depth horizons now supply a bent direction alongside
diffuse visibility. Reflected environment/planar light uses its overlap with a
roughness-dependent reflection cone, including the captured grazing-angle
coefficient. This reduces reflected light from locally blocked directions while
preserving open directions, direct light and emission. The native depth producer
remains an independent finite-radius approximation, without temporal filtering.
See [DIRECTIONAL_VISIBILITY.md](DIRECTIONAL_VISIBILITY.md).

The supplied-input response agrees with the retained ordinary 2077 consumer to
maximum .000875224 on a 0-1 scale with its captured coefficient. This measures
one arithmetic component; it does not establish the 90% whole-lighting target.
The change is subtle in the station's wide views. General reflection coverage,
temporal stability and indirect-light updates remain the main visible gaps.

All 50 focused native checks pass on Windows D3D12/Vulkan. Both normal game
launches and five-visit scene switches, eight wet station/battle API/host views,
eight noon/sunset runtime controls, and the isolated reflected-player check pass.
Disabled daylight controls remain pixel-exact against the previous baseline.
The initial D3D12 actor pair failed its exact outside-bounds diagnostic in nine
1/255 pixels; a fresh pair passes the unchanged check. Both are retained and the
initial discrepancy's cause remains unresolved. No threshold was weakened.

Autochess's two native cohorts and all 17 editor cases pass on both APIs; all 38
images remain pixel-exact against its previous baseline. The affected game and
Autochess CPU contracts (2 and 4), research's 12 tests, staged DLL hashes and paired
editor/plugin fingerprint pass. Final Vulkan validation reports are empty.
First-use compilation remains very slow, with retained cold capture failures;
successful consumer tests use warmed caches under their original deadlines.

This remains a visual prototype with substantial cost. At 1080p heavy wet night,
the entire contact feature adds 15.60 ms GPU on D3D12 and 15.61 ms on Vulkan on
one RTX 2060 SUPER, exceeding existing review guides. It is not an isolated
measurement of this addition over scalar contact visibility. Performance work
remains deferred in favor of visuals; Linux is unverified.

The existing capture also exposes the next visibility-producer target: half-size
directional sampling, spatial filtering, history validation/clipping, and
depth-aware reconstruction. Shader containers and descriptor dependencies are
verified, and CPU-upload reconstruction recovers the custom constants at all
five dispatches. Matched native inputs/outputs and an independent producer model
are still pending; no new capture session is needed to begin that work. Private
evidence is under each project's `directional-visibility` artifact directory and
research's `data/evidence/reflection-composition`. This pass is uncommitted.

## Prior result: live contact visibility

The user prioritized closing visual gaps before further hardware-specific FPS
optimization. The station now supplies live, finite-radius diffuse contact
visibility from current opaque/masked depth in exploration and battle. This
adds local indirect shading where objects meet, while keeping direct light and
emission unchanged. It is enabled independently of fixture state; the Colosseum
reference and Autochess leave the optional pass disabled.

The retained 2077 composition shader also consumes a bent direction for separate
diffuse/specular visibility. This first pass supplies scalar diffuse visibility;
it does not reproduce that full producer, specular cone response or temporal
filter. The 30 focused native checks and the station/battle runtime/editor views
pass on D3D12 and Vulkan. Direct-only and emission-only controls remain exact.
Final Vulkan editor launch, scene switching and wet field/battle checks report
zero core/synchronization errors after repairing editor render-pass formats and
texture-descriptor retirement. Contact-disabled station images match the prior
accepted baseline exactly.

Shared-consumer qualification also found and repaired D3D12 legacy-dispatcher
character corruption and Vulkan HDR overlay/push-constant errors. Autochess now
passes both native cohorts and all 17 editor cases on both APIs: all 38 images
match the previous accepted baseline exactly, with zero Vulkan validation errors.
The native submission test now exercises sprites and debug overlays in HDR and
display targets. First-use shader compilation remains a material limitation;
these successful consumer runs use warmed caches and unchanged deadlines.

This is a visual prototype, not performance-qualified. At 1080p heavy wet night
on one RTX 2060 SUPER, contact visibility adds 11.89 ms GPU time on D3D12 and
14.35 ms on Vulkan, exceeding the existing +1.5 ms review guide. The horizon
search currently runs inside forward material shading; computing a reusable
visibility buffer is an unimplemented optimization candidate. See
[CONTACT_VISIBILITY.md](CONTACT_VISIBILITY.md) for evidence and remaining gaps.
No new aggregate parity percentage follows from these component checks.

The final game build passes both normal launches, scene switching and the eight
wet station/battle captures with zero Vulkan validation errors. Its 12 normal
field/battle/reflected-player images remain exact. One additional 1080p D3D12
timing image differs by at most 2/255 in 482 pixels; the strict exact-image
diagnostic remains recorded as failing rather than changing its threshold.
Final evidence is the game's `contact-visibility/qualified/final`. These are
Windows results; Linux remains unverified. This work is uncommitted.

## Prior result: Vulkan submission performance

2026-10-19 follow-up: repeated heavy-wet-night 1080p runtime checks reduce Vulkan
host median from 49.136-50.289 ms to 22.415-23.262 ms (about 54% less frame time).
Final GPU medians remain 20.291-20.419 ms. All final timing images and scene states
are identical to their pre-pass baselines. The default D3D12 renderer has no
implementation change in this pass and measures 20.858 ms host / 20.042 ms GPU.
Neither backend meets the 16.67 ms target yet. These are the pre-contact-pass
timings; visual work now takes priority, with cost checks retained as a guard.

The fixes avoid repeated immutable-index validation and slow reads from GPU upload
memory when reusing skin palettes. Vulkan now overlaps next-frame CPU recording
with the preceding frame's GPU work, clears stale image/fence ownership and uses
presentation semaphores per swapchain image. Only one presentation may be pending;
surfaces without a spare image use immediate presentation. An overlap-disabled
control measures 42.187 ms host, confirming that removing the serial wait matters.
Display latency is not measured; the overlap can add up to a recording interval.

Native validation also reproduced preexisting clear/load render-pass compatibility,
depth reuse and presentation-semaphore errors in the old binary. These are fixed;
the final heavy station and 32 focused engine checks pass with Vulkan core and
synchronization validation enabled. Details, retained failed trials, reproduction
and platform limits are in [CPU_SUBMISSION.md](CPU_SUBMISSION.md).

Both normal editor launches and their five-visit station/reference switches pass,
with zero pending resources at each recorded visit. All eight station/battle
runtime/editor views have exact pixels and scene state against the preceding
accepted baseline. Existing fixture, allocation and API/host guards and both
affected game CPU contracts pass. The three staged game/model DLLs match their
current build hashes. These are targeted Windows checks; Linux remains unverified.

Autochess runtime/editor builds and four affected CPU contracts pass. Both native
character cohorts on both APIs and all 17 editor material cases on both APIs pass
their existing content/image guards; all 38 images exactly match the previous
accepted baseline. Final test registration respects `PHLOSION_BUILD_GPU_TESTS`;
both OFF/ON configurations were checked, followed by another native Vulkan run
with core/synchronization validation. That configure step regenerated Autochess's
policy header and relinked its plugin, while the qualified editor executable stayed
identical. Eight additional final-plugin captures covering fire, eyes, authored
wings and flight also pass with exact images. Final source/binary provenance is
retained in Autochess's `cpu-submission/editor-pair-final`.

Functional verification is complete. Performance still needs the GPU work above;
input-to-screen latency and cold startup were not measured in this pass. Autochess
captures used a retained warm shader cache with recorded hash provenance and the
unchanged 180-second native / 240-second editor timeouts. No local AI was used.
This pass and the preceding outstanding changes remain uncommitted.

## Prior result: indirect-light composition

2026-10-19 follow-up: scene reflections now blend with environment radiance
before a shared material response. The diffuse multiple-scattering contribution
is retained throughout the blend. Material occlusion uses the captured
albedo-dependent response for diffuse environment/local-probe light, without
dimming direct light or emission. See [INDIRECT_COMPOSITION.md](INDIRECT_COMPOSITION.md).

Research reproduced the retained shader's AO arithmetic exactly in float32
over 66,049 CPU samples; a separate 65,536-sample synthetic radiance blend
comparison has maximum error 1.42e-5 against float64. Our explicit upper clamp
bounds a .0001 coefficient-rounding overshoot. These measure supplied-input
arithmetic slices, not visibility generation, full captured frames or whole-engine
parity. Existing artistic scales and the approximate planar filter remain.

The station's 28 materials have zero occlusion textures. This pass therefore
does not add station crevice/contact darkness: that requires an actual visibility
input. Screen-space AO/bent normals, reflection visibility, general SSR/probe
selection and updated diffuse transport remain open targets.

The native suite passes 39 composition/reflection/HDR/probe checks, plus the
Vulkan cache contract and eight shared material contracts. Bounce/direct/emission
patches agree with independent display predictions within 1.03/255 on both APIs.
Reflected-glyph API MAE is .0368/255 or less; rough reflection edges remain wider
than clear ones. Eight relevant research tests and seven game CPU contracts pass.

The game actor oracle had a false positive: the old whole-reflection on/off pair
mostly detected broad ground darkening. The corrected pair hides only the
reflected player in a grazing view of an existing puddle. D3D12/Vulkan change
133/101 pixels by over 3/255 inside the independently projected body bounds;
every pixel outside is identical. The original >100-pixel, >3/255 signal threshold
is retained. Front/oblique/precipitation-free, exact dry/legacy, station and battle
checks pass. Failed initial oracle evidence is retained and explicitly superseded.

Both normal editor launches and both five-visit station/reference scene-switch
sequences pass, with zero pending scopes at the recorded visits. The three
staged game/model DLLs match their current build hashes. The Vulkan cache had
silently stopped saving after crossing its 64 MiB limit; the bounded cap is now
256 MiB and overflow is logged. A native 80,540,863-byte payload was saved and
reloaded successfully. Post-fix normal-launch log spans are 32.49 seconds D3D12
and 40.47 seconds Vulkan, versus earlier first/recompiling runs of 251.91 and
330.09 seconds. These are not an isolated cache speed benchmark; cold compilation
and content loading remain. No GPU allocation or plugin ABI changed.

All public/station/source diffuse-bounce controls pass. Eight matched station/
battle API/host views have exact scene, environment, import, surface and actor
state against the preceding accepted baseline. Image MAE is about .60-.61/255
for the station and .21/255 for battle, consistent with a modest visual change.

First matched heavy-wet-night 1080p runtime measurement, 200 warm samples,
RTX 2060 SUPER, no concurrent test/build job:

| Backend | GPU before / after ms | Host before / after ms |
| --- | ---: | ---: |
| D3D12 | 19.203 / 19.895 | 19.376 / 20.239 |
| Vulkan | 20.022 / 20.258 | 44.267 / 47.588 |

These are full-frame medians. Both GPU times remain over 16.67 ms and Vulkan
remains host-bound; no speedup is claimed. A separate Vulkan repeat with exact
scene state measured 19.912 ms GPU and 49.058 ms host, with p99 23.622/64.823 ms.
The host slowdown persists and cannot be called stable performance. During that
probe a desktop-app process consumed 45.89 CPU seconds and the C++ indexer 7.97;
attribution to this shader change is unresolved. The initial run also increased
unchanged rain/shadow host stages. Preserve both runs; a controlled CPU profile
is still needed. `repeat-comparison.json` records stage medians and activity.

Autochess's paired editor/plugin and standalone runtime/test builds pass, along
with its four affected CPU contracts and both native character cohorts on both
APIs. Warm native captures take about 61-64 seconds within the unchanged
180-second gate. Separate first-use preparation took 446.47 seconds on Vulkan
and 436.82 seconds on D3D12 for the first cohort. This is a substantial cold-start
limitation, not normal-launch qualification. Cache seed provenance and all raw
outputs are retained in that game's `indirect-composition` artifact directory.
All 17 editor cases pass on both APIs (34 captures) with unchanged image/content
limits, including both review lighting presets, fire/eyes, wings and flight.
All 34 cropped previews are pixel-identical to the preceding accepted baseline.
Warm preview log spans are 4.95-13.20 seconds within the original 240-second gate.
Separate editor preparation took 393.05 seconds Vulkan and 23.26 seconds D3D12.
The editor saved a 77,758,805-byte Vulkan cache, over the old limit, and the warm
matrix reloaded it successfully. PokemonModelSupport has no source changes.

Evidence is `artifacts/lighting-foundation/indirect-composition` in engine/game;
the game's accepted scene/bounce/launch captures are under `verified`.
`comparison.json` retains state, image and initial cost results.
Implementation, builds and functional qualification are complete. Performance
limitations above remain open; this work is uncommitted.

## Prior result: reflection response and Vulkan material integration

The flat wet-surface reflection now uses the existing licensed LTC GGX moments
for its angle/roughness material response. Against the retained Cyberpunk DFG
table over 131,841 samples in the active roughness range .04-.55, normalized
mean error falls .01423475 -> .00042803 at F0=.02, and .01477740 -> .00048900
at F0=.04 (96.99% and 96.69% reductions). This measures one response component,
excluding the artistic scale, filtering, coverage, occlusion and image composition.
It is not 97% lighting parity. See [REFLECTION_RESPONSE.md](REFLECTION_RESPONSE.md).

The game comparison also exposed and fixed an integration defect: composed
Vulkan surfaces bypassed `worldPlanarSpecular`. Native Vulkan fixtures already
used it, and the broad API image threshold did not detect the missing receiver.
The composition contract now requires the assignment; the game validator requires
a visible reflection on/off delta on each enrolled API. The new wet-front pairs
change 27,263 D3D12 and 26,634 Vulkan pixels by more than 3/255, below the projected
receiver horizon. Existing actor, oblique, dry and legacy controls pass with their
original thresholds. Dry/disabled controls remain pixel-identical.

Verification includes 32 native reflection/HDR/material checks (the initial two
off-screen fixture failures and their corrected luminous-target reruns are
retained), eight shared material contracts, 13 research tests and seven game CPU
contracts. Eight station/battle runtime/editor views pass existing content,
fixture, allocation and API/host guards. Station state is exact against the
retained baseline, and both APIs visibly change. Battle rendering state is exact;
the historical helper used .10 for the inactive exploration camera pitch versus
the current .18. That inactive-camera difference is recorded, not counted as
full-state equality. Both normal editor API launches pass, and all three staged
plugins match their current build hashes. Source captures remain untouched.

Autochess's paired editor/plugin and standalone runtime/test builds pass, as do
four affected CPU contracts, both native character cohorts on both APIs (four
captures), and all 17 editor material cases on both APIs (34 captures). Original
content/image limits are unchanged, including both review lighting presets,
fire/eyes, pale wings and bird flight. Evidence is that game's
`artifacts/lighting-foundation/reflection-response`, with `native-warm/captures`
and `editor-warm` as the accepted matrices. The initial PowerShell 7 image-tool
assembly failure and cold Vulkan startup timeouts are retained. A separate
preparation run allowed 600 seconds; the actual native/editor qualification then
passed with its original 180/240-second limits. The Vulkan cache copied into the
isolated warm-test data root has provenance recorded. This does not qualify cold
startup within those limits; first-compilation latency remains an explicit gap.

Matched heavy-wet-night 1080p runtime, 200 warm samples [30,230), RTX 2060 SUPER,
no concurrent GPU job or build during timing, exact scene/material/weather state:

| Backend | GPU before / after ms | Host before / after ms |
| --- | ---: | ---: |
| D3D12 | 19.157 / 19.203 | 19.345 / 19.376 |
| Vulkan | 19.448 / 20.022 | 43.796 / 44.267 |

These are full-frame medians, not isolated lookup costs. Vulkan's change includes
activating the previously bypassed receiver. D3D12 GPU p99 is 21.117 -> 22.338 ms
and host p99 22.685 -> 26.368 ms; Vulkan GPU p99 is 22.079 -> 22.169 ms and host
p99 54.137 -> 50.886 ms. No performance improvement is claimed. Both heavy-rain
views remain above a 16.67 ms GPU budget, and Vulkan remains host-bound. New
Vulkan pipelines required a lengthy first compilation before warm qualification.
No new GPU allocation, descriptor, asset, ABI or source-light intensity was added.

Evidence is `artifacts/lighting-foundation/reflection-response` in engine/game.
The game's `final` folder is the first pass, before the missing Vulkan call was
fixed; accepted evidence is `qualified`. Unchanged D3D12 files have hash-checked
copy provenance. `qualification-comparison.json` records state/image/cost details.
The reflection/probe blend and albedo-dependent indirect occlusion were identified
in the capture, but remain unqualified production targets. General SSR, temporal
reconstruction, probe blending and updated diffuse transport are not implemented
by this pass. Work remains uncommitted.

## Current result: static local-shadow cache

2026-10-19: the observed raw-distance cache/write/merge path is now integrated
into D3D12 and Vulkan and enabled for PokemonCyberpunk's immutable rigid field
snapshots. Characters, foliage, story actors and boundary props remain fresh.
Scene ownership, asset generation, transforms, exclusions and light projection/
slot identity guard reuse. OpenGL remains shelved; PCF retains all-fresh draws.
No existing interface layout, plugin ABI, fixture values or filter core changed.
See [STATIC_SHADOW_CACHE.md](STATIC_SHADOW_CACHE.md) for the contract and limits.

Verification: 54 native cache/fresh/forced-refresh fixture runs cover nine cases
on both APIs, including movement, removal, changing alpha coverage, projection,
scene replacement, source compaction and reset. All 18 synthetic cached/fresh
image pairs and all 18 reuse/refresh pairs are exact. Another 31 affected engine
checks, nine game CPU contracts and eight shared material contracts pass.
Eight wet station/battle runtime/editor pairs have exact reuse/refresh pixels
and scene states. Existing canopy, detailed-station, allocation and API/host
guards pass. Both APIs pass five same-session station/reference scene visits;
the final normal D3D12 launch passes and staged plugin hashes match the build.

Matched dry-night 1080p timing, 200 warm samples [30,230), no concurrent builds,
Cyberpunk closed, RTX 2060 SUPER, CPU diagnostics enabled throughout:

| Backend | GPU all-fresh / cached ms | Host all-fresh / cached ms | Local-shadow CPU all-fresh / cached ms |
| --- | ---: | ---: | ---: |
| D3D12 | 13.719 / 12.148 | 14.129 / 11.929 | 4.261 / 1.691 |
| Vulkan | 14.791 / 13.274 | 33.940 / 26.143 | 10.357 / 3.933 |

These are medians for this dry station view, not rain/battle or whole-game
guarantees. Vulkan remains host-bound above a 16.67 ms frame budget. The cache
adds 32 MiB on D3D12 and 64 MiB across Vulkan's two separately fenced banks.
Cold caches still draw the field and write the raw cache before fresh casters.

Raw RG16 distance storage is an additional quantization step versus the prior
all-fresh path. The eight wet captures differ from that old path by at most
0.00167533/255 mean and 18/255 peak (roughly .29% changed pixels in the largest
case). This is recorded separately from exact temporal reuse; no existing image
guard was relaxed. The first D3D12 timing reuse/refresh pair failed byte equality
at 3,811 channels, each by 1/255. An unchanged cached rerun matched both original
and repeated refresh runs exactly. Its cause is unresolved, rather than claimed
fixed. Both API timing pairs in the final report pass exact reuse/refresh checks;
the original failure and repeats are retained, with hashed copy provenance.

Engine evidence: `artifacts/lighting-foundation/static-shadow-cache`.
`final-fixtures` retains all 54 native cases from the final stable executable.
Game evidence: the same relative directory in PokemonCyberpunk, including
`cache-verification.json`, `performance-summary.json`, `performance-qualified/
provenance.json`, `performance-repeat/comparison.json`, scene-switch and normal
launch logs. An initial stale private-asset test passed after rebuilding its
existing reorganization edits. The game build is in documented local-development
dependency mode; lock pins were not changed. Work is uncommitted.

This closes live raw-cache production and static/dynamic combination. Retail
eligibility, invalidation, eviction and update scheduling remain unproven; the
engine uses its own conservative policy. These results do not establish a new
whole-lighting parity percentage.

## Research prerequisite: cache writer and finite-source projection

2026-10-19 research follow-up: the separate Cyberpunk2077Research checkout now
qualifies the previously missing cache writer against three draws in an eight-frame
outdoor capture. Both independent shader stages reproduce all 3,145,728 tested
RG16_UNORM channels exactly after clearing the outputs; 37 shadow/input readbacks
remain exact. The strict final-frame byte-equality gate is still `requires-review`;
an original-only repeat also shows small presentation variations. See that repo's
`lighting/shadows/CACHE_WRITER.md` and private `evidence/shadow-cache` results.
This supplies measured cache-production and layer-reuse evidence, not the full
retail invalidation/eviction policy. At that research checkpoint no engine/game
runtime code had changed; the production integration and controls are recorded
above.

2026-10-18: The finite-source shadow viewpoint and range now follow the recovered
caster/filter/receiver contract. The station keeps its existing 150-degree
coverage. Its shadow line length agrees with its direct-light representation,
and the range includes the viewpoint shift once. The live receiver now clamps
radial distance to [0,1], fixing false empty-map occlusion near long-light ends.
No source intensities, geometry, ABI, filter kernel or target allocations change.
See [SHADOW_PROJECTION.md](SHADOW_PROJECTION.md) for evidence and qualifications.

The engine helper matches the retained 120-degree caster within 0.00113 texel
at 1024 resolution over 10,000 fixed-seed points. All 122 original exported
source hashes are preserved; the restored shadow array is exact and later
composition stays within the existing two-half-ULP replay envelope. The four
captured cache arrays are byte-identical and have only read-only direct bindings.
Cache construction/invalidation remains unobserved and unimplemented; all local
casters are still rendered each frame. This is a measured projection component
comparison, not a whole-renderer percentage.

All 171 affected engine checks and the six game CPU/eight shared-material
contracts pass. The strengthened long-source edge assertion was rerun on all
three APIs and passes. All six station and six battle captures pass in runtime
and editor, together with normal editor launch, seven scene controls, seven
reflection controls and four isolated-color captures. Source-off pixels remain
exact; maximum station API/runtime-editor MAE is 1.077203/0.00001370 out of 255.
Autochess's six runtime and six editor captures pass the existing content, parity
and baseline guards. Model support and Autochess source remain unchanged.

Final evidence is under `artifacts/lighting-foundation/shadow-projection` in
each repository. The first scene batch with a stale staged DLL and the later
pre-clamp diagnostic captures are retained but are not final evidence. Final
game captures explicitly verify built/staged DLL equality. This pass adds no
render passes or target allocations; it does not claim a measured speedup.
First-use shader startup still takes minutes, and the preceding GL cost
variability and Vulkan host-frame limits remain open. Static-shadow caching is
the next implementation gap; its creation/invalidation policy needs either new
retail evidence or an explicitly Phlosion-owned correctness contract.

## Previous follow-up: live distance-moment shadows

2026-10-18: The independently recovered filter now runs in native GL, Vulkan and
D3D12 lighting. PokemonCyberpunk selects it for the six canopy sources in normal
exploration and battle; `PHLOSION_USE_PCF_LOCAL_SHADOWS=1` retains the previous
comparison. Model/editor ABI is 19/39, with rebuilt and staged consumers.
See [LIVE_SHADOW_MOMENTS.md](LIVE_SHADOW_MOMENTS.md) for the adapter and evidence.

Two ordered RG16_UNORM passes feed radial surface, haze and reflected-view
receivers. The qualified core is unchanged. Live integer texel addressing,
Phlosion's existing 150-degree projection/source mapping and rendering all
casters every frame are explicit differences from the captured adapter/cache
policy. This step integrates the measured filter; it does not establish complete
shadow-system or whole-engine parity.

The final stable rebuild passes 169 affected engine checks and six game CPU
contracts. All six station captures pass unchanged source/content/image guards;
normal editor launch and all six battle captures also pass. Moment selection is
verified in actual exploration and battle handshakes. Seven scene controls,
seven reflection controls, four isolated-color captures and five-visit editor
scene switches on all three APIs pass. Source-off pixels match the preceding
checkpoint exactly. Autochess's six runtime and six editor views pass the
existing content, parity and baseline checks, together with eight material
contracts and the paired ABI proof. Source code in model support and Autochess
is unchanged. The retained wrapper-only stale-exit-code failure is explained
in the live integration report; native captures were successful.

Matched 1080p PCF/moments GPU medians are GL 40.315/42.983 ms,
Vulkan 21.740/21.158 ms and D3D12 21.180/20.827 ms (200 warm samples each).
GL's first +2.668 ms GPU/+2.536 ms host increase exceeds the existing review
limits; a reverse-order repeat measures +0.463 ms GPU/-0.314 ms host. Both are
retained; variable GL cost remains a review item. Vulkan host median is
52.748 ms. First-use shader/scene startup still takes minutes. Functional
integration is accepted with those limits, not as a 60 FPS qualification.
Next parity targets are source/projection mapping and static-cache policy;
the latter needs evidence beyond the already-populated captured cache.

## Previous follow-up: recovered shadow moment filter

2026-10-18: The independent engine-owned distance-moment kernel passes the
captured first-frame comparison. Horizontal, vertical and chained outputs each
match all 2,097,152 RG16_UNORM components bit-exactly, including a control that
clears outputs before independent dispatch. Untouched layers and the restored
original shadow array are exact; later composition remains within the retained
two-ULP replay envelope. See [SHADOW_MOMENTS.md](SHADOW_MOMENTS.md) for coverage,
private reproducible commands and the addressing precision limitation.

Thirty-two public synthetic filtering/visibility cases pass on GL/Vulkan/D3D12;
the existing finite-light cases also pass using the shared native fixture runner.
The normal game remains on the accepted PCF shadow path. No plugin ABI, source
intensity, weather, scene or production rendering behavior changed in this step.
Next: live moment targets and filtering with radial receivers, API addressing
qualification, blocker controls and the established scene/shared-consumer checks.
This is measured filter agreement, not complete shadow-system or engine parity.

## Previous follow-up: live finite-source response

2026-10-17: The shared surface shaders now support an explicit finite-source
response on physical rectangle lights. PokemonCyberpunk selects it for the six
canopy fixtures; the engine default remains LTC and the game has an LTC review
override. The adapter uses rectangle area times radiance for normal intensity,
an inscribed line/sphere, one-sided angular emission and authored material F0.
Geometry, source colors, weather and normal launch cycles remain unchanged.
Model/editor ABI is now 18/38; all consumers require rebuilding and staging.

See [LIVE_FINITE_LIGHTS.md](LIVE_FINITE_LIGHTS.md) for the full contract and
acceptance record. The production guard replay retains the measured component
accuracy (0.000065974% diffuse / 0.000029806% specular aggregate L1), including
the same four/two shadow-edge outliers. All three native numerical/domain fixtures,
129 affected engine checks and six game CPU contracts pass. The six station,
six battle, seven day/dry/off/oblique, seven scene-reflection and four isolated
source captures pass their existing guards. All three scene-switch probes and
the normal project launch pass. Autochess's six runtime and six editor material
views pass with the accepted baseline. Its first PowerShell 7 verifier failure
and Vulkan cold-start timeout are retained; Windows PowerShell and a longer
artifact-only startup allowance resolve those harness limits without changing
image gates. Cold startup remains slow. Existing shadow filtering, bounce, haze
and reflection transport are not recovered Cyberpunk implementations.

2026-10-18 acceptance: paired 1080p wet-night GPU medians (LTC / finite) are
GL 46.147 / 42.882 ms, Vulkan 22.999 / 21.144 ms and D3D12 23.094 / 20.736 ms,
200 warm samples each. Recorded scene state differs only by the source-response
tags. These are full-scene observations, not isolated response timing or 60 FPS
qualification; Vulkan host median remains 54.717 ms. Live integration is accepted
with the declared startup/performance and captured shadow-edge limits. Next
measured parity target: shadow-map generation, projection and filtering.

## Previous follow-up: independent GPU local-light response

2026-10-17: `world_finite_light.glsl` independently implements the measured
finite-source distance, falloff, diffuse/specular response and moment visibility.
It is exercised by fixtures only; production material shaders do not include it.
No ABI, game source, staged runtime/editor binary, scene or launch default changed.

The private native replay runs the original retail PSO and the port against the
same captured inputs with directional RGB zeroed in both, then restores the
original constants and output resources before the captured frame continues.
Across all 873,728 active pixels, 99.98556% of the 5,242,368 RGB output channels
are identical. Aggregate relative L1 errors are 0.00006486% diffuse and
0.00002898% specular. Four diffuse pixels and two of the same specular pixels
exceed 2% relative error, with maximum 28.69% / 36.58%; all lie at shadow edges.
They remain an explicit limit, not removed from the aggregate or acceptance.
See [GPU_LOCAL_RESPONSE.md](GPU_LOCAL_RESPONSE.md) for coverage and interpretation.

Original local-light outputs are restored bit-exactly. Later composition differs
by at most two half-float ULPs, also observed in an ordinary no-port replay and
independent PIX export. The original strict preservation result remains
`requires-review`; a separate qualification records this measured replay
variation. GPU allocation addresses and tile order vary, while all 58,880 ordered
cluster light lists, dispatched tile sets and other inputs match the control.
All 122 original exported source files retain their recorded hashes.

Native OpenGL, Vulkan and D3D12 synthetic fixtures pass against independent
float64 reference values, 16 cases / 128 channels per API. They cover point,
finite line and sphere response, roughness, metallic response, back-facing
surfaces, radius-zero specular gating, falloff/cap/cutoff, moment visibility,
rotation and endpoint reversal. Maximum absolute errors: GL 1.15973e-6,
Vulkan/D3D12 7.47621e-7. Build warnings are errors. No performance measurement or
runtime/editor/shared-consumer qualification is implied by these isolated tests.

Private final case: `evidence/lighting-matched/gpu/local-o3-final-04/`, including
`case.json`, `qualification-v2.json` and source/binary hashes. The precision
experiment and initial strict failures remain retained. Engine evidence:
`artifacts/lighting-foundation/captured-local-gpu/`. Next: generic finite-source
contract, explicit canopy mapping and live material binding; preserve separate
LTC rectangles and require the existing scene/API/editor/shared-consumer checks.

## Previous follow-up: captured local-light CPU recovery

2026-10-17: User explicitly targets the actual light-to-surface calculation,
independent of station asset differences. Root-only cloud work continues; no
local model or delegated agent. Engine/game runtime source and launch defaults
are unchanged by this research milestone.

An owned hidden D3D12 replay now extracts the actual inputs and both HDR outputs
immediately around indirect dispatch parent 7084 (PSO 20727), with audited resource
transitions restored after readback. The original capture/export remains read-only.
The three previously unresolved CBVs were heap-relative addresses: b0 is resource
974, b12 is 956, b13 is 958; b6 is a 256-byte slice of upload resource 739.
GPU-written tile/cluster lists, 128-byte light records, G-buffer, shadow moments,
exposure and output textures are retained in the private research depot.

Current-GPU independent PIX composition export versus native replay differs in
246 of 3,686,400 half-float channels: 240 differ by one half-float ULP and six by
two. Maximum absolute difference is 0.000244141; aggregate relative L1 is
2.05148e-9. It is not bit-exact. The older GTX 1070 export has larger differences
and remains retained separately; do not silently substitute its qualification.

The readable CPU local-light model was compared at all 3,413 dispatched tile
centers, covering 53 light records and 13,478 evaluations (10,869 finite-size BRDF
evaluations, 566 with the shadow-enabled flag). With diagnostic captured surface
decoding and directional response retained, aggregate relative RGB L1 error
against the two captured GPU lobes is 0.04134%, maximum absolute HDR error
0.001953125. The independent local contributions versus diagnostic decoded
contributions differ by 0.003778% aggregate L1. These are defined component
measurements, not whole-engine parity or a production GPU port. Profile-texture
branches are present but no sampled light has a nonzero profile index.

A second deterministic sample uses a different pixel offset within each tile:
3,413 pixels, 54 light records, 13,543 evaluations, including 113 evaluations
actually attenuated by shadows. Aggregate RGB L1 error against GPU output is
0.044383%, maximum absolute HDR error 0.00390625; isolated local contributions
versus decoded execution differ by 0.001208% aggregate L1. No color/intensity
fitting was performed. CPU float32 interpretation, GPU optimization, half-float
storage and texture-filter precision remain numerical qualifications. Only this
captured shader permutation/frame is covered; the profile-texture path and other
lighting passes are not established by these results.

Evidence: private `evidence/lighting-matched/readbacks/baseline-01/` contains
`capture.json`, `current-gpu-qualification.json`, `local-model-comparison.json`
and the readbacks. `tools/lighting_matched/prepare.py`, `run_capture.py`,
`inspect_dispatch.py`, `light_model.py` and `compare_model.py` reproduce the work.
The fixed-seed offset validation and detailed handoff are recorded alongside it.
The GPU fixture described above completes the next recovery step. Production
integration and its runtime/editor/API/shared-consumer checks remain next.

## Previous follow-up: visible local-light scattering (accepted)

The repeated "75%" lighting estimate is withdrawn. It had no defined denominator
or measured image comparison. Historical percentages below are superseded, not
acceptance criteria. The last two passes improved frame cost without changing
the images; they must not be reported as lighting fidelity progress.

This pass targets the missing colored illumination of air around the canopy.
The retained retail night screenshot shows luminous haze around local sources;
Phlosion's matched baseline has colored surfaces but almost unlit air. The
retail screenshot and the earlier canopy concept establish the visual direction,
not a pixel-match target: geometry, weather and exposure differ. The retained
diagnostic composition PNGs use a non-retail transform and cannot establish color
parity. Capture shader types include `SVolFogConstants`; no exact Cyberpunk fog
algorithm or parameter match is claimed.

Acceptance: matched before/on/off native images, visible colored air with bloom
disabled, opaque foreground and source occlusion, complete rectangle extent,
RGB addition, zero-density/source-off preservation, view/sky continuity, and
all three APIs in runtime/editor. Keep source geometry, direct light strengths,
weather clocks and rain effects unchanged. Independent numerical area/volume
integration checks the new shader in a public fixture. Record the actual visual
result and cost after validation; do not replace the withdrawn number.

Implemented an opt-in rectangular-source integral in the shared height medium,
with source visibility from the local shadow atlas. The station uses scattering
albedo 0.55 and anisotropy 0.25. Ordinary materials retain extinction and defer
scattering to a quarter-width/quarter-height HDR pass. Depth-aware composition
protects foreground silhouettes; reflected and main views have separate view
integrals. Model plugin ABI is 17 and editor project ABI is 37; consumers require
rebuilt DLLs. The authored scene and source intensities are unchanged.

The isolated air captures disable falling rain and bloom. The haze-off image is
pixel-identical to the prior checkpoint. Open air under the canopy gains blue;
air outside the fascia gains pink, while the foreground remains sharp. Across
all APIs, the reduced pass differs from the retained full-resolution reference
by approximately 0.10/255 mean absolute error, with 99th percentile 1/255. This
compares two Phlosion implementations, not Phlosion against Cyberpunk.

The first full-resolution implementation was rejected for cost: D3D12 added
46.699 ms at 1080p. The first reduced pass still added 12.529 ms because the
compiler evaluated its expensive fallback. An explicit branch preserves the
image exactly and skips that work for depth-matched pixels. Paired heavy-wet-night
timings use 200 warm GPU samples on an RTX 2060 SUPER, with CPU builds and other
GPU jobs stopped. Final reduced-pass medians (off/on) are 20.578/22.563 ms on
D3D12, 20.353/24.688 ms on Vulkan and 39.661/47.006 ms on OpenGL. These are added
feature costs, not speedups or 60 FPS qualification. Host tails remain material.
Vulkan's earlier synchronization pair was 20.555/24.678 ms; the final repeat also
includes the rain-only specialization guard. Final D3D12/Vulkan/OpenGL GPU p99 values are
25.450/28.429/86.688 ms, and host medians are 23.413/59.932/49.369 ms. Ordinary
desktop applications remained open; this is a paired development measurement.
Cold compilation is also substantial: the first shared Autochess D3D12 capture
process took about six minutes, dominated by building previously uncached material
variants. This is not an isolated compiler benchmark or a warm launch measurement.
Completed source/entry-point/target/compiler-flag-keyed DXBC blobs were reused for
the editor checks after checking their header length and SHA-256 copy equality;
no existing cache entries were overwritten. See `shared-shader-cache-reuse.json`.
The first shared Vulkan asset-preview check exceeded its unchanged 240-second
limit during cold material preparation. Preserve that failure under Autochess
`editor-cold-timeout`. A separate shader-preparation diagnostic completed and
produced the native preview in 72.3 seconds; all six original editor checks then
passed with their timeout and image/content thresholds unchanged. This does not
qualify cold-start performance.
The raw rejected trials remain in `cost` and `reduced-cost`. `branch-cost` keeps
the initial optimized runs; `pre-volume-guard-cost` retains the prior Vulkan pair.
`final-cost` retains the final Vulkan repeat and exact
copies of the unchanged D3D12/OpenGL runs, with provenance recorded alongside them.

Completed checks: 127 engine contracts/native probes pass, including all 76 haze
checks and the existing planar/HDR/material/ownership checks. The independent
emissive-reflection oracle passes. Five game environment/rain/weather contracts
pass. All 36 station/air/battle/control/signage captures pass their existing
expected-content and API/host thresholds. All twelve shared character images
(six runtime images and six editor model crops) exactly preserve the previous
checkpoint, with no Autochess or PokemonModelSupport source changes. Both editor
build directories and the staged game/model plugins have been rebuilt.
The final Vulkan rain-only specialization excludes the haze dispatcher, allowing
unused integration code to be discarded. All 40 affected Vulkan haze, planar and
HDR probes pass again. Eight further shared runtime/editor captures (GL/Vulkan)
pass unchanged thresholds and exactly preserve their baseline images; D3D12 is
unchanged by this last shader condition. Six sky-only API/host captures pass.
Five same-session station/reference visits pass on each API, preserving resource
stability and clearing station lighting in the reference scene. The 120-second
day and 180-second Hybrid weather defaults remain intact. The final five Vulkan
field/battle/timing images exactly preserve their earlier counterparts. The normal
D3D12 project-launch probe passes with Hybrid rain and the CP audio selection.
The visual change is accepted with the performance and cold-start limits above.

Limits: bounded single scattering; emitter-centre phase/source attenuation;
the existing shadow proxy instead of source-sized volumetric penumbrae; no
scattering from point/sign lights, temporal volume history or multiple scattering.
The separate rain billow/drop simulation is unchanged. General reflections,
dynamic indirect light and softer area-light shadows remain visual gaps.

Evidence roots are `artifacts/lighting-foundation/local-haze` in engine, game and
Autochess. Game `accepted` contains the reduced-pass native matrix; `final` retains
the earlier full-resolution reference. Reproduce with `Verify-Local-Haze.ps1`,
then `verify_local_haze.py accepted --baseline before --reference-root final
--cost-root final-cost` using full paths beneath that evidence root. Follow with
the existing station rectangles, battle, canopy and signage validators. Engine
`PhlosionLocalHaze*` probes compare independent numerical area/view quadrature,
foreground depth, source occlusion and target reuse across all APIs. Exact model
and editor compatibility proof is retained in Autochess `paired-build-current`.

## Previous follow-up: Vulkan submission (performance only)

Starting from engine ebe4d1b / game b23efa5, profile current-thread CPU time and
repeated Vulkan submission work. Material lookup was the main measured cost;
locale-heavy string-stream texture-key construction is replaced with `to_chars`
and direct string appends. Cache identities, lifetimes, resource scopes, GPU
commands, lighting and scene content remain unchanged. Temporary profiling is
removed from the production code.

Matched 1080p heavy-wet-night host median falls 73.569 to 49.943ms (32.1%) on
Vulkan. A diagnostic repeat reduces mean current-thread CPU time from 50.078 to
30.156ms. Timing images and scene state remain pixel-identical on all APIs, with
unchanged warm resource totals. GL/D3D12 keep their existing implementation and
show only small run variation; the default D3D12 launch gains no new speedup.
Vulkan host p99 remains 76.150ms, so 60 FPS is still unqualified. GPU duration also
falls, but shader work is unchanged and clock coverage is incomplete.

56 engine checks and five game contracts pass. All 36 public reflection images,
27 station/battle/control/signage images and twelve shared character images are
unchanged. Twelve native shadow probes, all API/host content/parity checks and
normal D3D12 launch pass. Both engine editor builds and staged game/model plugins
are current. Blender/GLB hashes and 120s daylight / 180s Hybrid rain defaults are
unchanged; ModelSupport and Autochess source remain untouched. The optimization
is accepted for the next engine/game checkpoint. See [SUBMISSION.md](SUBMISSION.md) and each repository's
`artifacts/lighting-foundation/shadow-submission` for the exact method, diagnostics,
temporary profiler, matched controls and verification evidence.

## Previous follow-up: frame cost

Starting from engine fa7ae72 / game 45f42ca, the lighting shader checks local
shadow visibility before LTC integration on Vulkan/D3D12 and skips only exactly
zero contributions. OpenGL retains its previous ordering after an inconsistent
trial. Light strengths, shadow quality, scene content and material ABI are unchanged.
Game tracing now separates field/battle reflection host work; it remains opt-in.

Matched heavy-wet-night 1080p images are pixel-identical on all three APIs. Final
GPU control/candidate medians are 38.301/37.909ms GL, 24.154/22.264ms Vulkan and
20.257/19.893ms D3D12. GL's small difference is variation, not an optimization
claim. Earlier Vulkan savings were smaller; D3D12's final saving is .364ms (1.8%).
Host tails and repeated shadow/submission costs remain substantial. This is not
60 FPS qualification or a new numerical Cyberpunk parity claim.

55 engine checks, five game contracts and all 36 public reflection-image equality
checks pass. All 27 field/battle/control/signage images exactly preserve the prior
checkpoint, and all twelve native shadow probes pass. Normal launch and shared
runtime/editor material checks pass; all twelve shared images preserve the prior
checkpoint exactly. Both editor build directories and staged game/model plugins
are current. Source Blender/GLB hashes and 120s daylight / 180s Hybrid weather
defaults are unchanged; ModelSupport and Autochess source remain untouched.
The small optimization is accepted and saved as the next engine/game checkpoint.
See [FRAME_COST.md](FRAME_COST.md)
and each repository's `artifacts/lighting-foundation/frame-cost` for the exact
inputs, rejected/outlier evidence, raw timing and final verification status.

## Previous follow-up: reflected emission and roughness

The `reflection-quality` follow-up retains sign/screen emission in planar captures,
with game-owned capture overrides suppressing only the six analytic canopy
emitters. A normalized aspect-correct nine-tap Gaussian replaces the five-tap
cross. The existing shader emission hook is retained for composed-material
compatibility. No light strengths, authored assets, weather or provider ABI change.

The final 49 engine checks and five game contracts pass. All twelve shared
Autochess runtime/editor images and all 36 public reflection images preserve
their respective baselines exactly. Station, battle and oblique signage pass
all three APIs in runtime/editor at unchanged thresholds. The bloom-disabled
sign-emission control changes 691 pixels in an independently projected authored
puddle, with other lighting held constant. Same-session editor switching passes
five station/reference visits on each API, with stable resources and no stale
station light/bounce state in the Colosseum reference.
The normal D3D12 editor launch also passes with Hybrid rain and CP audio;
the 120s day / 180s rain defaults and source Blender/GLB hashes are unchanged.

Functional verification passes; performance remains flagged. Final 1080p heavy-rain
GPU off/on medians are 32.889/37.988ms GL, 21.274/24.588ms Vulkan, and
16.982/20.166ms D3D12. Every API exceeds the existing incremental review guides;
this is not sustained-60-FPS qualification. See REFLECTIONS.md and the engine/game/
Autochess `artifacts/lighting-foundation/reflection-quality` roots for controls,
tails and cold-start limitations. Broader reflection coverage, proper roughness
prefiltering and submission cost remain open. The approximately 75% foundation
estimate remains qualitative; it is not measured Cyberpunk parity.

## Earlier foundation and reflection checkpoints (historical)

The authorized foundation checkpoint is committed: engine `ef447b4` / `47a2cab`,
PokemonModelSupport `e4039c9`, Autochess `eef446d1`, and PokemonCyberpunk
`6fcfbb2` / `3dfe5a7`. Game dependency pins match those shared-source revisions.
No pushes were requested or performed.

The first reflection pass added live scene reflections on flat wet forecourt receivers.
It is functionally verified in all three APIs and both hosts. Its matched heavy-rain
GPU cost is +4.68/+3.58/+3.02ms (GL/Vulkan/D3D12), above the existing guide;
performance optimization remains required. The redundant initial sky draw is
removed, with all twelve final field/battle images preserved pixel-for-pixel.
See [REFLECTIONS.md](REFLECTIONS.md) for its contract, native acceptance and cost.
It is saved as the next authorized engine/game checkpoint; it does not change
PokemonModelSupport, Autochess source, authored assets, rain or the six light
strengths. Broader SSR/probe/RT coverage and emissive-sign reflections were still open
at that checkpoint; the current follow-up above addresses the latter.

The subsequent trial targeted reflected-scene submission cost before expanding coverage.
Preserve the current appearance, compare matched on/off timing, and retain the
existing performance guides. The roughly 75% foundation estimate discussed with
the user is qualitative feature coverage, not measured Cyberpunk image parity.
The below-plane static-culling trial was visually exact but did not establish a
useful speedup, so its runtime changes were removed. REFLECTIONS.md retains the
measurements and rejected-candidate evidence; the frame-cost flags remain open.

## Colored diffuse bounce — previous verified pass

The engine bakes one diffuse bounce from static glTF geometry into 32 directional
irradiance probes beneath the canopy. Six independent unit-source bases are
relit from live pink/blue fixture radiance; source-off gives exactly zero bounce.
Characters and shaded surfaces receive the result. Generic bake, math and all
three backend bindings belong to the engine; volume/source selection belongs to
PokemonCyberpunk. No new fill lights or authored asset changes. Moving objects
do not update transport, and this is not full GI or scene reflections.

Four engine CPU contracts, five game contracts, nine native HDR tests and 64
public/station/wet/battle/control captures pass with unchanged limits. Twelve
Autochess runtime/editor material captures pass; five editor images match the
earlier HDR baseline exactly, and the sixth differs by one channel byte. The
standalone Autochess executable was rebuilt explicitly; an earlier stale-binary
attempt was rejected. Scene switching passes on all APIs, five visits each,
including clearing bounce in the Colosseum reference. The final normal D3D12
launch passes and retains the 120s day / 180s Hybrid rain cycles and CP audio.

Fresh 1080p clear-night timing on RTX 2060 SUPER, Cyberpunk closed, 200 warm
samples per control, all shadows/HDR/bloom retained:

| API | GPU bounce off/on ms | Host off/on ms |
| --- | ---: | ---: |
| OpenGL | 26.336 / 27.602 | 28.636 / 29.841 |
| Vulkan | 14.666 / 14.970 | 49.956 / 48.739 |
| D3D12 | 10.363 / 11.931 | 10.689 / 11.939 |

D3D12 reverse-order repeat: GPU 10.474 / 11.683ms, host 10.773 / 11.962ms.
Its added GPU cost spans 1.21-1.57ms, with the first result slightly above the
existing +1.5ms review guide; both results remain recorded. No threshold change.
These medians do not establish sustained 60 FPS: host p99 exceeds 16.667ms on
every API, with GL throughput and Vulkan CPU-side cost still needing work. The
toggle measures current-shader execution, not total cost versus a pre-change
binary. The machine changed from GTX 1070 to two RTX 2060 SUPER cards; do not
compare those generations as an optimization result. Only GPU 0 rendered.

Vulkan now reuses and persists a bounded, device/driver-validated pipeline cache.
CPU corruption/compatibility contracts and native load/store checks pass. Warm
normal editor launches were about 34s in retained logs; first material compiles
can still take minutes. This pass used cloud execution only, without local AI
or delegation. See [BOUNCE.md](BOUNCE.md) for architecture, exact limitations,
startup evidence and game `artifacts/lighting-foundation/bounce/rtx2060` reports.

The preservation snapshot has 422 files: 387 unchanged, 35 intentional engine/game
overlaps and 14 new files. Model-support and Autochess source are unchanged.
Canonical Blender and private station.glb hashes remain unchanged. Binaries are
rebuilt/staged and this work is checkpointed at the user's request. At that
checkpoint the wet highlights represented only light sources. The subsequent
scene-reflection work is recorded above and in REFLECTIONS.md.

## Static shadow culling — previous accepted pass

Generic glTF import now computes primitive bounds from decoded positions. The
draw adapter optionally rejects static PBR primitives wholly outside the supplied
pass frustum, before material/backend submission. Field and battle supply each
actual shadow matrix for the environment only. Skinning, custom vertex materials,
prewarm and absent/invalid bounds retain their existing paths. Shaders, light
settings, source geometry, rain and camera behavior are unchanged. No new workers;
root continues under the user's recorded implementation fallback approval.

Four engine CPU tests (including the expanded import/draw contract) and five game
environment contracts pass. All 19 station/battle/control images are pixel-identical
to the accepted HDR baseline; six low-sun API/host captures and the culling-off
control pass. Existing source/packet, battle, wet reflection and bloom validators
pass. Scene switching passes on all three APIs (five visits each); the final normal
D3D12 launch passes and restores the station, 120s day cycle and 180s Hybrid weather
cycle. Engine `build-presentation`, game runtime/editor and model provider targets
were rebuilt and staged together. No shader/backend change required repeating the
previous Autochess material matrix; its separate paired binaries were not replaced.

Matched clear-night 1080p tests retain HDR, bloom and all shadows, 200 warm samples:

| API | GPU off/on culling ms | Host frame off/on culling ms |
| --- | ---: | ---: |
| OpenGL | 41.115 / 36.573 | 42.801 / 38.233 |
| Vulkan | 23.194 / 22.778 | 63.291 / 48.456 |
| D3D12 | 21.721 / 12.701 | 21.982 / 12.943 |

Reverse-order D3D12 repeats at GPU 21.632 / 13.280 and host 21.939 / 13.872 ms.
GL/Vulkan performance images are identical; D3D12 has maximum one-byte difference.
These are medians, not sustained 60 FPS or rain/battle guarantees. Frame spikes and
Vulkan host cost remain. Host timings include waits. See SHADOW_CULLING.md and game
`artifacts/lighting-foundation/shadow-culling` for exact evidence and limitations.

Before-state snapshot includes 117 files across four repositories (dirty work and
affected clean inputs). 106 are byte-identical; the eleven intentional changes are
five engine glTF/test files, game field/battle presentation, its capture helper and
test plan, and this checkpoint/PLAN. Earlier lighting/material/model edits remain
intact. New files are the culling design and two game verification tools. All
changes remain uncommitted. Next visual work is colored indirect light and actual
scene reflections; further Vulkan performance investigation remains worthwhile.

## Current changes

PokemonCyberpunk `tools/outskirt_station_lighting.py` rotates pink physical
diffusers/channels/clips to emit outward and 60 degrees down, below the fascia.
All six source colors/radiance/emission/condition settings are preserved, with
no added lights. Blue and non-canopy fixtures, point lights, shelters and puddles
match the retained baseline. The normal private station export was regenerated;
canonical Blender source hash is unchanged, 196 meshes / four pumps / four columns.
`verify_station_rect_sources.py` now checks visible normals, corner joins,
18 independent interior area-integral samples at two heights, exterior receiver
orientations and a zero-direct-light back-facing control. New
`Verify-Canopy-Overlap.ps1` captures four isolated source modes under the canopy.
Game HANDOFF, TEST_PLAN and EMISSION_PASS document this step.

No engine rendering code or ABI was changed in this phase. All 52 saved pre-existing
modified/untracked files across the four repositories still matched their baseline
copies at review. The earlier material ownership cleanup remains uncommitted.

## Verification complete

- Exported source geometry/normals/extent/corner oracle passed (maximum centreline
  corner error 0.000001404 m). All 18 interior and external orientation checks pass.
- OutdoorProfile and WorldEnvironment CTests pass, two of two.
- All six wet-night station runtime/editor captures passed the existing source
  packet, fixture, allocation and parity oracle. Maximum API MAE 1.36518/255
  (limit 1.5); maximum host MAE 0.00000343/255.
- All seven D3D12 controls completed. Existing analytic reflection, puddle/dry
  state and localized-bloom checks passed at front and oblique camera angles.
- All six rainy battle runtime/editor captures passed the existing geometry,
  fixture, weather, shelter, allocation and backend parity checks.
- All four under-canopy source controls passed matching player/camera/import
  state, dry surfaces, zero point radiance and expected rectangle counts. The
  reviewed character interior has 1,303 pixels responding to each source group
  independently out of 2,408 sampled pixels. The close camera retracts through
  the established collision behavior; no new camera behavior is claimed.
- Visually reviewed source-off, pink-only, blue-only, both, dry/oblique and battle
  views. Final total: 23 native captures and two CPU contracts, all passed.
  No performance qualification or new generic renderer feature is claimed.

Evidence is in game `artifacts/lighting-foundation/emission`; stage logs are
`artifacts/lighting-foundation-emission-{matrix,controls,battle,overlap}.log`.
Native exec sessions 33367 (export), 68476 (station matrix) and 9420 (remaining
captures) exited successfully. `overlap/comparison.png` and `verification.json`
retain the source-isolation evidence. Changes are left uncommitted.

## Routing and remaining renderer work

The installed doctor saw root gpt-6-astra and pinned astra_flash_builder. Actual
worker `/root/canopy_emission` failed immediately with HTTP402 insufficient credit
and was interrupted. Root implemented the fixture bundle under the user's existing
approval to continue with the current model. No worker implementation was accepted.

At phase 1 closure, local shadows, HDR, colored indirect light and scene reflections
were unimplemented. That review identified RGBA8 world targets, per-material
mapping and format-bound PSOs as constraints. The following shadow/HDR sections
record their implementation and compatibility checks. Colored indirect light and
scene-object reflections remain later work. No new Cyberpunk capture or hardware
RT was required for the fixture step.

## Local shadows (2026-10-16)

User authorized proceeding toward the approximately 75% conventional lighting
milestone. LOCAL_SHADOWS.md specifies the first renderer bundle. Current work is
uncommitted and functionally verified, with performance limitations below. Pre-existing 60 files across four repositories
were copied with hashes to artifacts/lighting-foundation/pre-local-shadows.

Implemented optional IWorldLocalShadow, shared atlas/projection policy, 4096 D32
atlas (2048 sun plus eight 1024 local tiles), three backends, shader visibility
for rectangular diffuse/specular contributions, field/battle/calibration routing.
Added bright public `RectLightCalibration shadow` plus independent ray/box oracle.
Corrected foliage CPU culling to use each actual light matrix (including sun).
At this stage there were no HDR/bloom or indirect/reflection changes. Root performs work under
prior approval after the already documented DeepSeek 402 failure; no new worker.

Initial and final engine/game builds passed. PhlosionLocalShadows,
PhlosionRectLights, CyberpunkOutdoorProfile and CyberpunkWorldEnvironment pass.
All twelve public probe on/off captures and all policy controls pass the
independent ray/box oracle. The skinned control was corrected to compare against
its matching moved-pose off capture (not the stationary cube); no image threshold
was relaxed. D3D12 has 294 shadow samples with median reduction 64/255 and 1,416
clear samples with median change zero. Twelve dry station on/off captures also
pass API/host parity, maximum API MAE 0.60834/255, with no allocation failures.

OpenGL's first local-shadow run exposed a major cost regression: roughly 212 ms
versus 58 ms off at 832x468. Temporary opaque-only, receiver-disabled and foliage-
disabled probes isolated the dominant problem to the vertex material dispatcher
for repeated wind-deformed shadow geometry. Specializing BOTH vertex and fragment
material selectors preserves the same shader/discard/skinning path and produces
an identical station image (MAE 0, max 0), at approximately 60 ms with shadows.
Fragment-only specialization did not solve it. The temporary opaque program,
receiver bypass and palette-unroll experiments were removed. The accepted approach
caches generic per-material shadow programs and prepares them through the existing
material prewarm path. It contains no station coordinates or foliage mode numbers.
All GL public probe/policy controls were rerun after this change and pass.

The user closed Cyberpunk 2077. Clean paired 1080p tests supersede all earlier
provisional timings: game `artifacts/lighting-foundation/local-shadows/clean-gpu/
performance-summary.json`. Clear night, bloom off, 200 warm samples [30,230):

| Backend | GPU off/on ms | CPU host off/on ms |
| --- | ---: | ---: |
| OpenGL | 18.622 / 36.435 | 20.266 / 38.137 |
| Vulkan | 15.212 / 21.205 | 28.619 / 72.622 |
| D3D12 | 8.544 / 19.962 | 9.478 / 20.272 |

These shadow-only measurements predate HDR. They establish substantial GPU and
CPU cost, not 60 FPS qualification. Vulkan local submission alone is 36.708 ms.
Retain the original failed/experimental logs. Wet station (six), rainy battle
(six), and D3D12 off/dry/day/oblique controls (seven) completed and passed existing
validators. Wet maximum API MAE is 1.31/255 at the unchanged 1.5 limit.

## HDR scene acceptance

RGBA16F world targets and compatible direct/scene pipelines are implemented on
all three APIs and editor surfaces. Optional IWorldSceneRadiance resets per view;
the game opts outdoor views in. Shared bloom receives unclipped radiance, then
ACES, sRGB and display dithering run once. Sky remains unclipped. Feedback maps
into its existing RGB8 history before authored feedback. Other consumers retain
their mapping convention (float intermediate storage can change quantization).

All nine native intensity/blend/bloom/feedback tests and three legacy feedback
tests pass. Eight engine CPU/shader architecture checks and five game environment
contracts pass. Original failures are retained: SDL_setenv did not set the CRT
capture environment; the feedback test then exposed a real OpenGL first-draw
buffer initialization bug, now fixed. Sky clamping found during review is fixed.

Engine build-presentation editor, game runtime/plugin and model providers are
rebuilt and staged. Autochess engine-build editor/plugin paired proof and separate
runtime rebuild pass. The first paired build was correctly rejected because
source changed during compilation; the stable second build passes.

Final HDR wet station (six), rainy battle (six), and D3D12 controls (seven) pass
under game `artifacts/lighting-foundation/hdr/native`, with unchanged validators.
Maximum API MAE is 1.30917/255; maximum runtime/editor MAE is 0.0000155/255.
The independent front/oblique emitter projection oracle, puddle/dry response and
localized halo checks pass. Visually reviewed night, lights-off, daylight and
battle images. All nine native radiance cases pass again after strengthening the
test to delete stale screenshots and reject exposure above the supported range.

Final clean 1080p timing with HDR enabled, bloom off, 200 warm samples [30,230),
all GPU runs sequential and Cyberpunk closed:

| Backend | GPU shadows off/on ms | CPU host off/on ms |
| --- | ---: | ---: |
| OpenGL | 22.529 / 39.054 | 24.163 / 40.777 |
| Vulkan | 15.316 / 20.984 | 26.206 / 64.490 |
| D3D12 | 14.852 / 20.214 | 14.919 / 20.422 |

D3D12 with normal bloom enabled: GPU 21.536 ms, CPU host 21.828 ms. These are
clear-night station measurements, not rain/battle/whole-game frame guarantees.
Do not infer isolated HDR overhead from separate earlier runs. This is not a
60 FPS setting across APIs. Vulkan's local-shadow CPU submission is 29.850 ms;
repeated geometry submission and GL's shadow cost remain optimization targets.
Full raw samples and matching-camera/import checks are in game
`artifacts/lighting-foundation/hdr/final-performance/performance-summary.json`.
RGBA16F adds four bytes per world-target pixel (~7.91 MiB at 1080p); opting into
the 4096 D32 shadow atlas adds 48 MiB over the original 2048 map. No allocation
failures occurred. The settings allocate targets on preparation/resize, not per
frame.

Preservation audit: 53 of 60 pre-existing dirty files remain byte-identical. The
seven intentional overlaps are three generic material HDR gates, engine CMake,
PLAN/CHECKPOINT, and game TEST_PLAN. The three material files differ from their
pre-HDR copies by one radiance-mode gate each; prior ownership cleanup is intact.
All other pre-existing model/Autochess edits remain unchanged.

All three API scene-switch checks pass (five station/reference visits each).
The final D3D12 normal launch passes and restores Hybrid rain / day-night defaults.
Game logs are `artifacts/lighting-foundation/hdr/scene-switch-*.log` and
`normal-launch.log`. Both engine editor build directories and staged game/model
plugins are current; no visible editor was left running.

Autochess native checks passed: six runtime cohort images and six editor body/eyes
and stage images, including comparison against the material-ownership baseline.
Existing content, API parity and baseline limits all pass. Visually reviewed a
runtime cohort and the editor model view. Final staged Cyberpunk/model DLL hashes
match their rebuilt artifacts, and engine/game/Autochess diff checks pass.
The first artifact-only retry helper failed because its new results directory did
not exist (the historical helper assumed an earlier capture run had created it).
The PNG and error are preserved; explicit directory creation fixed log capture.
The production capture helper and image limits remain unchanged. Runtime startup
is allowed 450 seconds for cold shader variants, as in the earlier qualification.
No engine/game source change was required for this harness issue. Final summaries
are in Autochess `artifacts/lighting-foundation/hdr/runtime/captures/matrix-report.json`
and `editor/report.json`. All native sessions have completed and no game/editor
process was left running. The next engineering priority is reducing repeated
shadow submission cost before increasing lighting complexity.
No colored indirect bounce or scene-object reflections are implemented. Do not
claim the approximately 75% target as measured parity. All work is uncommitted.
