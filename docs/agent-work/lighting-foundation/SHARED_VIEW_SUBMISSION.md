# Shared view preparation and draw submission

2026-10-29. Baseline engine d9570e593522129f7ea00bda38ed00e1ed1104ee;
game 6e63b99dd28c2ce32d6334adcdbd88703bdd8b34. Implementation, builds,
tests and processing run on Windows gpu-worker. controller owns review/integration.

## Architecture and scope

The prior packet/material caches removed repeated validation and resource lookup,
but D3D12 still copied shared lighting into every object's constants. Vulkan
avoided duplicate GPU uploads while reconstructing/searching a complete view
state for each primitive. This pass changes ownership and submission in the
ordinary renderer; it introduces no experiment or quality opt-in.

Both backends now expose immutable prepared-view tokens. Normal glTF producers
prepare once per participating material; model/character indexed batches use the
same contract. Matching view inputs across assets share a record. Receiver,
transparency, camera and material weather overrides remain distinct. Object
transforms, material factors, skin palettes and motion continue through their
live paths. Callers must prepare again when a captured input changes.

D3D12 binds a separate 3,136-byte shared lighting/camera-position/shadow block
at b4. The object block shrinks from 3,296 to 160 bytes. Its aligned
object/material/outline allocation shrinks from 3,840 to 768 bytes; a distinct
shared block requires 3,328 aligned bytes once. The frame ring reserves 4,096
bytes per budgeted draw instead of 3,840 to cover the worst case of all-distinct
views: reserved capacity increases 6.7%, rather than being advertised as a memory
saving. Shared payloads use append-only frame allocations and the existing GPU
fence owns reclamation. View-projection remains in the object block.

Vulkan resolves a prepared buffer/offset directly. Full WorldViewState assembly
and cached-upload lookup happen on a preparation miss, not on each primitive
using a valid token. The existing indirect path already prepares per batch.
Materials, transforms and skin/motion storage are not frozen with a view.

The CPU table is bounded at 4,096 records. Owner, generation and index checks
reject foreign/stale tokens. Native camera/pass/frame/resource transitions
invalidate records; nested and consecutive asset batches do not. Entry/exit
guards cover temporary shadow/reflection/probe/editor state and its restoration.
Invalidating CPU records never reclaims an in-flight GPU allocation. A retained
input key can incur safe misses from padding differences; it cannot alias
different input bytes.

No light equations, shader sample counts, rain density, resolution, assets,
reflection budgets or quality presets changed. Hybrid surface-specific splashes
and puddle rings remain intentional game behavior.

## Reference trace

Read-only source snapshot, not authenticated as current retail:

| Reference anchor | Mechanism addressed |
| --- | --- |
| renderConstantBufferShared.cpp, CalculateSharedConstants / BindSharedConstants, line 176 onward | Update shared values separately, then bind the shared buffer. |
| renderStateManager.cpp, SetGlobalShaderConstants / BindGlobalConstants / BindCameraConstants, line 219 onward | Camera/global ownership separate from material/object modification. |
| renderGraphNodes.cpp, CRenderNode_BindGlobalConstants::Execute, line 1270 | Explicit graph-stage shared/global bindings and camera rain/wind resources. |
| renderMaterial.cxx, PrepareRenderStagePassesRange, line 2785 | Retained material-stage preparation distinct from object submission. |

Independent implementation of the relevant ownership mechanism. No source dump
or original game binary is a runtime/build input. Hashes are recorded in
trace.json. This does not reproduce the complete reference material/job graph.

## Interface and normal build compatibility

The C++ renderer interface and WorldTextureData layout changed. Editor project
ABI is 44, model-render ABI 21, and the game battle-VFX ABI is 18. Model providers
with ABIs 6, 19 or 20 are rejected before use; a compatible frame prefix cannot
adapt an incompatible renderer vtable. Engine editor and game host CMake targets
now depend on the model/preview and game VFX/rules modules they load, so a targeted
host build rebuilds them.

The first full-scene candidate uncovered an old PokemonModelRuntime DLL calling
the wrong renderer virtual during mesh prewarm. The worker-only candidate failed;
no controller deployment was made. A debug stack identified this mismatch,
then ABI rejection, dependency fixes and fresh module builds addressed it.
PokemonModelSupport source did not change. All consumers must rebuild against the new
engine; existing projects pinned to the old engine remain on their old ABI.

## Verification

- Optimized MSVC RelWithDebInfo engine editor, game runtime/editor plugin and
  their required model/VFX/preview modules build successfully.
- Five CPU groups pass: EngineContracts, WorldMotionHistory, EnvironmentPackets,
  PreparedViews and ModelInstance. Contracts exercise 1,000 changing objects,
  weather/camera/receiver/transparency overrides, nested/cross-asset batching,
  foreign/stale generations, bounded capacity and old model ABI rejection.
- Twenty-six validation-enabled native GPU checks pass across both Windows APIs:
  four material binding cases, sixteen fresh/retained relighting runs and six
  prepared-view reuse/retirement/transition runs. All eighteen native reference
  image pairs are exact. The renderer checks were retained after the subsequent
  module-only ABI/build repair; CPU groups and full scenes then verified that repair.
- The 128-draw native fixture needs two preparations for two distinct inputs
  versus 128 when forced fresh; both images match exactly. Changing the native
  shadow state invalidates old tokens and the transition fixture matches too.
- Twelve full-scene pairs cover runtime wet/dry/orbit/battle and editor wet/battle.
  Required shared-surface, clustered and probe phases remain; no duplicate SSR
  source draw, API errors or invalid/dropped profile frames. Precipitation matches.
  D3D12's worst RGB8 difference is 5, worst mean 0.007353; Vulkan is within one
  level in all six pairs. Both complete sheets and worst crops were reviewed.
- Historical Vulkan captures initially differed by up to 40. A fresh run of the
  unchanged baseline reproduced the wet difference (max 25), while back-to-back
  baseline/candidate were within one level. All Vulkan baselines were refreshed;
  historical evidence remains in qualification-historical-baselines.json.
  D3D12 baselines reuse the source-identical, byte-equivalent previous accepted
  payload; reused-correctness-baseline.json records provenance.
- All packaged external materials and SPIR-V files match baseline. Embedded
  D3D12 declarations/root bindings changed for the split constants; their lighting
  calculations and sample budgets did not.
- EVGA RTX 2060 SUPER GPU1, LUID 000000000000be8e,
  UUID GPU_1_UUID. No local controller execution.
  Linux and the other quality tiers are not newly qualified by these scene runs.

## Matched warm performance

Native 1920x1080 Medium, fixed night/overcast, heavy Hybrid rain and matching
dry control. MSVC RelWithDebInfo, validation off, profiling on. Fresh wet ABBA
and dry AB on each API; 220 frames/run, analyze 40..179 (280 wet samples/version,
140 dry). Twelve accepted runs, equal paired settings, no API errors or thermal
flags, maximum 63 C. No builds or other GPU workloads overlapped timing.
Cold shader compilation is excluded. Milliseconds:

| API / scene | GPU mean | Whole CPU mean | Active CPU mean | Whole CPU p95 | Whole CPU p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3d12 / wet | 17.507 -> 17.276 | 18.369 -> 17.839 | 13.967 -> 13.693 | 28.608 -> 28.394 | 31.760 -> 29.242 |
| d3d12 / dry | 12.187 -> 12.081 | 13.005 -> 12.736 | 9.992 -> 9.649 | 21.605 -> 21.559 | 23.975 -> 25.156 |
| vulkan / wet | 18.402 -> 18.399 | 18.227 -> 18.222 | 8.172 -> 6.047 | 19.271 -> 19.103 | 19.704 -> 19.520 |
| vulkan / dry | 13.557 -> 13.541 | 13.378 -> 13.366 | 5.347 -> 4.010 | 14.162 -> 14.152 | 14.378 -> 14.421 |

Vulkan active CPU improves 2.125 ms (26.0%) wet and 1.337 ms (25.0%) dry.
Its wet shared-surface static-geometry preparation phase falls from 1.249 to
0.526 ms. GPU means are effectively unchanged: -0.003 ms wet, -0.015 ms dry.
Whole frame time therefore stays GPU-bound despite real CPU headroom.

D3D12 active CPU improves 0.274 ms (2.0%) wet and 0.343 ms (3.4%) dry.
Wet whole CPU improves 0.530 ms; wet GPU mean improves 0.231 ms, but GPU median
only improves 0.075 ms, so this is a modest observed change, not a general GPU
optimization claim. Dry whole-CPU p99 worsens 1.181 ms and active-CPU p99 worsens
2.820 ms despite improved means; keep this tail variability visible.

Timing captures were reviewed separately. All Vulkan pairs and D3D12 dry stay
within one RGB8 level; D3D12 wet repeat two stays within three. Wet repeat one
has 35 pixels above four levels (maximum 30, mean 0.007081), localized to sparse
fine-edge/temporal differences in the inspected crop; no changed light shape,
rain distribution or broad material response. Bit-exact full scenes are not
claimed. Native mechanism controls are exact.

Whole CPU includes waits/presentation; active CPU excludes them. Nested phases
must not be added to parent totals. Per-run and pooled means/medians/p95/p99
remain in analysis-matched.json. Neither backend establishes sustained 60 FPS;
GPU wet means remain 17.276 / 18.399 ms, and D3D12 CPU tails remain substantial.

## Matrix category and remaining completion gates

Estimated technique coverage for the new category moves from a retrospective
30-45% baseline to **55-70%** after this qualification. The increase is for
explicit shared/object separation, prepared consumer records and verified native
lifetime ownership; it is not a visual similarity measurement, retail FPS
comparison or percentage of development time. Retained draw/material programs,
remaining shared camera/material work, parallel recording and scheduling keep
the category incomplete. Lighting equation scores and C7/C8 do not increase
just because this architecture is faster.

This category tracks preparation/submission ownership, separately from lighting
equations and the existing clustered GPU workload category. Do not add or
average their percentages.

| Component | Result / remaining work |
| --- | --- |
| Complete environment/light packet reuse | Qualified by the preceding pass and retained here. |
| Explicit shared records and native bindings | Implemented for normal D3D12/Vulkan glTF/model paths; prepare distinct inputs once per native view generation. |
| View, pass, batch, resource and in-flight lifetime | Audited transitions, stale/foreign tokens, nested/cross-asset batches and frame ownership. |
| Retained material-resource bindings | Existing qualified binding reuse remains; this is not a complete retained material-stage program. |
| Remaining shared camera/probe/history/material constants | D3D12 still uploads view-projection per draw and binds separate probe/contact/reflection data; some material preparation remains per draw. |
| Retained draw descriptions and visibility/traversal | Not complete. glTF/material setup, pose/capture identity and specialized traversals still run. |
| Parallel command preparation/recording and render-job scheduling | Not delivered by this pass. Integrate with producer lifetime and resource ownership before claiming complete reference architecture. |
| Broader clustered feature/channel variants and C8 probe/shadow scheduling | Remain open; this pass does not close C7/C8. |
| Linux and whole-frame 16.67 ms | Not newly qualified / still a separate acceptance gate. |

Next architectural target: retain stable draw/material descriptions across views
and frames, rebuild them only for changed asset/material/pose/visibility inputs,
and remove remaining repeated traversal/preparation while retaining ordered
transparent draws. Measure preparation counts under increasing object counts,
then connect command recording jobs. Keep GPU contact/rain/haze costs visible as
separate work; a CPU ownership change does not remove their shader work.

Worker evidence: WORKER_EVIDENCE_ROOT/phlosion-shared-view-20261029. Small receipts/images
are mirrored under ignored artifacts/lighting-foundation/shared-view-20261029.
Raw captures/profiles remain on the worker.

Key receipts: trace.json, tested-source.json, build.json, cpu.json,
native-candidate/checks.json, lifetime-invalidation-audit.json,
state-writer-audit.json, module-abi-repair.json, qualification.json,
repeat-scenes.json, baseline-refresh.json, visual-review.json,
analysis-matched.json, timing-visual-review.json, strict-build.json,
locked-payload-comparison.json and integration-final.json.
