# Contact shading and reflection visibility completion - 2026-10-31

**100% estimated technique coverage of the fixed C1-C8 supported Windows
contact contract.** The ordinary non-RT perspective game/editor path is qualified
on D3D12 and Vulkan. This is mechanism coverage, not retail framebuffer/FPS
identity, whole-engine coverage or Linux qualification. C1-C8 here names
CONTACT_COMPLETION_CONTRACT.md, not the broader historic lighting checklist.

## Implementation and ownership

The engine now prepares directional contact visibility once per view. Five
compute stages replace the ordinary fragment-side legacy approximation:
half-resolution depth/normal/velocity preparation, horizon integration, spatial
filtering, motion-aware temporal accumulation and full-resolution reconstruction.
Consumers perform a single prepared lookup rather than reconstructing the cone
for each material.

The producer uses compact R32F depth, RG16F motion and RGBA8 cone/normal storage.
The three horizon budgets are 5x3, 10x6 and 15x10. The game maps Medium/High/Ultra
to these levels; Low disables contact shading. There is no feature opt-in.
The normal shared surface supplies evaluated normals, camera/object/skinning/wind
motion and foliage/no-AO receiver flags. A lost no-AO bit in the D3D12 standard
material packer is fixed. Temporal material-class extraction masks the receiver
bits rather than treating them as a new temporal class.

History has independent identity, frame, generation, extent and quality/radius
validation, bounded to the existing eight view slots. Cuts, skips, missing motion,
changed views and retirement cannot borrow another view's history. GPU resources
retain their fence-safe lifetime. Jittered camera axes remain orthogonal.

Raw diffuse visibility still drives the reflection BRDF coefficients. Indirect
diffuse/specular and direct diffuse/specular then receive independent strengths.
Diffuse lighting uses the colored multibounce response; leaves apply their
translucency dim and the configurable grazing response. Standard, specialized
and clustered/forward consumers share those controls. Emission is unchanged.
The game owns its leaf material and preset selection; generic production,
filtering, response and GPU lifetime remain in the engine. ModelSupport is
unchanged.

## Fixed acceptance

| ID | Required mechanism | Qualified evidence |
| --- | --- | --- |
| C1 | Active caller, quality, formats and consumer ownership | Active ConeAO call/source audit, retained retail response constants, fixed contract |
| C2 | Farthest depth, gather ties, associated motion/normal and receiver flags | Independent native prepare-plane comparisons, no-AO and invalid-motion fixtures |
| C3 | All horizon budgets, Bayer/Weyl variation, leaf sampling, depth normals, gathers and cone moments | Independent raw-plane oracle across all tiers and moving/edge/odd views |
| C4 | Depth/normal spatial weights, camera/world cone transform and compact storage | Quantized intermediate readbacks and independent spatial oracle |
| C5 | Motion/depth confidence, neighborhood cone clipping, history weight and resets | Frame-by-frame temporal oracle, camera/rigid/leaf motion, missing motion, cut/skip/resize/generation |
| C6 | Independent views and safe retirement | Interleaved views, return to prior view, retirement and allocation fixtures on both APIs |
| C7 | Full reconstruction and independent material/lobe consumption | Independent upsample/leaf/specular oracles, direct/local/ambient/emission controls and regression suites |
| C8 | Ordinary hosts, presets, correctness, images, cost and exact integration | CPU/native/scene evidence, visual review, matched timings and final handoff manifests |

## Provenance and adapters

The supplied Mainline coneaocs.fx and renderPostFxConeAO.cpp define the active
ordinary pipeline, confirmed at its render-frame caller. include_utilities.fx
supplies the Bayer layout; include_lightingHelpers.fx and the shared constant
buffer define consumers. The alternate SSAO implementation is not substituted
for the active ConeAO path.

Retained retail 2.31 event 7161 resource 956 (CB12) supplies scene overrides:
row 51 is (1.2, .95, 1, .5), and the used row-52 values are (-1, .75, .5).
The readback SHA-256 is
408a1ab1bd70e24a13e2734d88b7255a599f09a05018bd92561abbf6e08a5703.
These differ from generic Mainline art defaults. They are configurable response
defaults here, not proof every retail scene uses those values. Private source
and captured bytes are audit inputs, not shipped runtime dependencies.
The source snapshot is not authenticated as the current retail revision.

The initial C7 wording incorrectly assumed direct lighting should be preserved.
Source/retail consumer review expanded it before final qualification to require
separate direct-lobe and leaf controls. acceptance-contract.json retains both
wordings and the reason. No requirement was waived to obtain completion.

Explicit adapters are conventional Z, top-down UV, ceil-half odd extents,
per-view sample clock, packed receiver flags, evaluated normal storage and the
existing eight-view capacity. Orthographic and explicit legacy diagnostics keep
their prior path; RT, new unsupported hair/layered/transparent materials and
other matrix categories are not counted as completed here.

## Verification

Optimized RelWithDebInfo on gpu-worker's EVGA RTX 2060 SUPER, GPU1:
- 13 CPU groups pass: six engine and seven game.
- 44 producer/history/consumer frames per API pass with clean D3D12/Vulkan
  validation. All five stages are checked against independent double-precision
  calculations using actual quantized prior-stage inputs.
- Independent consumer tests cover 2,711 leaf pixels and 22,788 specular pixels
  per API. Leaf maximum absolute HDR error is .000120261; specular maximum is
  .000034422. Limits are 1/4096 and 1/16384 respectively. Emission controls are
  nonblack and exactly unchanged. Other controls verify direct/local/ambient
  responses, isolated lobes and no brightening.
- 16 existing native regression suites pass, covering reflections, temporal
  reconstruction, dry/wet composition and legacy contact consumers.
- 18 runtime/editor scene pairs cover wet night, dry daylight, orbit, battle and
  all runtime presets. The five new stages appear in normal Medium/High/Ultra
  profiles and are absent on Low. Existing shared composition/temporal reflection
  stages remain. Precipitation metadata matches.
- 16 matched 1080p Medium ABBA timing runs use 140 steady frames each, with
  validation/readbacks disabled. Paired renderer/weather/quality settings match.
  No accepted sample reports thermal throttling.

Numerical checks account explicitly for float32 reciprocal/FMA differences
(one ULP), discontinuous gather footprint boundaries (eight coordinate ULPs),
and hardware linear-sampler eight-fractional-bit interpolation. They test the
derived possible-value interval, not a relaxed global image threshold. UNORM8
stage limits remain one code value, or two for horizon integration. Per-frame
central errors and interval errors are retained. This establishes the mechanism
within arithmetic/storage precision, not byte-identical GPU arithmetic.

Two fresh Vulkan baseline controls precede twelve reused accepted baseline
captures; four editor controls are fresh. All eighteen candidates are fresh.
Reuse requires all 125 baseline payload files to match the prior accepted
screen-reflection candidate exactly. baseline-reuse.json preserves original
manifests and provenance; reused captures are not described as new executions.

The native oracle exposed a missing Vulkan 8x8 workgroup declaration that API
validation alone did not catch. Consumer checks also exposed backwards test
fixture winding and a missing leaf camera input; these were fixed before R6
qualification. Earlier failure logs remain in the task evidence.

## Reproducing the independent checks

On the qualified worker, with engine/game material outputs built and API
validation enabled in the process environment, run from the engine checkout:

    build-presentation/RelWithDebInfo/PhlosionContactComputeNativeTests.exe vulkan artifacts/contact-check/vulkan ../../Games/PokemonCyberpunk
    python tests/ContactComputeOracle.py artifacts/contact-check/vulkan
    python tests/ContactConsumerOracle.py artifacts/contact-check/vulkan

Repeat with d3d12 and a separate output directory. The Python oracles require
NumPy. The native fixture uses synthetic geometry/textures and the built
game-owned leaf material; it does not require raw research captures. Run GPU
qualification in the worker's interactive desktop session, not SSH Session 0.

## Cost and retained scope

| API / scene | Baseline GPU median (ms) | Candidate GPU median (ms) |
| --- | ---: | ---: |
| d3d12 / wet | 16.515 | 15.391 |
| d3d12 / battle | 16.987 | 14.920 |
| vulkan / wet | 17.530 | 16.054 |
| vulkan / battle | 17.246 | 15.511 |

These timings describe this worker, not controller or retail Cyberpunk 2077. The
16.67 ms whole-frame target remains independent of technique coverage.

All four candidate GPU medians are below 16.67 ms, but that does not establish
a locked 60 FPS. Candidate tails remain:

| API / scene | GPU p95 (ms) | CPU-frame p95 (ms) |
| --- | ---: | ---: |
| d3d12 / battle | 16.023 | 27.720 |
| d3d12 / wet | 16.532 | 25.870 |
| vulkan / battle | 16.604 | 16.795 |
| vulkan / wet | 17.328 | 17.271 |

Automatic GPU boost was left enabled. Sparse busy telemetry samples span
roughly 1.63-1.80 GHz medians between runs, with some higher candidate clocks;
these are observed workload timings, not fixed-clock algorithm-only savings.
All runs remain accepted: maximum recorded temperature is 64 C and no thermal
throttle flags are reported. timing-clocks.json retains the sample counts.
CPU tails, especially D3D12, and the broader frame-time target remain open.

Contact texel payload at 1920x1080 is 26,956,812 bytes (25.71 MiB) per active view,
excluding resource alignment and small constant buffers. It is bounded by the
existing view residency policy. Cold shader preparation remains a separate
loading/resource-management concern. First changed-shader scene checks took
305.0 s on D3D12 and 351.9 s on Vulkan including preparation and frames. These
are not controlled cold-start comparisons or time-to-first-frame measurements.

Hybrid rain geometry, material-specific splashes, real-puddle rings, weather
schedules and light-fixture art are unchanged. Other coverage rows retain their
own estimates and acceptance gates.

Evidence: artifacts/lighting-foundation/contact-completion-20261031/evidence.zip
(ignored local archive), and WORKER_EVIDENCE_ROOT/phlosion-contact-completion-20261031 on
gpu-worker. acceptance-results.json, final-tested-source.json,
locked-payload-comparison.json and finalization.json identify final source,
payload and controller/worker handoff. No GitHub push is part of this task.
