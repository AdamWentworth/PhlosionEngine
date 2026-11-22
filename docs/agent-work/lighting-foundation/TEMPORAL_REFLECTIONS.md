# Temporal stochastic reflections - 2026-10-27

Current default, 2026-10-27: PokemonCyberpunk uses the connected temporal renderer
without feature switches. Earlier opt-in/default-off statements below describe
historical checkpoints and are superseded by the default promotion. Quality
selection is Low/Medium/High/Ultra; remaining C1/C7/C8 work improves this same path.

## Scope

PHLOSION_TEMPORAL_REFLECTIONS=1 connects C5/C6 to C3/C4's completed N-1 color
and native hit-object motion in primary field, battle and editor views. It implies
native motion, scene reconstruction and primary-view jitter. Windows D3D12 and
Vulkan are qualified here. Normal launch remains unchanged.

CYBERPUNK_REFLECTION_QUALITY selects low/medium/high/ultra (default medium).
The legacy reference tier is deliberately rejected with the new path; use the
old path for that comparison. The handshake records temporal-stochastic-v1
versus legacy-inline-v1 and actual base rays, steps and roughness cutoff.

This is the implemented Windows consumer milestone, not C1-C6 shipping acceptance,
C1-C8 lighting completion, Linux qualification or measured retail image/FPS parity.

## Source to implementation

The supplied Mainline snapshot is not authenticated as current retail. Research
owns interpretation; source-anchors.json records hashes. Production has no
dependency on source dumps, binaries or extracted noise textures.

| Source anchor | Independent Phlosion implementation | Verification |
| --- | --- | --- |
| renderNode_ScreenSpaceReflections.cpp, ordinary path | Full-output variants; quarter-depth prepare; half-size N-1 feedback; compact result/fade | Native extents and real pass profiles |
| postfx_SSR.fx, ordinary entry/march | GGX/Hammersley, owned blue noise, four-step groups, stride/tolerance, invalidation extra rays, backward/edge/roughness fades | All tiers; hit/miss/cut/roughness/missing-motion controls |
| postfx_SSR.fx, cone sampling | Hit-object velocity, feedback mips 0-4, large-cone three taps, exposure ratio | Moving gradient and N-1 color/exposure fixtures |
| include_lightingModel.fx | Stable normal basis and GGX direction distribution | Shared GLSL/HLSL body; both native APIs |
| include_lightingHelpers.fx, ordinary reflection composition | Independent radiance/fade, probe blend before receiver response | Prepared consumer, scene captures, no source redraw |

Core files: WorldTemporalReflection.h, temporal_reflection_common.glsl,
D3D12/Vulkan temporal backends, world_screen_reflection.glsl. Invalidation is
prepared once before tracing; reconstruction and feedback promotion stay at the
end of composition. Distinct Vulkan prepare/finish descriptor sets protect
already-recorded dispatches from descriptor mutation.

| Tier | Base rays | Steps/ray | Full / probe roughness |
| --- | ---: | ---: | --- |
| Low | 1 | 8 | .3 / .5 |
| Medium | 2 | 12 | .4 / .6 |
| High | 3 | 12 | .5 / .7 |
| Ultra | 4 | 16 | .6 / .8 |

Invalidation adds int(5*mask), total clamped to 1-16. Low's one-ray direction
sampling uses roughness*.75. RGB and confidence are averaged separately across
all rays, including misses. Depth is sqrt-compressed reverse Z. The source's
declared steps-below counter is not incremented in the active ordinary branch;
its TODO was not invented here.

Mode 4 consumes the prepared result and cannot fall through to the old inline
search. Unsupported/transparent receivers use probes. First frame, cut, missing
feedback or missing hit motion produce zero screen confidence. Existing planar
reflection and local-probe captures remain distinct; this removes only the
redundant ordinary SSR source draw.

## Formats and adapters

- Full reflected RGB: R11G11B10 float; independent confidence: R8 UNORM.
- Quarter width/height depth: R16 UNORM. At 1920x1080 these image payloads total
  10,627,200 bytes (10.14 MiB), excluding driver alignment.
- C3/C4 history compaction is qualified in [COMPACT_HISTORY.md](COMPACT_HISTORY.md):
  64.60 MiB/view payload at 1080p, with exact depth/IDs retained. C1 shared-surface compaction and
  explicit participation diagnostics are qualified in COMPACT_SURFACES.md.
  Complete specialized material/normal and producer coverage remain unfinished.
- Owned deterministic void-and-cluster noise: two 64x64 rank channels, 64 shifted/
  rank-rotated frame slices, 1 MiB RGBA8 upload. The offline generator checks
  uniform ranks and spatial low-frequency suppression (NumPy 2.2.6 here).
  These are independent inputs, not the source's exact noise texture content.
- Native conventional projection depth converts to reverse depth; screen UVs are
  top-down and motion is previous-current. Exact-ID invalidation, unsupported
  material validity and missing-producer rejection remain explicit adapters.
  No unobserved weapon-stencil category or complete material classification is claimed.
- Game feedback is scene-linear with preExposure=1. Non-unit exposure is tested
  independently. This is not a replacement probe manager or shadow scheduler.

## Qualification

gpu-worker Windows; optimized RelWithDebInfo; EVGA RTX 2060 SUPER,
LUID 000000000000be8e / UUID GPU_1_UUID.
Evidence: PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/temporal-reflections-20261027 on controller and
WORKER_EVIDENCE_ROOT/phlosion-temporal-reflections-20261027 on the worker.

- Native fixtures on both APIs: all tiers; analytic plane intersections check
  quarter-depth gather/compression; N-1 color; exposure; hit-object gradient
  reprojection; cuts; missing motion; roughness; same-view odd resize;
  retirement/recreation and valid odd-size history.
- Previous temporal scene numerical/display fixture passes after the scheduling
  split. Five engine and six game CPU contracts pass.
- Four rain CPU/GPU checks preserve Hybrid material-specific impacts/rings,
  rain RGB and coverage.
- 24 runtime/editor validation scenarios cover wet/daylight, camera movement,
  battle, enter/return cuts, orbit sequence frames and four default-off controls.
  Controls remain pixel-identical to the preceding accepted baseline. Transition
  captures have zero reflection hits and reject incompatible history.
- New profiles contain temporal-reflection and no screen-reflection-source.
  Wet D3D12 at 1080p had 101,293 confident reflection hits, 480x270 depth and
  960x540 feedback. Versus the preceding temporal/legacy image, 103,805 pixels
  changed (mean byte difference .075); this is not a retail similarity score.
- Final validation audit rejects errors. Known D3D12 upload COMMON warning 1328
  is recorded separately. Bounded visual sequence review: visual-review.json.

## Warm timing

Same candidate, 1920x1080 internal/output, Medium, selected EVGA GPU. Both sides
include C3/C4; only the reflection consumer/source draw changes. Validation and
GPU image readbacks are disabled. Frames 40-179 provide 140 samples/run, excluding
startup and final screenshots. No selected-card thermal slowdown is accepted. The first Vulkan wet new-path run
reported a slowdown flag during shutdown and is excluded; its retry is identified
in the retained phase names. This is one warm run per accepted condition.
Raw mean/median/p95/p99 for GPU, CPU-frame and present, inclusive pass timings
and temperatures are in timing.json. CPU-frame includes waits; do not sum
inclusive pass durations.

| API / scene | Legacy GPU median ms | New GPU median ms | New GPU p95 ms | New CPU-frame median ms | New present median ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| d3d12 / wet | 42.926 | 32.925 | 33.768 | 32.422 | 0.183 |
| d3d12 / dry | 27.986 | 25.575 | 26.109 | 24.361 | 0.175 |
| vulkan / wet | 51.982 | 36.162 | 37.237 | 36.277 | 0.053 |
| vulkan / dry | 42.265 | 28.676 | 29.031 | 28.565 | 0.063 |

These compare worker implementations, not retail Cyberpunk FPS or the controller
GPU. Native-1080p Medium remains a 16.67 ms target. Cold shader preparation remains
expensive and is excluded only from the explicitly warm timing claim.

## Next

Finish remaining C1 evaluated-material/normal and producer coverage, then
connected C1-C6 acceptance. Default promotion is already qualified; see
TEMPORAL_DEFAULTS.md. Compact shared surfaces are qualified in COMPACT_SURFACES.md. C7 clustered/classified
lighting and C8 placed-probe/shadow scheduling remain separate major gaps.
Preserve Hybrid impacts and authored station choices; arbitrary quality caps do
not replace these stages.
