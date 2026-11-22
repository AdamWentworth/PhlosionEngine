# Screen reflection completion - 2026-10-31

**100% estimated technique coverage of the fixed S1-S6 supported Windows
contract.** All six mechanisms are qualified on the ordinary non-RT D3D12/Vulkan
runtime/editor path. This is not identical retail pixels/FPS, whole-engine
coverage or Linux qualification. The fixed acceptance contract and exact tested
source/payload manifests are retained with the evidence.

## Changes and ownership

The preceding material and temporal milestones supplied most of the connected
pipeline. This pass verifies that chain rather than counting it as new work.

- Independent 64-frame/two-channel patterns replace translated 8-bit patterns.
  Both noise channels retain UNORM16 precision. Two byte pairs occupy the same
  1 MiB RGBA8 allocation; the shader decodes them before GGX sampling. The one-ray
  branch retains the unwrapped input, including its UNORM endpoint behavior.
- A missing specialized consumer is connected: game-owned leaves now blend
  prepared screen/probe radiance before their dielectric/DFG response, apply
  the leaf transmission adjustment to the grazing lobe, and retain contact
  specular visibility. Their evaluated normal and roughness already existed.
- Engine-owned prepared-result helpers isolate sampling and composition from
  the legacy reference marchers. The standard compatibility entry points share
  those same prepared helpers, preserving their existing fallback behavior.
  Leaves use the ordinary lookup-only consumer; they do not import planar or
  old exhaustive searches into another shader call graph.
- New native qualification checks actual standard and specialized consumers,
  independently computed ray confidence, previous-hit motion, filtered previous
  color, exposure, material attributes, resets and view lifetime.

The original broad leaf call graph exposed an FXC optimizer stack overflow and
very slow Vulkan shader compilation. Intermediate branch workarounds were
discarded. The final prepared consumer addresses that unnecessary call graph
without changing ray budgets, resolution or rain quality. Cold shader preparation
remains a separately tracked loading/resource-management concern. The first changed-shader scene validation runs took 771.7 s
on D3D12 and 357.7 s on Vulkan, including launch/preparation and frames.
These are not controlled cold-start comparisons or time-to-first-frame measures.
They must not be confused with steady GPU frame costs.

## Fixed acceptance

| ID | Required mechanism | Verified evidence |
| --- | --- | --- |
| S1 | Evaluated dry/wet/metal/mapped-normal/masked/SSS/foliage receivers and exclusions | Actual native material readbacks and reflected-color consumers; existing temporal coverage plus runtime/editor captures |
| S2 | Noise channel/frame/precision, GGX/Hammersley, invalidation rays and quality budgets | Owned-noise audit, all individual tiers, frame wrap and numerical oracle |
| S3 | Quarter compressed depth, stride/tolerance/grouped first hit | Independent analytic plane intersections and double-precision ray confidence over native inputs |
| S4 | Hit motion, N-1 color cones/mips, exposure, fades and lifetime | Gradient displacement, roughness/cone sweep, matched leaf exposure, offscreen prior wall, cuts, odd resize and retirement |
| S5 | Separate radiance/confidence, probe blend before response, no repeated source draw | Standard/SSS/leaf receivers, prepared and compatibility probe regressions, normal composition/profile labels |
| S6 | APIs/hosts/presets, validation, rain preservation and measured cost | Eight reflection suites, twenty probe regressions, eighteen scene pairs, sixteen matched timings and source/payload manifests |

Normal Low omits SSR; its individual one-ray variant is tested directly.
Medium/High/Ultra retain 2/3/4 base rays and 12/12/16 steps. No opt-in feature
switch or extra quality cut is introduced. Hybrid's surface-specific rain
splashes and rings on actual puddles remain unchanged.

## Input provenance and limits

Mainline postfx_SSR.fx and renderNode_ScreenSpaceReflections.cpp define the
ordinary ray/feedback/output/quality contract; include_materialId.fx defines
minimum roughness, include_lightingModel.fx basis/GGX, and
include_lightingHelpers.fx radiance composition and leaf response. The supplied
snapshot is not authenticated as the current retail revision.

Retained retail 2.31 night/rain event 7147, resource 15755 independently supplies
noise evidence: 64x64x64 RGBA16_UNORM, 4,096 uniform ranks per active R/G channel,
constant B/A. Its decoded SHA-256 is 0f142e95b8fd4c9252b7993938c94959cdd16a80077c44b193ed101605fc40d5.
The Mainline snapshot names its noise texture but omits the asset bytes.
Retail evidence is private research input, never a production dependency.

Our independently generated permutations are not byte-identical to either
reference. Owned/retail mean low-to-high spectral power is 0.024436/0.024740;
worst is 0.042512/0.039486. Maximum cross-frame shifted correlation is
0.079207/0.086623. Each active slice/channel has 4,096 uniform ranks, maximum
65520. The compact owned payload is 1,048,576 bytes. This qualifies its input
mechanism/statistics and explicitly does not establish exact asset identity.

First-person weapon/stencil behavior has no producer in this third-person game.
New hair/layered/transparent materials, planar-special handling, contact-visibility
generation, offscreen probe generation/placement, RT/DLSS and Linux belong to
separate scope/categories. Conventional-Z/top-down UV, compact formats and
independent noise remain explicit Phlosion adapters.

## Verification and measured cost

Optimized RelWithDebInfo on gpu-worker's EVGA RTX 2060 SUPER (GPU1):
- 12 CPU test groups pass (5 engine, 7 game).
- 8 native reflection suites pass: standard, secondary view, SSS and foliage
  on each API, with validation enabled and no errors. A final diagnostic-only
  newline correction reruns both affected foliage suites; prior logs are retained.
- 674,448 independent confidence comparisons; worst per-case
  disagreement fraction 0.221893%, mean absolute
  error at most 0.00117303. Declared limits were 1.5% and 0.004;
  these were fixed before running the oracle.
- 20 prepared/compatibility/placed-probe cases are qualified: 18 pass, and
  two pre-existing legacy mirror failures match an unchanged-helper control
  (including image comparisons). The ordinary prepared path passes on both APIs.
- 18 runtime/editor scene pairs cover wet/dry/orbit/battle, both editor scenes,
  and all normal runtime presets. Medium profiles retain the shared composition
  chain without a fresh reflection-source draw. Rain/gameplay state matches.
- 16 baseline/candidate ABBA timing runs use 1080p Medium, night/overcast,
  identical settings, validation/readbacks disabled during 140 steady frames
  per run. All samples are free of reported thermal-throttle flags.

Sixteen scene baseline images reuse the previous task's accepted candidate,
whose 125 payload files are byte-identical to this task's immutable baseline.
Two fresh Vulkan controls confirm that reuse (maximum one display code value).
All eighteen final candidate captures are fresh. The reuse receipt preserves
original manifests; old runs are not relabeled as newly executed tests.

| API / scene | Baseline GPU median (ms) | Candidate GPU median (ms) |
| --- | ---: | ---: |
| d3d12 / wet | 16.024 | 16.970 |
| d3d12 / battle | 16.795 | 17.012 |
| vulkan / wet | 17.257 | 17.506 |
| vulkan / battle | 16.907 | 17.234 |

These are this worker's timings, not controller or retail comparisons. The separate
16.67 ms whole-frame target and broader loading/streaming work remain open.
Technique coverage and frame-time performance are tracked separately. The
measured cost does not change which mechanisms were required for completion.

A post-timing orchestration path error temporarily restored the preceding task's
generated assets. The affected foliage rerun exposed the mismatch. The task root
was corrected, the current candidate hashes restored, and both affected native
suites passed again. The earlier task receipt was recovered from its verified
archive. Scene/timing captures used their explicit correct variant files and
predate this restoration; their manifests remain unchanged. Failure and recovery
receipts are retained in restoration-harness-correction.json and test-log-correction/.

Evidence: artifacts/lighting-foundation/screen-reflections-20261031/evidence.zip
(ignored local evidence archive), and
WORKER_EVIDENCE_ROOT/phlosion-screen-reflections-20261031 on gpu-worker.
acceptance-results.json, final-tested-source.json, locked-payload-comparison.json
and finalization.json record the final acceptance and controller/worker handoff.
No private capture data or reference-source files are distributed with the code.
