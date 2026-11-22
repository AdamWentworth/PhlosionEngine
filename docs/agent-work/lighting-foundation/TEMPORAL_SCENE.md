# Temporal scene reconstruction and color feedback - 2026-10-27

This report records the initial C3/C4 producer milestone and its original
qualification. The renderer is now standard in PokemonCyberpunk runtime/editor;
no temporal enable flags are read. C5/C6 consume the N-1 feedback and the
redundant reflection-source scene draw is removed. See
[TEMPORAL_DEFAULTS.md](TEMPORAL_DEFAULTS.md) for the current quality contract and
[COMPACT_HISTORY.md](COMPACT_HISTORY.md) for current formats and performance.

## Source anchors and implementation

The inspected worker snapshot is R6.Root/Mainline. It is not authenticated as the
shipping retail build. These anchors describe the reference architecture and
arithmetic, with independent implementation and explicit format/input adapters.
Production has no dependency on the source snapshot or research tooling.

| Reference anchor | Current producer/consumer |
| --- | --- |
| renderer/src/renderRenderFrame.cpp: InitVelocityBuffer, BuildTemporalInvalidationMask, ApplyTXAA, PrepareFeedbackSSRBuffer, then DOF/motion blur | Existing native primary-view motion precedes reconstruction at endWorldSceneColorPass; display/bloom follows it. Battle screen-feedback composition also reads the reconstructed image. |
| renderNode_AntyAliasing.cpp: BuildTemporalInvalidationMask | Half-size closest-depth metadata, nearest 3x3 velocity selection and 4x4 invalidation dilation in temporal_scene_common.glsl. |
| renderNode_AntyAliasing.cpp: ApplyTXAA; postfx_txaa.fx | Cross/box neighborhood YCoCg bounds, five-tap bicubic history, exposure ratio, luma/motion rejection, compressed-color accumulation and time-scaled minimum current weight. |
| renderNode_Prepare.cpp: PrepareFeedbackSSRBuffer; postfx_SSR.fx: SSR_DOWNSAMPLE | Half-size composed linear color and a full mip chain. Each four-level group starts with the five-tap center/diagonal filter; subsequent levels reduce 2x2 compressed samples. |
| Reference view history and invalidation requirements | WorldTemporalHistory and the two native Temporal.cpp implementations own view/frame/extent/generation state, paired images, reset handling, deferred view retirement and shutdown. |

The primary camera uses an eight-sample pixel jitter. Motion records the actual
current/previous jittered projection; reconstruction does not add a second jitter
correction. Shadow and probe cameras retain their own projections. Field, battle
and editor review views keep independent IDs and histories. Authored shot changes,
encounter boundaries, missing frames, new generations and resized views invalidate
reuse. Each rendered view promotes history once. Feedback metadata is available
only for valid N-1 history while N is being composed, never the unfinished N image.

Current lighting is not pre-exposed: preExposure=1. The API accepts the multiplier
already applied to linear RGB, not final display exposure. Native tests exercise a
changing multiplier. Game elapsed rendered-view time controls the reference
1-pow(.2,clamp(dt,1/60,1/20)) minimum-current blend; deterministic fixtures provide dt.

## Transparency and rain

HDR scene alpha carries reactivity in the standard temporal path. Opaque surfaces clear it;
ordinary transparency records coverage. Reconstruction considers current and
previous coverage so a disappearing effect cannot leave its old image behind.
Missing motion/identity producers are rejected rather than assigned zero motion.

Two integration bugs were found by full-scene checks:
- The additive haze compositor uses alpha=1 as a radiance multiplier. Its temporal
  pipeline now preserves destination reactivity instead of marking the full view.
- Rain RGB already includes texture/depth visibility, but its previous alpha was
  1 across entire rain cards. PremultipliedAdditive (blendMode 3) uses RGB ONE/ONE
  and carries the actual rain visibility independently in alpha. This preserves
  RGB energy and makes the mask follow visible drops. The engine owns the generic
  blend equation; the game supplies its existing rain coverage.

The rain motion, counts, lighting calculation, surface-specific impacts, wetness
and puddle rings are unchanged. The existing rain pixel oracle additionally checks
coverage alpha; its RGB expectation/tolerance is retained.

## Resource and algorithm boundaries

- The original qualification used RGBA32F histories (~120 MiB/view at 1080p).
  Current color/mask images use RGBA16F and feedback uses R11G11B10F; depth
  metadata and exact IDs retain RGBA32F/R32_UINT. Current payload is 64.60 MiB,
  excluding allocator alignment, SSR, readback and shared-surface buffers.
  See COMPACT_HISTORY.md for the tested allocation and precision boundaries.
  Views remain bounded, independently owned, safely retired and released.
- Exact scene IDs and previous projected conventional-Z depth adapt the source's
  class/depth invalidation heuristic. The half-float producer depth tolerance is
  tested at a nonexactly representable depth. This is not the complete source
  stencil/material-class or optional rotation-aware speed heuristic.
- Unsupported/unenrolled surfaces and sky do not receive invented history.
  Translucent particles use coverage rejection rather than deformation motion.
  Complete producer coverage remains a C2 acceptance item.
- Feedback filtering follows the traced arithmetic, but separate dispatches
  replace four-level group-shared scheduling. Mips are separate backend images
  consumed by C5/C6. No opaque geometry is drawn by C4.
- Conservative barriers and first-use compilation remain optimization work.
  Diagnostic GPU readbacks must be absent from performance timing.
- OpenGL is shelved for this game. Its existing material composition/blend contract
  remains coherent, but this feature has no OpenGL temporal implementation.
  Linux Vulkan still needs release qualification.

## Verification

The task evidence is under PROJECTS_ROOT/Phlosion/PhlosionEngine/artifacts/agent-work/temporal-scene-20261027 on controller
and WORKER_EVIDENCE_ROOT/phlosion-temporal-scene-20261027 on gpu-worker. The worker used
optimized RelWithDebInfo builds and the EVGA RTX 2060 SUPER
GPU_1_UUID (LUID 000000000000be8e) for native/game
D3D12/Vulkan checks. These are correctness runs, not clean performance samples.

Native numerical sequences check actual accumulation, first visibility, camera
cuts, exact identity replacement, depth disocclusion, independent views, frame
gaps, exposure changes, transparent appearance/removal, additive energy/coverage,
nonlinear feedback filtering (independent 8/27 oracle), displayed output, odd resize, half-depth precision and view retirement/recreation.
CPU contracts cover lifecycle and projection conventions.

PHLOSION_TEMPORAL_READBACK_PATH is an optional runtime screenshot-frame diagnostic.
It checks finite resolved/feedback values and records full/half extents, invalidation
and reactive coverage. It waits for the GPU and must be absent from benchmarks.

Final evidence: build-r13 and native-final-build succeeded; 5 engine and 6 game
CPU suites passed, plus four rain CPU/pixel-oracle checks in extra.json. The legacy shader oracle uses its own hidden SDL OpenGL context and D3D11 arithmetic fixture; the game renderer stays OpenGL-disabled. Its initial missing-audio/context setup failures are retained separately.
native-r9.json passes on both APIs, including a displayed-image error of
0.401/255 and the independent nonlinear feedback filter fixture. game-checks-r5.json
contains 14 passing runtime/editor checks, including four pixel-identical default
wet/battle controls. transitions.json covers first battle/first returning field
on both APIs with no reused history. extra.json adds dry/daylight checks.

The retained failed attempts are recorded in the task evidence. In particular, early runs with full-screen reactivity and the Vulkan
depth-layout mismatch are not accepted evidence. Candidate r4 was superseded
by the Vulkan display-lifetime fix; native readback alone had not detected that
presentation was still sampling the original image. The displayed-pixel fixture
now covers this error.

## Remaining dependencies

C5/C6 now consume this N-1 feedback in the normal renderer. Continue compact
shared-surface/material coverage under C1, followed by clustered dispatch and
probe/shadow scheduling. C1-C8 completion, Linux qualification and the native
1080p Medium 16.67 ms target remain open.
